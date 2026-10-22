// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Smooth normal baking for inverted-hull toon outlines.
 *
 * Why: the outline material pushes each vertex out along its normal. Where the mesh has hard edges or UV
 * seams, one position has several normals, so the hull splits open into gaps. Baking one averaged ("smooth")
 * normal per position and pushing along that instead keeps the hull closed.
 *
 * Process (ComputeSmoothNormals, shared by both targets)
 *  1. Weld vertices that sit within WeldDistance of each other (spatial grid lookup).
 *  2. Work out the face winding from the mesh's own normals, so mirrored/flipped imports still point outward.
 *  3. Sum angle-weighted face normals per welded position -> one smooth normal per position.
 *  4. Optional thickness mask: rate how tightly the surface bends (radius of curvature) so small features
 *     (noses, fingers) get a thinner outline; blurred so the width changes gradually.
 *  5. Express each smooth normal in tangent space (follows skinning) or object space (static meshes only).
 *
 * Targets
 *  - Vertex colors: RGB = encoded normal, A = thickness mask. Written straight into the mesh's source data
 *    (undoable). Tangents are "locked" first so the build can't recompute a different basis than we encoded against.
 *  - Texture: the same values rasterized in UV space, padded past the island edges, saved as an uncompressed
 *    linear texture the outline material samples in the vertex shader.
 */

#include "Core_BakeNormals.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "ScopedTransaction.h"
#include "SkeletalMeshOperations.h"
#include "SkeletalMeshTypes.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "SmoothNormalBaker"

namespace BakeNormalsImpl
{
	/**
	 * The tangent basis the GPU builds for a vertex (see CalcTangentToLocal in the vertex factories):
	 * Z = normal, Y = cross(Z, tangent) * sign, X = cross(Y, Z) * sign. Orthonormal.
	 */
	bool GetTangentBasis(const FVector3f& Normal, const FVector3f& Tangent, float BinormalSign, FVector3f& OutX, FVector3f& OutY, FVector3f& OutZ)
	{
		const float Sign = BinormalSign < 0.0f ? -1.0f : 1.0f;
		OutZ = Normal.GetSafeNormal();
		OutY = FVector3f::CrossProduct(OutZ, Tangent).GetSafeNormal() * Sign;
		OutX = FVector3f::CrossProduct(OutY, OutZ) * Sign;
		return !OutZ.IsNearlyZero() && !OutY.IsNearlyZero();
	}

	/** Output of ComputeSmoothNormals for one LOD, plus counts for the result message. */
	struct FSmoothNormals
	{
		/** Per vertex instance (indexed by ID value): the smooth normal in the requested space, unit length. */
		TArray<FVector3f> Encoded;
		/** Instances that kept their own normal (welded shells cancelled out or flipped the normal). */
		int32 NumFallback = 0;
		/** Instances whose tangent basis was unusable (encoded as straight up). */
		int32 NumBadTangents = 0;
		/** Per vertex instance: outline width multiplier from surface curvature, 0..1 (only filled when the mask is on). */
		TArray<float> Mask;
		/** Welded vertices whose mask is below full width. */
		int32 NumMasked = 0;
		int32 NumClusters = 0;
	};

	/** Thickness mask options. FullRadius: curvature radius (cm) that still gets the full outline width; Minimum: width at the tightest bends. */
	struct FMaskSettings
	{
		bool bEnabled = false;
		float FullRadius = 4.0f;
		float Minimum = 0.15f;
		int32 BlurPasses = 2;

		FMaskSettings(bool bInEnabled, float InFullRadius, float InMinimum)
			: bEnabled(bInEnabled), FullRadius(InFullRadius), Minimum(InMinimum) {}
	};

	/**
	 * Averages face normals over every vertex within WeldDistance of each other, ignoring hard edges and UV seams.
	 * Corners are weighted by their angle, so the result doesn't depend on how the faces are triangulated.
	 * With the mask on, also rates how tightly the surface bends around each vertex (thin outlines on noses, lips, fingers).
	 */
	void ComputeSmoothNormals(const FMeshDescription& MeshDescription, float WeldDistance, bool bTangentSpace, const FMaskSettings& MaskSettings, FSmoothNormals& Out)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesConstRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
		TVertexInstanceAttributesConstRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();

		// 1. Weld vertices by position (a vertex can be split even when the importer kept positions separate)
		TArray<int32> ClusterOfVertex;
		ClusterOfVertex.Init(INDEX_NONE, MeshDescription.Vertices().GetArraySize());
		TArray<FVector3f> ClusterPositions;
		TMap<FIntVector, TArray<int32>> Grid;
		// Grid cells as big as the weld distance: a match can only be in the same or a neighboring cell
		const float CellSize = FMath::Max(WeldDistance, 0.001f);
		const float WeldDistanceSq = FMath::Square(WeldDistance);

		auto FindCluster = [&](const FVector3f& Position, const FIntVector& Cell) -> int32
		{
			for (int32 X = -1; X <= 1; ++X)
			for (int32 Y = -1; Y <= 1; ++Y)
			for (int32 Z = -1; Z <= 1; ++Z)
			{
				if (const TArray<int32>* Clusters = Grid.Find(Cell + FIntVector(X, Y, Z)))
				{
					for (const int32 Cluster : *Clusters)
					{
						if (FVector3f::DistSquared(ClusterPositions[Cluster], Position) <= WeldDistanceSq)
						{
							return Cluster;
						}
					}
				}
			}
			return INDEX_NONE;
		};

		// Each vertex joins the first cluster in range, or starts a new one
		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			const FIntVector Cell(FMath::FloorToInt32(Position.X / CellSize), FMath::FloorToInt32(Position.Y / CellSize), FMath::FloorToInt32(Position.Z / CellSize));

			int32 Cluster = FindCluster(Position, Cell);
			if (Cluster == INDEX_NONE)
			{
				Cluster = ClusterPositions.Add(Position);
				Grid.FindOrAdd(Cell).Add(Cluster);
			}
			ClusterOfVertex[VertexID.GetValue()] = Cluster;
		}

		// 2. Face winding: match the mesh's own normals, so this works whatever the handedness
		double Orientation = 0.0;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = MeshDescription.GetTriangleVertexInstances(TriangleID);
			const FVector3f P0 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[0])];
			const FVector3f P1 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[1])];
			const FVector3f P2 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[2])];
			const FVector3f FaceNormal = FVector3f::CrossProduct(P1 - P0, P2 - P0);
			Orientation += FVector3f::DotProduct(FaceNormal, Normals[Corners[0]] + Normals[Corners[1]] + Normals[Corners[2]]);
		}
		const float FaceSign = Orientation < 0.0 ? -1.0f : 1.0f;

		// 3. Angle-weighted face normals, summed per welded position
		TArray<FVector3f> Accumulated;
		Accumulated.SetNumZeroed(ClusterPositions.Num());

		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = MeshDescription.GetTriangleVertexInstances(TriangleID);
			FVertexID Vertices[3];
			FVector3f P[3];
			for (int32 i = 0; i < 3; ++i)
			{
				Vertices[i] = MeshDescription.GetVertexInstanceVertex(Corners[i]);
				P[i] = Positions[Vertices[i]];
			}

			const FVector3f FaceNormal = FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]).GetSafeNormal() * FaceSign;
			if (FaceNormal.IsNearlyZero())
			{
				continue; // degenerate
			}

			for (int32 i = 0; i < 3; ++i)
			{
				const FVector3f EdgeA = (P[(i + 1) % 3] - P[i]).GetSafeNormal();
				const FVector3f EdgeB = (P[(i + 2) % 3] - P[i]).GetSafeNormal();
				// Weight by the corner angle: a fan of thin triangles counts the same as one big one
				const float Angle = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(EdgeA, EdgeB), -1.0f, 1.0f));
				Accumulated[ClusterOfVertex[Vertices[i].GetValue()]] += FaceNormal * Angle;
			}
		}

		// 4. Thickness mask from curvature. For each edge between welded vertices, the smooth normals turn by an angle
		// over the edge's length; length / angle is the local radius of curvature. Small radius = thin outline.
		TArray<float> ClusterMask;
		if (MaskSettings.bEnabled)
		{
			const int32 NumClusters = ClusterPositions.Num();
			TArray<FVector3f> ClusterNormals;
			ClusterNormals.SetNumUninitialized(NumClusters);
			for (int32 Cluster = 0; Cluster < NumClusters; ++Cluster)
			{
				ClusterNormals[Cluster] = Accumulated[Cluster].GetSafeNormal();
			}

			// Interior edges show up once per adjacent triangle; that only changes the weighting, not the ratio
			TArray<TPair<int32, int32>> Edges;
			Edges.Reserve(MeshDescription.Triangles().Num() * 3);
			for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
			{
				TArrayView<const FVertexInstanceID> Corners = MeshDescription.GetTriangleVertexInstances(TriangleID);
				int32 C[3];
				for (int32 i = 0; i < 3; ++i)
				{
					C[i] = ClusterOfVertex[MeshDescription.GetVertexInstanceVertex(Corners[i]).GetValue()];
				}
				for (int32 i = 0; i < 3; ++i)
				{
					if (C[i] != C[(i + 1) % 3])
					{
						Edges.Emplace(C[i], C[(i + 1) % 3]);
					}
				}
			}

			TArray<float> SumAngle;
			TArray<float> SumLength;
			SumAngle.SetNumZeroed(NumClusters);
			SumLength.SetNumZeroed(NumClusters);
			for (const TPair<int32, int32>& Edge : Edges)
			{
				const FVector3f& NA = ClusterNormals[Edge.Key];
				const FVector3f& NB = ClusterNormals[Edge.Value];
				if (NA.IsNearlyZero() || NB.IsNearlyZero())
				{
					continue;
				}
				const float Angle = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(NA, NB), -1.0f, 1.0f));
				const float Length = FVector3f::Dist(ClusterPositions[Edge.Key], ClusterPositions[Edge.Value]);
				SumAngle[Edge.Key] += Angle;
				SumAngle[Edge.Value] += Angle;
				SumLength[Edge.Key] += Length;
				SumLength[Edge.Value] += Length;
			}

			const float FullRadius = FMath::Max(MaskSettings.FullRadius, 0.001f);
			const float Minimum = FMath::Clamp(MaskSettings.Minimum, 0.0f, 1.0f);
			ClusterMask.Init(1.0f, NumClusters);
			for (int32 Cluster = 0; Cluster < NumClusters; ++Cluster)
			{
				if (SumAngle[Cluster] > UE_KINDA_SMALL_NUMBER)
				{
					// Map radius to width: >= FullRadius keeps full width, tighter bends shrink toward Minimum
					const float Radius = SumLength[Cluster] / SumAngle[Cluster];
					ClusterMask[Cluster] = FMath::Lerp(Minimum, 1.0f, FMath::Clamp(Radius / FullRadius, 0.0f, 1.0f));
				}
			}

			// Soften so the width doesn't jump between neighboring vertices
			TArray<float> Sum;
			TArray<int32> Count;
			for (int32 Pass = 0; Pass < MaskSettings.BlurPasses; ++Pass)
			{
				Sum = ClusterMask;
				Count.Init(1, NumClusters);
				for (const TPair<int32, int32>& Edge : Edges)
				{
					Sum[Edge.Key] += ClusterMask[Edge.Value];
					Sum[Edge.Value] += ClusterMask[Edge.Key];
					++Count[Edge.Key];
					++Count[Edge.Value];
				}
				for (int32 Cluster = 0; Cluster < NumClusters; ++Cluster)
				{
					ClusterMask[Cluster] = Sum[Cluster] / Count[Cluster];
				}
			}

			Out.NumClusters = NumClusters;
			for (const float Value : ClusterMask)
			{
				Out.NumMasked += Value < 0.99f ? 1 : 0;
			}
		}

		// 5. Per vertex instance, in the requested space
		Out.Encoded.Init(FVector3f(0.0f, 0.0f, 1.0f), MeshDescription.VertexInstances().GetArraySize());
		Out.Mask.Init(1.0f, MeshDescription.VertexInstances().GetArraySize());

		for (const FVertexInstanceID InstanceID : MeshDescription.VertexInstances().GetElementIDs())
		{
			const FVector3f VertexNormal = Normals[InstanceID].GetSafeNormal();
			const int32 Cluster = ClusterOfVertex[MeshDescription.GetVertexInstanceVertex(InstanceID).GetValue()];
			FVector3f Smooth = Accumulated[Cluster].GetSafeNormal();
			if (!ClusterMask.IsEmpty())
			{
				Out.Mask[InstanceID.GetValue()] = ClusterMask[Cluster];
			}

			// Opposite shells welded together (e.g. two-sided cloth) cancel out or flip; push along the vertex's own normal there
			if (!VertexNormal.IsNearlyZero() && (Smooth.IsNearlyZero() || FVector3f::DotProduct(Smooth, VertexNormal) < 0.0f))
			{
				Smooth = VertexNormal;
				++Out.NumFallback;
			}

			FVector3f& Encoded = Out.Encoded[InstanceID.GetValue()];
			if (!bTangentSpace)
			{
				Encoded = Smooth;
				continue;
			}

			FVector3f X, Y, Z;
			if (!GetTangentBasis(Normals[InstanceID], Tangents[InstanceID], BinormalSigns[InstanceID], X, Y, Z))
			{
				++Out.NumBadTangents;
				continue;
			}
			// Tangent space: project onto the vertex's own basis. The shader rebuilds the world normal from the skinned basis
			Encoded = FVector3f(FVector3f::DotProduct(Smooth, X), FVector3f::DotProduct(Smooth, Y), FVector3f::DotProduct(Smooth, Z)).GetSafeNormal();
		}
	}

	/** -1..1 vector to 0..255 bytes (the shader decodes with * 2 - 1). */
	FColor EncodeColor(const FVector3f& Vector, uint8 Alpha)
	{
		auto ToByte = [](float Value) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt((Value * 0.5f + 0.5f) * 255.0f), 0, 255)); };
		return FColor(ToByte(Vector.X), ToByte(Vector.Y), ToByte(Vector.Z), Alpha);
	}

	using VrrCore::ToByte01;
	using VrrCore::EdgeFunction;

	/** One bake: the settings plus the mesh they apply to. */
	class FJob : public FSmoothNormalBakeSettings
	{
	public:
		using ESpace = ESmoothNormalSpace;

		FJob(UObject* InMesh, const FSmoothNormalBakeSettings& InSettings)
			: FSmoothNormalBakeSettings(InSettings), Mesh(InMesh) {}

		bool BakeVertexColors(FText& OutMessage);
		bool BakeTexture(FText& OutMessage);

		UTexture2D* CreatedTexture = nullptr;

	private:
		TWeakObjectPtr<UObject> Mesh;

		bool IsSkeletal() const { return VrrCore::IsSkeletalMesh(Mesh.Get()); }

		/** LOD indices to bake: every LOD with its own mesh data, or just LOD 0. */
		TArray<int32> GetLODsToBake() const
		{
			TArray<int32> LODs = VrrCore::GetLODsWithMeshData(Mesh.Get());
			if (!bAllLODs && LODs.Num() > 1)
			{
				LODs.SetNum(1);
			}
			return LODs;
		}

		FMeshDescription* GetMeshDescription(int32 LODIndex) const { return VrrCore::GetMeshDescription(Mesh.Get(), LODIndex); }
		void ModifyMeshDescription(int32 LODIndex) const;
		void CommitMeshDescription(int32 LODIndex) const;

		/** Makes the description hold the normals/tangents the build would produce, then stops the build recomputing them. */
		void LockTangents(int32 LODIndex, FMeshDescription& MeshDescription) const;
	};

	// MESH ACCESS
	/** Marks the LOD source data as about to change (needed for undo and for the commit to rebuild). */
	void FJob::ModifyMeshDescription(int32 LODIndex) const
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get()))
		{
			StaticMesh->ModifyMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.Get()))
		{
			SkeletalMesh->ModifyMeshDescription(LODIndex);
		}
	}

	/** Saves the edited source data back into the asset. */
	void FJob::CommitMeshDescription(int32 LODIndex) const
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get()))
		{
			StaticMesh->CommitMeshDescription(LODIndex);
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.Get()))
		{
			SkeletalMesh->CommitMeshDescription(LODIndex);
		}
	}

	/**
	 * Tangent-space normals only decode correctly against the exact basis they were encoded with.
	 * So: compute the normals/tangents now, exactly like the build would, store them in the source data,
	 * and turn off "Recompute Normals/Tangents" so later builds keep them.
	 */
	void FJob::LockTangents(int32 LODIndex, FMeshDescription& MeshDescription) const
	{
		// Same steps and options the build uses, so the stored result is what would have been rendered
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get()))
		{
			FMeshBuildSettings& Settings = StaticMesh->GetSourceModel(LODIndex).BuildSettings;

			EComputeNTBsFlags Flags = EComputeNTBsFlags::BlendOverlappingNormals;
			if (Settings.bComputeWeightedNormals)	{ Flags |= EComputeNTBsFlags::WeightedNTBs; }
			if (Settings.bRecomputeNormals)			{ Flags |= EComputeNTBsFlags::Normals; }
			if (Settings.bRecomputeTangents)		{ Flags |= EComputeNTBsFlags::Tangents; }
			if (Settings.bUseMikkTSpace)			{ Flags |= EComputeNTBsFlags::UseMikkTSpace; }
			if (Settings.bRemoveDegenerates)		{ Flags |= EComputeNTBsFlags::IgnoreDegenerateTriangles; }

			FStaticMeshOperations::ComputeTriangleTangentsAndNormals(MeshDescription, 0.0f, *StaticMesh->GetPathName());
			FStaticMeshOperations::ComputeTangentsAndNormals(MeshDescription, Flags);

			Settings.bRecomputeNormals = false;
			Settings.bRecomputeTangents = false;
		}
		else if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.Get()))
		{
			FSkeletalMeshBuildSettings& Settings = SkeletalMesh->GetLODInfo(LODIndex)->BuildSettings;

			EComputeNTBsFlags Flags = EComputeNTBsFlags::None;
			if (Settings.bComputeWeightedNormals)	{ Flags |= EComputeNTBsFlags::WeightedNTBs; }
			if (Settings.bRecomputeNormals)			{ Flags |= EComputeNTBsFlags::Normals; }
			if (Settings.bRecomputeTangents)		{ Flags |= EComputeNTBsFlags::Tangents; }
			if (Settings.bUseMikkTSpace)			{ Flags |= EComputeNTBsFlags::UseMikkTSpace; }

			FSkeletalMeshOperations::ComputeTriangleTangentsAndNormals(MeshDescription, UE_SMALL_NUMBER, *SkeletalMesh->GetPathName());
			FSkeletalMeshOperations::ComputeTangentsAndNormals(MeshDescription, Flags);

			// The skeletal build doesn't keep these (matches FSkeletalMeshOperations::ValidateFixComputeMeshDescriptionData)
			MeshDescription.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Normal);
			MeshDescription.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Tangent);
			MeshDescription.TriangleAttributes().UnregisterAttribute(MeshAttribute::Triangle::Binormal);

			Settings.bRecomputeNormals = false;
			Settings.bRecomputeTangents = false;
		}
	}

	// BAKING
	/**
	 * Vertex color target. In one undo transaction, for each LOD:
	 *  1. open the LOD's source data, 2. lock tangents, 3. compute smooth normals,
	 *  4. write them into the vertex colors, then 5. rebuild the mesh and 6. report.
	 */
	bool FJob::BakeVertexColors(FText& OutMessage)
	{
		const TArray<int32> LODs = GetLODsToBake();
		if (LODs.IsEmpty())
		{
			OutMessage = LOCTEXT("NoLODs", "No LOD has mesh data to bake into.");
			return false;
		}

		const bool bTangentSpace = Space == ESpace::Tangent;
		const bool bSkeletal = IsSkeletal();
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.Get());
		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get());

		FScopedSlowTask SlowTask(LODs.Num() + 1, LOCTEXT("BakingColors", "Baking smooth normals into vertex colors..."));
		SlowTask.MakeDialog();

		const FScopedTransaction Transaction(LOCTEXT("BakeTransaction", "Bake Smooth Normals"));

		const FMaskSettings MaskSettings(bBakeThicknessMask, MaskFullRadius, MaskMinimum);
		int32 NumFallback = 0;
		int32 NumBadTangents = 0;
		int32 NumMasked = 0;
		int32 NumClusters = 0;
		{
			// Skeletal meshes rebuild (and their components re-register) once this scope ends
			TUniquePtr<FScopedSkeletalMeshPostEditChange> SkeletalScope;
			if (SkeletalMesh)
			{
				SkeletalScope = MakeUnique<FScopedSkeletalMeshPostEditChange>(SkeletalMesh);
			}

			Mesh->Modify();

			// 1. Each LOD with its own source data is baked separately (they have different vertices)
			for (const int32 LOD : LODs)
			{
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("BakingLOD", "LOD {0}"), LOD));

				ModifyMeshDescription(LOD);
				FMeshDescription* MeshDescription = GetMeshDescription(LOD);
				if (!MeshDescription)
				{
					continue;
				}

				// 2. Freeze the tangent basis the normals are encoded against
				if (bTangentSpace && bLockTangents)
				{
					LockTangents(LOD, *MeshDescription);
				}

				// 3. Compute the smooth normals for this LOD
				FSmoothNormals Smooth;
				ComputeSmoothNormals(*MeshDescription, WeldDistance, bTangentSpace, MaskSettings, Smooth);
				NumFallback += Smooth.NumFallback;
				NumBadTangents += Smooth.NumBadTangents;
				if (LOD == LODs[0])
				{
					NumMasked = Smooth.NumMasked;
					NumClusters = Smooth.NumClusters;
				}

				// 4. Write them into the vertex colors: RGB = normal, A = thickness mask (or the old alpha)
				FStaticMeshAttributes Attributes(*MeshDescription);
				TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();

				for (const FVertexInstanceID InstanceID : MeshDescription->VertexInstances().GetElementIDs())
				{
					// Alpha is stored linearly by both mesh builds
					const uint8 Alpha = bBakeThicknessMask ? ToByte01(Smooth.Mask[InstanceID.GetValue()])
						: bKeepAlpha ? ToByte01(Colors[InstanceID].W) : 255;
					const FColor Color = EncodeColor(Smooth.Encoded[InstanceID.GetValue()], Alpha);

					// The builds turn these into 8-bit colors differently: skeletal meshes copy the values as they are,
					// static meshes convert linear to sRGB. Store whatever makes the GPU see exactly Color.
					const FLinearColor Stored = bSkeletal ? Color.ReinterpretAsLinear() : FLinearColor(Color);
					Colors[InstanceID] = FVector4f(Stored);
				}

				CommitMeshDescription(LOD);
			}

			// 5. Rebuild the render data (skeletal meshes rebuild when SkeletalScope ends)
			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Rebuilding", "Rebuilding mesh..."));
			if (StaticMesh)
			{
				StaticMesh->PostEditChange();
			}
		}

		// 6. Summary, plus notes for anything the user should know about
		OutMessage = FText::Format(LOCTEXT("ColorsBaked", "Baked {0} {0}|plural(one=LOD,other=LODs) of {1} into vertex color RGB ({2} space)."),
			LODs.Num(), FText::FromString(Mesh->GetName()), bTangentSpace ? LOCTEXT("Tangent", "tangent") : LOCTEXT("Object", "object"));

		if (bBakeThicknessMask)
		{
			OutMessage = FText::Format(LOCTEXT("MaskNote", "{0} Thickness mask in alpha: {1} of {2} vertices ({3}) get a thinner outline."),
				OutMessage, NumMasked, NumClusters, FText::AsPercent(NumClusters > 0 ? static_cast<float>(NumMasked) / NumClusters : 0.0f));
		}
		if (NumFallback > 0)
		{
			OutMessage = FText::Format(LOCTEXT("FallbackNote", "{0} {1} vertices kept their own normal (opposite faces welded together, e.g. two-sided geometry)."), OutMessage, NumFallback);
		}
		if (NumBadTangents > 0)
		{
			OutMessage = FText::Format(LOCTEXT("BadTangentNote", "{0} {1} vertices had no usable tangents and point straight out."), OutMessage, NumBadTangents);
		}
		if (bTangentSpace && !bLockTangents)
		{
			OutMessage = FText::Format(LOCTEXT("UnlockedNote", "{0} Tangents weren't locked: if the build recomputes them, outlines may bend the wrong way."), OutMessage);
		}
		return true;
	}

	/**
	 * Texture target (LOD 0 only, since the texture is shared by every LOD that uses the same UVs):
	 *  1. lock tangents, 2. rasterize the smooth normals into UV space, 3. pad the islands,
	 *  4. encode to pixels, 5. create the texture asset.
	 */
	bool FJob::BakeTexture(FText& OutMessage)
	{
		const bool bTangentSpace = Space == ESpace::Tangent;
		const int32 Size = Resolution;

		// Pick a free asset name first, so nothing is changed if the path is bad
		FString PackageName, AssetName;
		if (!VrrCore::MakeUniqueAssetPath(OutputFolder, OutputName, PackageName, AssetName))
		{
			OutMessage = FText::Format(LOCTEXT("BadTexturePath", "Invalid output path: {0}"), FText::FromString(OutputFolder / OutputName));
			return false;
		}

		FScopedSlowTask SlowTask(3.0f, LOCTEXT("BakingTexture", "Baking smooth normals into a texture..."));
		SlowTask.MakeDialog();

		// 1. Lock tangents on LOD 0 (changes the mesh, so it's undoable and rebuilds it)
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("PreparingMesh", "Preparing mesh..."));
		if (bTangentSpace && bLockTangents)
		{
			const FScopedTransaction Transaction(LOCTEXT("LockTangentsTransaction", "Lock Tangents for Smooth Normal Bake"));

			USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh.Get());
			TUniquePtr<FScopedSkeletalMeshPostEditChange> SkeletalScope;
			if (SkeletalMesh)
			{
				SkeletalScope = MakeUnique<FScopedSkeletalMeshPostEditChange>(SkeletalMesh);
			}

			Mesh->Modify();
			ModifyMeshDescription(0);
			if (FMeshDescription* MeshDescription = GetMeshDescription(0))
			{
				LockTangents(0, *MeshDescription);
				CommitMeshDescription(0);
			}
			if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get()))
			{
				StaticMesh->PostEditChange();
			}
		}

		const FMeshDescription* MeshDescription = GetMeshDescription(0);
		if (!MeshDescription)
		{
			OutMessage = LOCTEXT("NoLOD0", "LOD 0 has no mesh data.");
			return false;
		}

		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		if (UVChannel >= UVs.GetNumChannels())
		{
			OutMessage = FText::Format(LOCTEXT("NoUVChannel", "The mesh has no UV channel {0}."), UVChannel);
			return false;
		}

		// Smooth normals per vertex instance, in the space the texture stores
		FSmoothNormals Smooth;
		ComputeSmoothNormals(*MeshDescription, WeldDistance, bTangentSpace, FMaskSettings(bBakeThicknessMask, MaskFullRadius, MaskMinimum), Smooth);

		// 2. Rasterize every triangle in UV space, interpolating the encoded normals
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Rasterizing", "Rasterizing..."));
		const int32 NumTexels = Size * Size;
		TArray<FVector3f> Texels;
		Texels.SetNumZeroed(NumTexels);
		TArray<float> MaskTexels;
		MaskTexels.Init(1.0f, NumTexels);
		TBitArray<> Filled(false, NumTexels);
		TBitArray<> Conflict(false, NumTexels);
		int32 NumOutOfRange = 0;

		for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = MeshDescription->GetTriangleVertexInstances(TriangleID);

			FVector2f Pixel[3];
			FVector3f Value[3];
			float MaskValue[3];
			bool bOutOfRange = false;
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2f UV = UVs.Get(Corners[i], UVChannel);
				bOutOfRange |= UV.X < -0.001f || UV.X > 1.001f || UV.Y < -0.001f || UV.Y > 1.001f;
				Pixel[i] = UV * static_cast<float>(Size);
				Value[i] = Smooth.Encoded[Corners[i].GetValue()];
				MaskValue[i] = Smooth.Mask[Corners[i].GetValue()];
			}
			NumOutOfRange += bOutOfRange ? 1 : 0;

			const float Area = EdgeFunction(Pixel[0], Pixel[1], Pixel[2]);
			if (FMath::Abs(Area) < UE_SMALL_NUMBER)
			{
				continue;
			}

			const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const FVector2f Center(X + 0.5f, Y + 0.5f);
					const float W0 = EdgeFunction(Pixel[1], Pixel[2], Center) / Area;
					const float W1 = EdgeFunction(Pixel[2], Pixel[0], Center) / Area;
					const float W2 = 1.0f - W0 - W1;
					constexpr float Epsilon = -1e-4f;
					if (W0 < Epsilon || W1 < Epsilon || W2 < Epsilon)
					{
						continue;
					}

					const FVector3f Normal = (Value[0] * W0 + Value[1] * W1 + Value[2] * W2).GetSafeNormal();
					const int32 Index = Y * Size + X;

					// Overlapping or mirrored UVs: two places on the mesh want the same texel
					if (Filled[Index] && FVector3f::DistSquared(Texels[Index], Normal) > 0.01f)
					{
						Conflict[Index] = true;
					}
					Texels[Index] = Normal;
					MaskTexels[Index] = MaskValue[0] * W0 + MaskValue[1] * W1 + MaskValue[2] * W2;
					Filled[Index] = true;
				}
			}
		}

		// 3. Pad the islands so bilinear filtering at their edges stays on valid data
		TArray<int32> NewlyFilled;
		for (int32 Pass = 0; Pass < Padding; ++Pass)
		{
			NewlyFilled.Reset();
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const int32 Index = Y * Size + X;
					if (Filled[Index])
					{
						continue;
					}

					FVector3f Sum = FVector3f::ZeroVector;
					float MaskSum = 0.0f;
					int32 Count = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if ((DX || DY) && NX >= 0 && NX < Size && NY >= 0 && NY < Size && Filled[NY * Size + NX])
							{
								Sum += Texels[NY * Size + NX];
								MaskSum += MaskTexels[NY * Size + NX];
								++Count;
							}
						}
					}
					if (Count > 0)
					{
						Texels[Index] = Sum.GetSafeNormal();
						MaskTexels[Index] = MaskSum / Count;
						NewlyFilled.Add(Index);
					}
				}
			}
			if (NewlyFilled.IsEmpty())
			{
				break;
			}
			for (const int32 Index : NewlyFilled)
			{
				Filled[Index] = true;
			}
		}

		// 4. Encode to BGRA8 (empty texels point straight out) and count UV conflicts
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("CreatingTexture", "Creating texture..."));
		TArray<uint8> Pixels;
		Pixels.SetNumUninitialized(NumTexels * 4);
		int32 NumConflicts = 0;
		for (int32 Index = 0; Index < NumTexels; ++Index)
		{
			const FColor Color = EncodeColor(Filled[Index] ? Texels[Index] : FVector3f(0.0f, 0.0f, 1.0f), ToByte01(MaskTexels[Index]));
			Pixels[Index * 4 + 0] = Color.B;
			Pixels[Index * 4 + 1] = Color.G;
			Pixels[Index * 4 + 2] = Color.R;
			Pixels[Index * 4 + 3] = Color.A;
			NumConflicts += Conflict[Index] ? 1 : 0;
		}

		// 5. Create the texture asset (unsaved, like the other Toolbox outputs)
		UPackage* Package = CreatePackage(*PackageName);
		UTexture2D* NewTexture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		NewTexture->PreEditChange(nullptr);
		NewTexture->Source.Init(Size, Size, 1, 1, TSF_BGRA8, Pixels.GetData());

		// Exact 8-bit data: no sRGB, no block compression, no mips (World Position Offset samples mip 0),
		// and always resident so the vertex shader never reads a lower streamed mip
		NewTexture->SRGB = false;
		NewTexture->CompressionSettings = TC_VectorDisplacementmap;
		NewTexture->MipGenSettings = TMGS_NoMipmaps;
		NewTexture->NeverStream = true;
		NewTexture->Filter = TF_Bilinear;
		NewTexture->PostEditChange();

		FAssetRegistryModule::AssetCreated(NewTexture);
		Package->MarkPackageDirty();

		CreatedTexture = NewTexture;

		OutMessage = FText::Format(LOCTEXT("TextureBaked", "Created {0} ({1}x{1}, UV {2}, {3} space)."),
			FText::FromString(AssetName), Size, UVChannel, bTangentSpace ? LOCTEXT("Tangent", "tangent") : LOCTEXT("Object", "object"));

		if (NumConflicts > 0)
		{
			OutMessage = FText::Format(LOCTEXT("ConflictNote", "{0} Warning: {1} texels are used by more than one part of the mesh (overlapping or mirrored UVs); those parts get the wrong direction. Use a UV channel with unique UVs, or bake to vertex colors."),
				OutMessage, NumConflicts);
		}
		if (NumOutOfRange > 0)
		{
			OutMessage = FText::Format(LOCTEXT("OutOfRangeNote", "{0} {1} triangles have UVs outside 0-1 and were clipped."), OutMessage, NumOutOfRange);
		}
		return true;
	}
}

namespace SmoothNormalBaker
{
	/** True when the mesh already has vertex colors (the page warns before they are overwritten). */
	bool HasVertexColors(UObject* Mesh)
	{
		if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			return SkeletalMesh->GetHasVertexColors();
		}

		// Static meshes have no flag; colors default to white
		if (const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0))
		{
			FStaticMeshConstAttributes Attributes(*MeshDescription);
			TVertexInstanceAttributesConstRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();
			for (const FVertexInstanceID InstanceID : MeshDescription->VertexInstances().GetElementIDs())
			{
				if (!Colors[InstanceID].Equals(FVector4f(1.0f, 1.0f, 1.0f, 1.0f), 0.001f))
				{
					return true;
				}
			}
		}
		return false;
	}

	/** SM_Rock -> T_Rock_SmoothN */
	FString SuggestTextureName(const UObject* Mesh)
	{
		return FString::Printf(TEXT("T_%s_SmoothN"), *VrrCore::GetBaseAssetName(Mesh));
	}

	/** Entry point: checks the mesh, fixes settings that can't work for it, then runs the chosen target. */
	FSmoothNormalBakeResult Bake(UObject* Mesh, const FSmoothNormalBakeSettings& Settings)
	{
		FSmoothNormalBakeResult Result;
		if (!VrrCore::IsStaticOrSkeletalMesh(Mesh) || !VrrCore::GetMeshDescription(Mesh, 0))
		{
			Result.Message = LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data.");
			return Result;
		}

		FSmoothNormalBakeSettings Checked = Settings;
		if (VrrCore::IsSkeletalMesh(Mesh) && Checked.Space == ESmoothNormalSpace::Object)
		{
			// Object space doesn't follow skinning
			Checked.Space = ESmoothNormalSpace::Tangent;
		}

		BakeNormalsImpl::FJob Job(Mesh, Checked);
		Result.bSuccess = Checked.Target == ESmoothNormalTarget::VertexColor
			? Job.BakeVertexColors(Result.Message)
			: Job.BakeTexture(Result.Message);
		Result.Texture = Job.CreatedTexture;
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
