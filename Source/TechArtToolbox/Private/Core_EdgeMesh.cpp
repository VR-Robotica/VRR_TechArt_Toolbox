// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Edge mesh: thin strips that hug a Static Mesh's sharp outer edges, built as their own mesh for decal materials.
 *
 * Strategy
 *  Edge wear, chipped paint and grime live on an object's outer edges. Painting them into the object's own textures
 *  ties them to its UVs and texel density; a separate edge mesh gets its own clean UV strip along every edge, so one
 *  tiling decal texture works on any mesh at any size, and it can be swapped or turned off per instance.
 *  The edges come from the geometry itself, the same way Bake Mesh Maps finds its Edge mask (sharp angles between
 *  faces), not from the baked texture: the texture is a blurred, UV-space copy of that information, and going back
 *  from it to 3D would lose precision at UV seams.
 *
 * Process
 *  1. Weld the mesh by position (Core_MeshWeld), so hard edges and UV seams don't split creases.
 *  2. Creases: edges between two faces meeting at Min Angle or more; convex only by default (outer edges).
 *  3. Chains: join creases end to end through vertices where exactly two meet and the line turns less than
 *     Max Turn Angle. Corners (three creases, or a sharp turn) end chains, so strips don't twist around them.
 *  4. Drop chains shorter than Min Length: small details go, long outer edges stay.
 *  5. Strips: along each chain, a band folded over the crease: Width onto each of the two faces, floating
 *     Surface Offset above them. At each chain point the two neighboring segments' directions are averaged
 *     (a mitre) so the band bends smoothly along curved edges.
 *     UVs: U = distance along the chain / tile length, V = 0 on one side, 0.5 on the crease, 1 on the other.
 *  6. Write the strips into a Static Mesh with the source's pivot, one material slot ("Edges"), no lightmap UVs.
 */

#include "Core_EdgeMesh.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core_Common.h"
#include "Core_MeshWeld.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "EdgeMesh"

namespace EdgeMeshImpl
{
	using namespace VrrMeshWeld;

	/** A crease: the edge between welded vertices A and B, and the two faces that meet there. */
	struct FCrease
	{
		int32 A;
		int32 B;
		int32 T1;
		int32 T2;
	};

	/** Creases end to end. Vertices has one more entry than Creases, or the same number when the chain is a closed loop. */
	struct FChain
	{
		TArray<int32> Vertices;
		TArray<int32> Creases;
		bool bClosed = false;
		float Length = 0.0f;	// cm
	};

	/** The third vertex of triangle T, the one not on edge AB. */
	int32 OppositeVertex(const FWeldedMesh& Mesh, int32 T, int32 A, int32 B)
	{
		const FIntVector& Tri = Mesh.Triangles[T];
		return (Tri.X != A && Tri.X != B) ? Tri.X : (Tri.Y != A && Tri.Y != B) ? Tri.Y : Tri.Z;
	}

	/**
	 * Step 2: every manifold edge whose faces meet at MinAngle or more.
	 * Convex test (as in Bake Mesh Maps): the far corner of the second face sits below the first face's plane.
	 */
	void FindCreases(const FWeldedMesh& Mesh, const FEdgeMeshSettings& Settings, TArray<FCrease>& Out)
	{
		const float CosMin = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(Settings.MinAngle, 0.0f, 179.0f)));
		for (const TPair<uint64, TArray<int32, TInlineAllocator<2>>>& Edge : Mesh.EdgeTriangles)
		{
			// Open borders and non-manifold edges have no single fold to follow
			if (Edge.Value.Num() != 2)
			{
				continue;
			}
			int32 A, B;
			SplitEdgeKey(Edge.Key, A, B);
			const int32 T1 = Edge.Value[0];
			const int32 T2 = Edge.Value[1];

			// 2a. Angle between the faces: flatter than MinAngle isn't a crease
			if (FVector3f::DotProduct(Mesh.FaceNormals[T1], Mesh.FaceNormals[T2]) > CosMin)
			{
				continue;
			}

			// 2b. Outer edges only, unless inner (valley) creases are wanted too
			const int32 Opposite = OppositeVertex(Mesh, T2, A, B);
			const bool bConvex = FVector3f::DotProduct(Mesh.FaceNormals[T1], Mesh.Positions[Opposite] - Mesh.Positions[A]) < 0.0f;
			if (!bConvex && Settings.bConvexOnly)
			{
				continue;
			}
			Out.Add({ A, B, T1, T2 });
		}
	}

	/**
	 * Step 3: creases joined into chains.
	 *  3a. Index the creases at each vertex.
	 *  3b. A chain runs on through a vertex only when exactly two creases meet there and the line turns gently.
	 *  3c. Walk open chains from every vertex where chains end, then whatever is left: closed loops.
	 */
	void BuildChains(const FWeldedMesh& Mesh, const TArray<FCrease>& Creases, float MaxTurnAngle, TArray<FChain>& Out)
	{
		// 3a.
		TArray<TArray<int32>> CreasesOfVertex;
		CreasesOfVertex.SetNum(Mesh.Positions.Num());
		for (int32 C = 0; C < Creases.Num(); ++C)
		{
			CreasesOfVertex[Creases[C].A].Add(C);
			CreasesOfVertex[Creases[C].B].Add(C);
		}
		auto Other = [&](int32 C, int32 V) { return Creases[C].A == V ? Creases[C].B : Creases[C].A; };

		// 3b.
		const float CosTurn = FMath::Cos(FMath::DegreesToRadians(FMath::Clamp(MaxTurnAngle, 0.0f, 180.0f)));
		auto Continues = [&](int32 V)
		{
			const TArray<int32>& Here = CreasesOfVertex[V];
			if (Here.Num() != 2)
			{
				return false;
			}
			const FVector3f In = (Mesh.Positions[V] - Mesh.Positions[Other(Here[0], V)]).GetSafeNormal();
			const FVector3f Next = (Mesh.Positions[Other(Here[1], V)] - Mesh.Positions[V]).GetSafeNormal();
			return FVector3f::DotProduct(In, Next) >= CosTurn;
		};

		// 3c.
		TBitArray<> Used(false, Creases.Num());
		auto Walk = [&](int32 Start, int32 FirstCrease)
		{
			FChain Chain;
			Chain.Vertices.Add(Start);
			int32 V = Start;
			int32 C = FirstCrease;
			while (true)
			{
				Used[C] = true;
				Chain.Creases.Add(C);
				const int32 Next = Other(C, V);
				Chain.Length += FVector3f::Distance(Mesh.Positions[V], Mesh.Positions[Next]);

				// Back at the start: a loop if the start itself continues smoothly, else an open chain ending there
				if (Next == Start && Continues(Start))
				{
					Chain.bClosed = true;
					break;
				}
				Chain.Vertices.Add(Next);
				if (Next == Start || !Continues(Next))
				{
					break;
				}
				const TArray<int32>& Here = CreasesOfVertex[Next];
				const int32 NextCrease = Here[0] == C ? Here[1] : Here[0];
				if (Used[NextCrease])
				{
					break;
				}
				V = Next;
				C = NextCrease;
			}
			Out.Add(MoveTemp(Chain));
		};
		for (int32 V = 0; V < CreasesOfVertex.Num(); ++V)
		{
			if (!CreasesOfVertex[V].IsEmpty() && !Continues(V))
			{
				for (const int32 C : CreasesOfVertex[V])
				{
					if (!Used[C])
					{
						Walk(V, C);
					}
				}
			}
		}
		for (int32 C = 0; C < Creases.Num(); ++C)
		{
			if (!Used[C])
			{
				Walk(Creases[C].A, C);
			}
		}
	}

	/** Steps 1-4 for one mesh: welded mesh, creases, and the chains split into kept and dropped. */
	struct FAnalysis
	{
		FWeldedMesh Welded;
		TArray<FCrease> Creases;
		TArray<FChain> Chains;
		TArray<bool> Kept;	// per chain
	};

	bool Analyze(UStaticMesh* Mesh, const FEdgeMeshSettings& Settings, FAnalysis& Out, FText& OutError)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			OutError = LOCTEXT("NoMeshData", "This mesh has no source mesh data for LOD 0.");
			return false;
		}
		// 1.
		BuildWeldedMesh(*MeshDescription, Settings.WeldDistance, Out.Welded);
		// 2.
		FindCreases(Out.Welded, Settings, Out.Creases);
		// 3.
		BuildChains(Out.Welded, Out.Creases, Settings.MaxTurnAngle, Out.Chains);
		// 4.
		Out.Kept.SetNumUninitialized(Out.Chains.Num());
		for (int32 i = 0; i < Out.Chains.Num(); ++i)
		{
			Out.Kept[i] = Out.Chains[i].Length >= Settings.MinLength;
		}
		return true;
	}

	/** One strip vertex. */
	struct FStripVertex
	{
		FVector3f Position;
		FVector3f Normal;
		FVector2f UV;
	};

	/**
	 * Step 5 for one chain: appends its strip (4 vertices per chain point, 4 triangles per segment).
	 *  5a. Per segment: its direction, and for each face the direction across it (in the face, away from the crease),
	 *      sorted into the left and right side of the chain so sides stay consistent along it.
	 *  5b. Per chain point: average the sides of the one or two segments touching it (a mitre), widened so the strip
	 *      keeps Width measured square to each segment.
	 *  5c. Emit left edge, crease (left normal), crease (right normal), right edge. Closed loops repeat their first
	 *      point at the end, so U runs on without wrapping back to 0 mid-strip.
	 *  5d. Two quads per segment, wound like the source mesh (FaceSign) so they face outward.
	 */
	void BuildStrip(const FWeldedMesh& Mesh, const TArray<FCrease>& Creases, const FChain& Chain, const FEdgeMeshSettings& Settings,
		TArray<FStripVertex>& Vertices, TArray<int32>& Indices)
	{
		const int32 NumSegments = Chain.Creases.Num();
		const int32 NumPoints = Chain.Vertices.Num();
		if (NumSegments == 0)
		{
			return;
		}
		const float Width = FMath::Max(Settings.Width, 0.001f);
		const float Offset = FMath::Max(Settings.SurfaceOffset, 0.0f);
		const float TileLength = Settings.UTileLength > 0.0f ? Settings.UTileLength : 2.0f * Width;

		// 5a.
		struct FSide { FVector3f Across; FVector3f Normal; };
		TArray<FSide> Left, Right;
		TArray<float> SegmentLength;
		for (int32 S = 0; S < NumSegments; ++S)
		{
			const FVector3f P0 = Mesh.Positions[Chain.Vertices[S]];
			const FVector3f P1 = Mesh.Positions[Chain.Vertices[(S + 1) % NumPoints]];
			const FVector3f Dir = (P1 - P0).GetSafeNormal();
			SegmentLength.Add(FVector3f::Distance(P0, P1));

			const FCrease& Crease = Creases[Chain.Creases[S]];
			FSide Sides[2];
			const int32 Faces[2] = { Crease.T1, Crease.T2 };
			for (int32 i = 0; i < 2; ++i)
			{
				const FVector3f N = Mesh.FaceNormals[Faces[i]];
				FVector3f Across = FVector3f::CrossProduct(N, Dir).GetSafeNormal();
				// Point it into the face: toward the face's corner that isn't on the crease
				if (FVector3f::DotProduct(Across, Mesh.Positions[OppositeVertex(Mesh, Faces[i], Crease.A, Crease.B)] - P0) < 0.0f)
				{
					Across = -Across;
				}
				Sides[i] = { Across, N };
			}
			// Left = the face whose across direction points more along Up x Dir
			const FVector3f SideAxis = FVector3f::CrossProduct((Sides[0].Normal + Sides[1].Normal).GetSafeNormal(), Dir);
			const bool bFirstIsLeft = FVector3f::DotProduct(Sides[0].Across, SideAxis) >= FVector3f::DotProduct(Sides[1].Across, SideAxis);
			Left.Add(bFirstIsLeft ? Sides[0] : Sides[1]);
			Right.Add(bFirstIsLeft ? Sides[1] : Sides[0]);
		}

		// 5b.
		auto Blend = [](const FSide* A, const FSide* B, FVector3f& OutAcross, FVector3f& OutNormal)
		{
			if (!B)
			{
				OutAcross = A->Across;
				OutNormal = A->Normal;
				return;
			}
			const FVector3f Across = (A->Across + B->Across).GetSafeNormal();
			// Averaging shortens the reach square to each segment; scale back up (capped at 2x on tight bends)
			OutAcross = Across / FMath::Max(FVector3f::DotProduct(Across, A->Across), 0.5f);
			OutNormal = (A->Normal + B->Normal).GetSafeNormal();
		};

		// 5c.
		const int32 First = Vertices.Num();
		const int32 NumEmitted = Chain.bClosed ? NumPoints + 1 : NumPoints;
		float Distance = 0.0f;
		for (int32 K = 0; K < NumEmitted; ++K)
		{
			const int32 Point = K % NumPoints;
			// Segments touching this point: the one arriving (if any) and the one leaving (if any)
			const int32 Arriving = (Point > 0) ? Point - 1 : (Chain.bClosed ? NumSegments - 1 : INDEX_NONE);
			const int32 Leaving = (Point < NumSegments) ? Point : INDEX_NONE;
			const int32 A = Leaving != INDEX_NONE ? Leaving : Arriving;
			const int32 B = (Leaving != INDEX_NONE && Arriving != INDEX_NONE) ? Arriving : INDEX_NONE;

			FVector3f AcrossL, NormalL, AcrossR, NormalR;
			Blend(&Left[A], B != INDEX_NONE ? &Left[B] : nullptr, AcrossL, NormalL);
			Blend(&Right[A], B != INDEX_NONE ? &Right[B] : nullptr, AcrossR, NormalR);
			const FVector3f P = Mesh.Positions[Chain.Vertices[Point]];
			const FVector3f Up = (NormalL + NormalR).GetSafeNormal();
			const float U = Distance / TileLength;

			Vertices.Add({ P + AcrossL * Width + NormalL * Offset, NormalL, FVector2f(U, 0.0f) });
			Vertices.Add({ P + Up * Offset, NormalL, FVector2f(U, 0.5f) });
			Vertices.Add({ P + Up * Offset, NormalR, FVector2f(U, 0.5f) });
			Vertices.Add({ P + AcrossR * Width + NormalR * Offset, NormalR, FVector2f(U, 1.0f) });

			if (K < NumSegments)
			{
				Distance += SegmentLength[K];
			}
		}

		// 5d.
		auto AddTriangle = [&](int32 I0, int32 I1, int32 I2)
		{
			const FVector3f Face = FVector3f::CrossProduct(Vertices[I1].Position - Vertices[I0].Position, Vertices[I2].Position - Vertices[I0].Position) * Mesh.FaceSign;
			if (FVector3f::DotProduct(Face, Vertices[I0].Normal) < 0.0f)
			{
				Swap(I1, I2);
			}
			Indices.Append({ I0, I1, I2 });
		};
		for (int32 S = 0; S < NumSegments; ++S)
		{
			const int32 V0 = First + S * 4;
			const int32 V1 = First + (S + 1) * 4;
			// Left quad: left edge -> crease
			AddTriangle(V0 + 0, V0 + 1, V1 + 1);
			AddTriangle(V0 + 0, V1 + 1, V1 + 0);
			// Right quad: crease -> right edge
			AddTriangle(V0 + 2, V0 + 3, V1 + 3);
			AddTriangle(V0 + 2, V1 + 3, V1 + 2);
		}
	}

	/** Step 6a: the strips as mesh data (one vertex per strip vertex, one polygon group named "Edges"). */
	void MakeMeshDescription(const TArray<FStripVertex>& Vertices, const TArray<int32>& Indices, FMeshDescription& Out)
	{
		FStaticMeshAttributes Attributes(Out);
		Attributes.Register();
		TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		UVs.SetNumChannels(1);

		const FPolygonGroupID Group = Out.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = TEXT("Edges");

		Out.ReserveNewVertices(Vertices.Num());
		Out.ReserveNewVertexInstances(Vertices.Num());
		Out.ReserveNewTriangles(Indices.Num() / 3);
		TArray<FVertexInstanceID> Instances;
		Instances.Reserve(Vertices.Num());
		for (const FStripVertex& Vertex : Vertices)
		{
			const FVertexID VertexID = Out.CreateVertex();
			Positions[VertexID] = Vertex.Position;
			const FVertexInstanceID Instance = Out.CreateVertexInstance(VertexID);
			Normals[Instance] = Vertex.Normal;
			UVs.Set(Instance, 0, Vertex.UV);
			Instances.Add(Instance);
		}
		for (int32 i = 0; i + 2 < Indices.Num(); i += 3)
		{
			const FVertexInstanceID Corners[3] = { Instances[Indices[i]], Instances[Indices[i + 1]], Instances[Indices[i + 2]] };
			Out.CreateTriangle(Group, Corners);
		}
	}

	/**
	 * Step 6b: the mesh to write into. With bOverwrite, an existing Static Mesh at Folder/Name (rebuilt in place, so
	 * actors and Blueprints using it pick up the new strips); otherwise a new asset at the first free name.
	 */
	UStaticMesh* FindOrCreateMesh(const FString& Folder, const FString& Name, bool bOverwrite, FText& OutError)
	{
		const FString CleanFolder = VrrCore::NormalizeContentFolder(Folder);
		if (bOverwrite)
		{
			const FString PackageName = CleanFolder / Name;
			if (FPackageName::DoesPackageExist(PackageName))
			{
				if (UStaticMesh* Existing = LoadObject<UStaticMesh>(nullptr, *(PackageName + TEXT(".") + Name)))
				{
					return Existing;
				}
			}
		}
		FString PackageName, AssetName;
		if (!VrrCore::MakeUniqueAssetPath(CleanFolder, Name, PackageName, AssetName))
		{
			OutError = FText::Format(LOCTEXT("BadPath", "{0}/{1} isn't a valid content path."), FText::FromString(CleanFolder), FText::FromString(Name));
			return nullptr;
		}
		UPackage* Package = CreatePackage(*PackageName);
		UStaticMesh* Mesh = NewObject<UStaticMesh>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Mesh);
		return Mesh;
	}
}

namespace EdgeMeshBuilder
{
	using namespace EdgeMeshImpl;

	FString SuggestName(const UStaticMesh* Mesh)
	{
		return Mesh ? FString::Printf(TEXT("SM_%s_Edges"), *VrrCore::GetBaseAssetName(Mesh)) : FString();
	}

	bool ComputePreview(UStaticMesh* Mesh, const FEdgeMeshSettings& Settings, FEdgeMeshPreview& OutPreview, FText& OutError)
	{
		FAnalysis Analysis;
		if (!Analyze(Mesh, Settings, Analysis, OutError))
		{
			return false;
		}
		OutPreview = FEdgeMeshPreview();
		const FWeldedMesh& Welded = Analysis.Welded;

		// Every mesh edge, for context (capped so huge meshes stay quick to draw)
		constexpr int32 MaxWireEdges = 60000;
		for (const TPair<uint64, TArray<int32, TInlineAllocator<2>>>& Edge : Welded.EdgeTriangles)
		{
			if (OutPreview.Wireframe.Num() / 2 >= MaxWireEdges)
			{
				break;
			}
			int32 A, B;
			SplitEdgeKey(Edge.Key, A, B);
			OutPreview.Wireframe.Append({ Welded.Positions[A], Welded.Positions[B] });
		}
		for (const FVector3f& Position : Welded.Positions)
		{
			OutPreview.Bounds += Position;
		}

		// Creases by chain, kept or dropped, and what the strips would cost
		OutPreview.NumChains = Analysis.Chains.Num();
		for (int32 i = 0; i < Analysis.Chains.Num(); ++i)
		{
			const FChain& Chain = Analysis.Chains[i];
			TArray<FVector3f>& Into = Analysis.Kept[i] ? OutPreview.Kept : OutPreview.Dropped;
			for (int32 S = 0; S < Chain.Creases.Num(); ++S)
			{
				Into.Append({ Welded.Positions[Chain.Vertices[S]], Welded.Positions[Chain.Vertices[(S + 1) % Chain.Vertices.Num()]] });
			}
			if (Analysis.Kept[i])
			{
				++OutPreview.NumKeptChains;
				OutPreview.KeptLength += Chain.Length;
				OutPreview.NumTriangles += Chain.Creases.Num() * 4;
			}
		}
		return true;
	}

	FEdgeMeshResult Build(UStaticMesh* Mesh, const FEdgeMeshSettings& Settings)
	{
		FEdgeMeshResult Result;
		if (!Mesh)
		{
			Result.Message = LOCTEXT("NoMesh", "Pick a Static Mesh first.");
			return Result;
		}
		FScopedSlowTask SlowTask(3.0f, LOCTEXT("Building", "Building edge mesh..."));
		SlowTask.MakeDialog();

		// 1-4. Find the edges to keep
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Finding", "Finding edges..."));
		FAnalysis Analysis;
		if (!Analyze(Mesh, Settings, Analysis, Result.Message))
		{
			return Result;
		}

		// 5. Strips along every kept chain
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Strips", "Building strips..."));
		TArray<FStripVertex> Vertices;
		TArray<int32> Indices;
		int32 NumKept = 0;
		float KeptLength = 0.0f;
		for (int32 i = 0; i < Analysis.Chains.Num(); ++i)
		{
			if (Analysis.Kept[i])
			{
				BuildStrip(Analysis.Welded, Analysis.Creases, Analysis.Chains[i], Settings, Vertices, Indices);
				++NumKept;
				KeptLength += Analysis.Chains[i].Length;
			}
		}
		if (Indices.IsEmpty())
		{
			Result.Message = Analysis.Creases.IsEmpty()
				? LOCTEXT("NoCreases", "No edges are as sharp as Min Angle. Lower Min Angle (or allow inner edges).")
				: LOCTEXT("AllDropped", "Every edge chain is shorter than Min Length. Lower Min Length.");
			return Result;
		}

		// 6. Write the mesh
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Writing", "Writing mesh..."));
		UStaticMesh* Target = FindOrCreateMesh(Settings.OutputFolder, Settings.OutputName, Settings.bOverwrite, Result.Message);
		if (!Target)
		{
			return Result;
		}
		if (Target == Mesh)
		{
			Result.Message = LOCTEXT("SameAsSource", "The output name is the source mesh itself. Pick another name.");
			return Result;
		}

		FMeshDescription Built;
		MakeMeshDescription(Vertices, Indices, Built);

		Target->Modify();
		Target->SetNumSourceModels(1);
		// 6c. Normals are ours (each strip side takes its face's normal); tangents are built from the UVs for normal-mapped decals
		FMeshBuildSettings& BuildSettings = Target->GetSourceModel(0).BuildSettings;
		BuildSettings.bRecomputeNormals = false;
		BuildSettings.bRecomputeTangents = true;
		BuildSettings.bUseMikkTSpace = true;
		BuildSettings.bGenerateLightmapUVs = false;
		BuildSettings.bRemoveDegenerates = true;
		FMeshDescription* Description = Target->CreateMeshDescription(0);
		*Description = MoveTemp(Built);
		Target->CommitMeshDescription(0);

		// 6d. One slot with the decal material, no lightmap (edges are decoration, not lit geometry)
		TArray<FStaticMaterial>& Materials = Target->GetStaticMaterials();
		Materials.Reset();
		Materials.Add(FStaticMaterial(Settings.Material.Get(), TEXT("Edges"), TEXT("Edges")));
		Target->SetLightMapCoordinateIndex(0);
		Target->SetLightMapResolution(4);
		Target->PostEditChange();
		Target->MarkPackageDirty();

		Result.bSuccess = true;
		Result.Mesh = Target;
		Result.Message = FText::Format(LOCTEXT("Done", "Built {0}: {1} {1}|plural(one=edge,other=edges), {2} m of strips, {3} triangles. Add it at the same transform as {4} (e.g. as a child component)."),
			FText::FromString(Target->GetName()), NumKept, FText::AsNumber(KeptLength / 100.0f), Indices.Num() / 3, FText::FromString(Mesh->GetName()));
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
