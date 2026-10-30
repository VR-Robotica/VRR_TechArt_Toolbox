// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Mesh map baking: Cavity, Edge, Curvature and Ambient Occlusion masks computed from the mesh itself (no high-poly source).
 * Materials use them for toon details: dark crevices, bright worn edges, rim tints on convex areas.
 *
 * Process
 *  1. Weld the mesh by position (BuildWeldedMesh) so hard edges and UV seams don't cut the surface into pieces.
 *     That gives one smooth normal per position, a neighbor list, and the triangles that share each edge.
 *  2. Compute each map per welded vertex (cheap, and smooth across UV seams):
 *       Curvature - how fast the normal turns along the surface (0.5 = flat, brighter = convex).
 *       Cavity    - how far a vertex sits below a smoothed copy of the surface (dark = recessed).
 *     Edge is different: it comes from creases (sharp angles between faces) and is measured per texel
 *     as the distance to the nearest crease, so the line width doesn't depend on the vertex density.
 *     Ambient Occlusion is also per texel: rays cast over the hemisphere above it against a BVH of the mesh.
 *  3. Rasterize every triangle in UV space, blending the vertex values with barycentric weights.
 *  4. Trace AO for every covered texel (in parallel), then smooth it within connected surface.
 *  5. Pad the UV islands outward so filtering and mips at island borders don't pull in background.
 *  6. Write one packed mask (_CCE: R Curvature, G Cavity, B Edge; _CCEA adds A AO) or one grayscale texture per map.
 *
 * With Split by Shading Model, steps 3-6 run once per shading model of the mesh's materials, each drawing only the
 * triangles of its slots: materials whose UVs overlap each other get separate textures (<Base>_DefaultLit_CCE...).
 * The analysis (step 1-2) stays whole-mesh, so AO is still blocked by every part and curvature doesn't break at slot borders.
 */

#include "Core_BakeMaps.h"

#include "Async/ParallelFor.h"
#include "Core_Common.h"
#include "Core_ConsolidateMaterials.h"
#include "Core_MeshWeld.h"
#include "Engine/Texture2D.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"

#include <algorithm>

#define LOCTEXT_NAMESPACE "MeshMapBaker"

namespace BakeMapsImpl
{
	// Welding lives in Core_MeshWeld (shared with the Edge Mesh tool)
	using VrrMeshWeld::FWeldedMesh;
	using VrrMeshWeld::EdgeKey;
	using VrrMeshWeld::SplitEdgeKey;
	using VrrMeshWeld::BuildWeldedMesh;

	/** Averages each value with its neighbors. */
	void Blur(const FWeldedMesh& Mesh, TArray<float>& Values, int32 Passes)
	{
		TArray<float> Next;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			Next = Values;
			for (int32 Index = 0; Index < Values.Num(); ++Index)
			{
				const TArray<int32>& Neighbors = Mesh.Neighbors[Index];
				float Sum = Values[Index];
				for (const int32 Neighbor : Neighbors)
				{
					Sum += Values[Neighbor];
				}
				Next[Index] = Sum / (Neighbors.Num() + 1);
			}
			Swap(Values, Next);
		}
	}

	/**
	 * Signed mean curvature per vertex. Along each edge the smooth normal turns by some angle; length / angle is the
	 * radius of curvature. Normals spreading apart = convex (bright), closing in = concave (dark). 0.5 = flat.
	 */
	TArray<float> ComputeCurvature(const FWeldedMesh& Mesh, float FullRadius, int32 BlurPasses)
	{
		const int32 Num = Mesh.Positions.Num();
		TArray<float> SumAngle;
		TArray<float> SumLength;
		SumAngle.SetNumZeroed(Num);
		SumLength.SetNumZeroed(Num);

		for (const TPair<uint64, TArray<int32, TInlineAllocator<2>>>& MeshEdge : Mesh.EdgeTriangles)
		{
			int32 A, B;
			SplitEdgeKey(MeshEdge.Key, A, B);
			const FVector3f& NA = Mesh.Normals[A];
			const FVector3f& NB = Mesh.Normals[B];
			if (NA.IsNearlyZero() || NB.IsNearlyZero())
			{
				continue;
			}
			const FVector3f Delta = Mesh.Positions[B] - Mesh.Positions[A];
			const float Angle = FMath::Acos(FMath::Clamp(FVector3f::DotProduct(NA, NB), -1.0f, 1.0f));
			// Normals that spread apart along the edge = convex (+), that close in = concave (-)
			const float Sign = FVector3f::DotProduct(NB - NA, Delta) >= 0.0f ? 1.0f : -1.0f;
			const float Length = Delta.Size();
			SumAngle[A] += Angle * Sign;
			SumAngle[B] += Angle * Sign;
			SumLength[A] += Length;
			SumLength[B] += Length;
		}

		const float Radius = FMath::Max(FullRadius, 0.001f);
		TArray<float> Values;
		Values.Init(0.5f, Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			if (SumLength[Index] > UE_SMALL_NUMBER)
			{
				const float Curvature = SumAngle[Index] / SumLength[Index]; // 1 / radius
				// Map to 0..1: a bend of radius FullRadius (or tighter) reaches full black/white
				Values[Index] = 0.5f + 0.5f * FMath::Clamp(Curvature * Radius, -1.0f, 1.0f);
			}
		}
		Blur(Mesh, Values, BlurPasses);
		return Values;
	}

	/**
	 * Cavity per vertex: how far the surface sits below a smoothed copy of itself, along the normal.
	 * Taubin smoothing (alternating shrink/inflate) so the smoothed copy doesn't shrink and turn everything convex.
	 * 1 = flat or raised, 0 = at least Depth deep.
	 */
	TArray<float> ComputeCavity(const FWeldedMesh& Mesh, int32 Spread, float Depth)
	{
		const int32 Num = Mesh.Positions.Num();
		TArray<FVector3f> Smoothed = Mesh.Positions;
		TArray<FVector3f> Next;
		Next.SetNumUninitialized(Num);

		// 1. Smoothed copy of the surface: each pass shrinks toward the neighbor average (0.5) then inflates back (-0.53)
		for (int32 Iteration = 0; Iteration < Spread; ++Iteration)
		{
			for (const float Factor : { 0.5f, -0.53f })
			{
				for (int32 Index = 0; Index < Num; ++Index)
				{
					const TArray<int32>& Neighbors = Mesh.Neighbors[Index];
					if (Neighbors.IsEmpty())
					{
						Next[Index] = Smoothed[Index];
						continue;
					}
					FVector3f Average = FVector3f::ZeroVector;
					for (const int32 Neighbor : Neighbors)
					{
						Average += Smoothed[Neighbor];
					}
					Average /= static_cast<float>(Neighbors.Num());
					Next[Index] = Smoothed[Index] + (Average - Smoothed[Index]) * Factor;
				}
				Swap(Smoothed, Next);
			}
		}

		// 2. Height of each vertex above the smoothed surface, along the normal. Below = cavity
		const float SafeDepth = FMath::Max(Depth, 0.0001f);
		TArray<float> Values;
		Values.Init(1.0f, Num);
		for (int32 Index = 0; Index < Num; ++Index)
		{
			const float Height = FVector3f::DotProduct(Mesh.Normals[Index], Mesh.Positions[Index] - Smoothed[Index]);
			Values[Index] = 1.0f - FMath::Clamp(-Height / SafeDepth, 0.0f, 1.0f);
		}
		Blur(Mesh, Values, 1);
		return Values;
	}

	/** A crease as a 3D segment with its strength (0..1). */
	struct FSharpEdge
	{
		FVector3f A;
		FVector3f B;
		float Strength;
	};

	/** Creases between two faces, with 0..1 strength from the angle between them. */
	void FindSharpEdges(const FWeldedMesh& Mesh, float MinAngle, float FullAngle, bool bConcave, TArray<FSharpEdge>& OutEdges, TArray<TArray<int32>>& OutEdgesOfVertex)
	{
		OutEdgesOfVertex.SetNum(Mesh.Positions.Num());

		for (const TPair<uint64, TArray<int32, TInlineAllocator<2>>>& MeshEdge : Mesh.EdgeTriangles)
		{
			if (MeshEdge.Value.Num() != 2)
			{
				continue; // open border or non-manifold
			}
			int32 A, B;
			SplitEdgeKey(MeshEdge.Key, A, B);

			const int32 T1 = MeshEdge.Value[0];
			const int32 T2 = MeshEdge.Value[1];
			const float Angle = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector3f::DotProduct(Mesh.FaceNormals[T1], Mesh.FaceNormals[T2]), -1.0f, 1.0f)));

			// Strength ramps from 0 at MinAngle to 1 at FullAngle
			const float Strength = FullAngle > MinAngle
				? FMath::Clamp((Angle - MinAngle) / (FullAngle - MinAngle), 0.0f, 1.0f)
				: (Angle >= MinAngle ? 1.0f : 0.0f);
			if (Strength <= 0.0f)
			{
				continue;
			}

			// Convex if the far corner of the second face sits below the first face's plane
			const FIntVector& Other = Mesh.Triangles[T2];
			const int32 Opposite = (Other.X != A && Other.X != B) ? Other.X : (Other.Y != A && Other.Y != B) ? Other.Y : Other.Z;
			const bool bConvex = FVector3f::DotProduct(Mesh.FaceNormals[T1], Mesh.Positions[Opposite] - Mesh.Positions[A]) < 0.0f;
			if (!bConvex && !bConcave)
			{
				continue;
			}

			const int32 Index = OutEdges.Add({ Mesh.Positions[A], Mesh.Positions[B], Strength });
			OutEdgesOfVertex[A].Add(Index);
			OutEdgesOfVertex[B].Add(Index);
		}
	}

	/** Shortest distance from a point to segment AB. */
	float DistanceToSegment(const FVector3f& Point, const FVector3f& A, const FVector3f& B)
	{
		const FVector3f AB = B - A;
		const float LengthSq = AB.SizeSquared();
		const float T = LengthSq > UE_SMALL_NUMBER ? FMath::Clamp(FVector3f::DotProduct(Point - A, AB) / LengthSq, 0.0f, 1.0f) : 0.0f;
		return FVector3f::Dist(Point, A + AB * T);
	}

	/**
	 * Bounding volume hierarchy over the welded triangles, for the AO rays.
	 * Built by median split on the longest axis, so its depth stays near log2(triangles).
	 */
	class FTriangleBVH
	{
	public:
		void Build(const FWeldedMesh& Mesh)
		{
			const int32 NumTriangles = Mesh.Triangles.Num();
			TArray<int32> Order;
			TArray<FVector3f> Centroids;
			Order.SetNumUninitialized(NumTriangles);
			Centroids.SetNumUninitialized(NumTriangles);
			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
			{
				const FIntVector& Tri = Mesh.Triangles[Triangle];
				Order[Triangle] = Triangle;
				Centroids[Triangle] = (Mesh.Positions[Tri.X] + Mesh.Positions[Tri.Y] + Mesh.Positions[Tri.Z]) / 3.0f;
			}

			Nodes.Reset();
			Nodes.Reserve(FMath::Max(1, NumTriangles * 2 / LeafSize + 1));
			Nodes.AddDefaulted();
			// (node, first, count) still to split
			TArray<FIntVector, TInlineAllocator<64>> Pending;
			Pending.Add(FIntVector(0, 0, NumTriangles));
			while (!Pending.IsEmpty())
			{
				const FIntVector Work = Pending.Pop(EAllowShrinking::No);
				const int32 First = Work.Y;
				const int32 Count = Work.Z;

				FVector3f Min(UE_BIG_NUMBER), Max(-UE_BIG_NUMBER), CentroidMin(UE_BIG_NUMBER), CentroidMax(-UE_BIG_NUMBER);
				for (int32 i = First; i < First + Count; ++i)
				{
					const FIntVector& Tri = Mesh.Triangles[Order[i]];
					for (const int32 Corner : { Tri.X, Tri.Y, Tri.Z })
					{
						Min = Min.ComponentMin(Mesh.Positions[Corner]);
						Max = Max.ComponentMax(Mesh.Positions[Corner]);
					}
					CentroidMin = CentroidMin.ComponentMin(Centroids[Order[i]]);
					CentroidMax = CentroidMax.ComponentMax(Centroids[Order[i]]);
				}
				Nodes[Work.X].Min = Min;
				Nodes[Work.X].Max = Max;

				const FVector3f Extent = CentroidMax - CentroidMin;
				if (Count <= LeafSize || Extent.GetMax() <= UE_SMALL_NUMBER)
				{
					Nodes[Work.X].First = First;
					Nodes[Work.X].Count = Count;
					continue;
				}

				// Split at the median centroid along the longest axis; children are stored next to each other
				const int32 Axis = Extent.X >= Extent.Y && Extent.X >= Extent.Z ? 0 : (Extent.Y >= Extent.Z ? 1 : 2);
				const int32 Half = Count / 2;
				int32* Begin = Order.GetData() + First;
				std::nth_element(Begin, Begin + Half, Begin + Count, [&Centroids, Axis](int32 A, int32 B) { return Centroids[A][Axis] < Centroids[B][Axis]; });

				const int32 Left = Nodes.AddDefaulted(2);
				Nodes[Work.X].First = Left;
				Nodes[Work.X].Count = 0;
				Pending.Add(FIntVector(Left, First, Half));
				Pending.Add(FIntVector(Left + 1, First + Half, Count - Half));
			}

			// Triangle data in leaf order, ready for the ray test
			Triangles.SetNumUninitialized(NumTriangles);
			for (int32 i = 0; i < NumTriangles; ++i)
			{
				const FIntVector& Tri = Mesh.Triangles[Order[i]];
				const FVector3f& P0 = Mesh.Positions[Tri.X];
				Triangles[i] = { P0, Mesh.Positions[Tri.Y] - P0, Mesh.Positions[Tri.Z] - P0, Mesh.FaceNormals[Order[i]] };
			}
		}

		/** Distance to the nearest hit within MaxDistance, or -1 when the ray escapes. */
		float Trace(const FVector3f& Origin, const FVector3f& Direction, float MaxDistance, bool bIgnoreBackFaces) const
		{
			if (Nodes.IsEmpty())
			{
				return -1.0f;
			}
			const FVector3f InvDirection(SafeInverse(Direction.X), SafeInverse(Direction.Y), SafeInverse(Direction.Z));
			float Nearest = MaxDistance;
			bool bHit = false;

			int32 Stack[64];
			int32 StackSize = 0;
			Stack[StackSize++] = 0;
			while (StackSize > 0)
			{
				const FNode& Node = Nodes[Stack[--StackSize]];
				if (!HitsBox(Node, Origin, InvDirection, Nearest))
				{
					continue;
				}
				if (Node.Count == 0)
				{
					Stack[StackSize++] = Node.First;
					Stack[StackSize++] = Node.First + 1;
					continue;
				}
				for (int32 i = Node.First; i < Node.First + Node.Count; ++i)
				{
					const FTriangle& Tri = Triangles[i];
					if (bIgnoreBackFaces && FVector3f::DotProduct(Direction, Tri.Normal) > 0.0f)
					{
						continue;
					}
					// Moller-Trumbore
					const FVector3f P = FVector3f::CrossProduct(Direction, Tri.E2);
					const float Det = FVector3f::DotProduct(Tri.E1, P);
					if (FMath::Abs(Det) < 1e-12f)
					{
						continue;
					}
					const float InvDet = 1.0f / Det;
					const FVector3f T = Origin - Tri.V0;
					const float U = FVector3f::DotProduct(T, P) * InvDet;
					if (U < 0.0f || U > 1.0f)
					{
						continue;
					}
					const FVector3f Q = FVector3f::CrossProduct(T, Tri.E1);
					const float V = FVector3f::DotProduct(Direction, Q) * InvDet;
					if (V < 0.0f || U + V > 1.0f)
					{
						continue;
					}
					const float Distance = FVector3f::DotProduct(Tri.E2, Q) * InvDet;
					if (Distance > 1e-5f && Distance < Nearest)
					{
						Nearest = Distance;
						bHit = true;
					}
				}
			}
			return bHit ? Nearest : -1.0f;
		}

	private:
		static constexpr int32 LeafSize = 4;

		/** Interior node: Count 0, children at First and First + 1. Leaf: Triangles[First, First + Count). */
		struct FNode
		{
			FVector3f Min = FVector3f::ZeroVector;
			FVector3f Max = FVector3f::ZeroVector;
			int32 First = 0;
			int32 Count = 0;
		};
		struct FTriangle
		{
			FVector3f V0, E1, E2, Normal;
		};
		TArray<FNode> Nodes;
		TArray<FTriangle> Triangles;

		static float SafeInverse(float Value)
		{
			return FMath::Abs(Value) > 1e-12f ? 1.0f / Value : (Value >= 0.0f ? UE_BIG_NUMBER : -UE_BIG_NUMBER);
		}

		/** Slab test: does the ray reach the box before MaxDistance? */
		static bool HitsBox(const FNode& Node, const FVector3f& Origin, const FVector3f& InvDirection, float MaxDistance)
		{
			float Near = 0.0f;
			float Far = MaxDistance;
			for (int32 Axis = 0; Axis < 3; ++Axis)
			{
				float T0 = (Node.Min[Axis] - Origin[Axis]) * InvDirection[Axis];
				float T1 = (Node.Max[Axis] - Origin[Axis]) * InvDirection[Axis];
				if (T0 > T1)
				{
					Swap(T0, T1);
				}
				Near = FMath::Max(Near, T0);
				Far = FMath::Min(Far, T1);
				if (Near > Far)
				{
					return false;
				}
			}
			return true;
		}
	};

	/** Do two welded triangles touch (same triangle or a shared corner)? */
	bool TrianglesTouch(const FWeldedMesh& Mesh, int32 A, int32 B)
	{
		if (A == B)
		{
			return true;
		}
		const FIntVector& TA = Mesh.Triangles[A];
		const FIntVector& TB = Mesh.Triangles[B];
		for (const int32 Corner : { TA.X, TA.Y, TA.Z })
		{
			if (Corner == TB.X || Corner == TB.Y || Corner == TB.Z)
			{
				return true;
			}
		}
		return false;
	}

	/**
	 * Cosine-weighted hemisphere directions around +Z (Fibonacci spiral): evenly spread, so the plain fraction of
	 * blocked rays is the occlusion.
	 */
	TArray<FVector3f> MakeHemisphereRays(int32 Count)
	{
		TArray<FVector3f> Rays;
		Rays.SetNumUninitialized(Count);
		const float GoldenAngle = UE_PI * (3.0f - FMath::Sqrt(5.0f));
		for (int32 i = 0; i < Count; ++i)
		{
			const float U = (i + 0.5f) / Count;
			const float Radius = FMath::Sqrt(U);
			const float Phi = i * GoldenAngle;
			Rays[i] = FVector3f(Radius * FMath::Cos(Phi), Radius * FMath::Sin(Phi), FMath::Sqrt(1.0f - U));
		}
		return Rays;
	}

	/** "m:ss" for a duration in seconds. */
	FText FormatDuration(double Seconds)
	{
		const int32 Total = FMath::Max(FMath::RoundToInt32(Seconds), 0);
		return FText::FromString(FString::Printf(TEXT("%d:%02d"), Total / 60, Total % 60));
	}

	/** Time since a stage started, for progress text like "Tracing ambient occlusion: 42%  (0:35 elapsed, about 0:48 left)". */
	struct FBakeTimer
	{
		double Start = FPlatformTime::Seconds();

		double Elapsed() const { return FPlatformTime::Seconds() - Start; }

		FText Describe(const FText& Stage, float Fraction) const
		{
			const double Seconds = Elapsed();
			const FText Percent = FText::AsPercent(Fraction);
			// The estimate is noise until a few percent is done
			if (Fraction < 0.02f || Seconds < 1.0)
			{
				return FText::Format(LOCTEXT("ProgressStart", "{0}: {1}  ({2} elapsed)"), Stage, Percent, FormatDuration(Seconds));
			}
			const double Left = Seconds * (1.0 - Fraction) / Fraction;
			return FText::Format(LOCTEXT("ProgressEta", "{0}: {1}  ({2} elapsed, about {3} left)"), Stage, Percent, FormatDuration(Seconds), FormatDuration(Left));
		}
	};

	using VrrCore::EdgeFunction;
	inline uint8 ToByte(float Value) { return VrrCore::ToByte01(Value); }

	/** Index of each map in the per-map arrays below. */
	enum EMap : int32 { Cavity, Edge, Curvature, AmbientOcclusion, NumMaps };

	/** One bake: the settings plus the mesh they apply to. */
	class FJob : public FMeshMapBakeSettings
	{
	public:
		FJob(UObject* InMesh, const FMeshMapBakeSettings& InSettings)
			: FMeshMapBakeSettings(InSettings), Mesh(InMesh) {}

		bool Bake(FMeshMapBakeResult& OutResult);

	private:
		TWeakObjectPtr<UObject> Mesh;

		/**
		 * Creates (or with bOverwrite, updates) a linear texture. Pixels are G8 (one byte per texel) or, with bPacked, BGRA8; bWithAlpha keeps the alpha channel.
		 * Returns null on failure.
		 */
		UTexture2D* WriteTexture(const FString& BaseName, const FString& Suffix, int32 Size, const TArray<uint8>& Pixels, bool bPacked, bool bWithAlpha, FText& OutError) const;

		/** Fills OutValues (one per texel) with AO for every texel a triangle covers. False if the user cancelled. */
		bool TraceAmbientOcclusion(const FWeldedMesh& Welded, const FTriangleBVH& BVH, const TArray<int32>& Owner, const TArray<FVector2f>& Barycentrics,
			int32 Size, TArray<float>& OutValues) const;
	};

	/**
	 * Saves one baked map:
	 *  1. get the texture asset (VrrCore::FindOrCreateTexture: with Overwrite on, the existing one of that name so
	 *     materials keep their reference, otherwise a new asset at the first free name),
	 *  2. fill it with the pixels as linear data.
	 */
	UTexture2D* FJob::WriteTexture(const FString& BaseName, const FString& Suffix, int32 Size, const TArray<uint8>& Pixels, bool bPacked, bool bWithAlpha, FText& OutError) const
	{
		// 1. The asset (an empty base name would leave just the suffix, so reject it here)
		if (OutputBaseName.IsEmpty())
		{
			OutError = LOCTEXT("NoBaseName", "Enter an output name first.");
			return nullptr;
		}
		UTexture2D* Texture = VrrCore::FindOrCreateTexture(OutputFolder, BaseName + Suffix, bOverwrite, OutError);
		if (!Texture)
		{
			return nullptr;
		}

		// 2. The pixels

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Size, Size, 1, 1, bPacked ? TSF_BGRA8 : TSF_G8, Pixels.GetData());

		// Linear data: a channel-packed mask, or a single grayscale channel
		Texture->SRGB = false;
		Texture->CompressionSettings = bPacked ? TC_Masks : TC_Grayscale;
		// RGB-only mask: drop alpha so it compresses to DXT1/BC1 (half the memory of DXT5/BC3).
		// Set either way, so overwriting a texture that was baked the other way doesn't keep the old setting.
		Texture->CompressionNoAlpha = bPacked && !bWithAlpha;
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}

	/**
	 * Ambient occlusion:
	 *  1. per texel, rebuild the surface point and normal from its triangle and barycentric weights,
	 *  2. cast AORays cosine-weighted rays over the hemisphere (spun per texel so the pattern becomes fine noise
	 *     instead of banding); each blocked ray adds occlusion, less for far hits with distance falloff,
	 *  3. smooth with AOBlur 3x3 passes, only between texels on touching triangles so separate UV islands don't mix.
	 * Runs in chunks across all cores, updating the progress bar (with time left) and checking Cancel between chunks.
	 */
	bool FJob::TraceAmbientOcclusion(const FWeldedMesh& Welded, const FTriangleBVH& BVH, const TArray<int32>& Owner, const TArray<FVector2f>& Barycentrics,
		int32 Size, TArray<float>& OutValues) const
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh.Get(), 0);
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexInstanceAttributesConstRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();

		const int32 NumRays = FMath::Max(AORays, 1);
		const TArray<FVector3f> Rays = MakeHemisphereRays(NumRays);
		const float MaxDistance = FMath::Max(AODistance, 0.001f);
		const float Strength = FMath::Max(AOStrength, 0.0f);

		auto TraceTexel = [&](int32 Index) -> float
		{
			const int32 Triangle = Owner[Index];
			const FIntVector& Tri = Welded.Triangles[Triangle];
			const float W0 = Barycentrics[Index].X;
			const float W1 = Barycentrics[Index].Y;
			const float W2 = 1.0f - W0 - W1;

			// 1. Surface point, face normal, and the shading normal (keeps hard edges, unlike the welded normals)
			const FVector3f Position = Welded.Positions[Tri.X] * W0 + Welded.Positions[Tri.Y] * W1 + Welded.Positions[Tri.Z] * W2;
			const FVector3f& FaceNormal = Welded.FaceNormals[Triangle];
			FVector3f Normal = (InstanceNormals[Welded.Corners[Triangle * 3 + 0]] * W0
				+ InstanceNormals[Welded.Corners[Triangle * 3 + 1]] * W1
				+ InstanceNormals[Welded.Corners[Triangle * 3 + 2]] * W2).GetSafeNormal();
			if (FVector3f::DotProduct(Normal, FaceNormal) <= 0.0f)
			{
				Normal = FaceNormal;
			}

			// Tangent frame, spun by a per-texel angle (integer hash of the texel index)
			FVector3f Tangent, Bitangent;
			Normal.FindBestAxisVectors(Tangent, Bitangent);
			const float Spin = static_cast<float>((static_cast<uint32>(Index) * 2654435761u) >> 8) * (UE_TWO_PI / 16777216.0f);
			const float Cos = FMath::Cos(Spin);
			const float Sin = FMath::Sin(Spin);
			const FVector3f T = Tangent * Cos + Bitangent * Sin;
			const FVector3f B = Bitangent * Cos - Tangent * Sin;

			// 2. Rays from just above the face
			const FVector3f Origin = Position + FaceNormal * AOBias;
			float Occlusion = 0.0f;
			for (const FVector3f& Local : Rays)
			{
				FVector3f Direction = T * Local.X + B * Local.Y + Normal * Local.Z;
				// A ray the smooth normal tilts below the face would go straight into the mesh: mirror it back up
				const float AboveFace = FVector3f::DotProduct(Direction, FaceNormal);
				if (AboveFace < 0.01f)
				{
					Direction = (Direction - FaceNormal * (2.0f * AboveFace - 0.01f)).GetSafeNormal();
				}
				const float Hit = BVH.Trace(Origin, Direction, MaxDistance, bAOIgnoreBackFaces);
				if (Hit >= 0.0f)
				{
					Occlusion += bAODistanceFalloff ? 1.0f - Hit / MaxDistance : 1.0f;
				}
			}
			const float Visibility = FMath::Clamp(1.0f - Occlusion / NumRays, 0.0f, 1.0f);
			return FMath::Pow(Visibility, Strength);
		};

		// Only covered texels cost anything, so progress is counted over those (empty rows would make the bar jump)
		TArray<int32> Covered;
		Covered.Reserve(Owner.Num());
		for (int32 Index = 0; Index < Owner.Num(); ++Index)
		{
			if (Owner[Index] != INDEX_NONE)
			{
				Covered.Add(Index);
			}
		}

		// Chunks: progress, time estimate and Cancel between them, all cores within each
		constexpr int32 NumChunks = 200;
		const int32 ChunkSize = FMath::Max(FMath::DivideAndRoundUp(Covered.Num(), NumChunks), 1);
		FScopedSlowTask AOTask(static_cast<float>(NumChunks), LOCTEXT("TracingAO", "Tracing ambient occlusion..."));
		FBakeTimer Timer;
		for (int32 First = 0; First < Covered.Num(); First += ChunkSize)
		{
			if (AOTask.ShouldCancel())
			{
				return false;
			}
			const int32 Count = FMath::Min(ChunkSize, Covered.Num() - First);
			AOTask.EnterProgressFrame(static_cast<float>(NumChunks) * Count / Covered.Num(),
				Timer.Describe(LOCTEXT("TracingAOStep", "Tracing ambient occlusion"), static_cast<float>(First) / Covered.Num()));
			ParallelFor(Count, [&](int32 Local)
			{
				const int32 Index = Covered[First + Local];
				OutValues[Index] = TraceTexel(Index);
			});
		}

		// 3. Smooth the per-texel noise, only across touching triangles
		TArray<float> Next;
		for (int32 Pass = 0; Pass < AOBlur; ++Pass)
		{
			Next = OutValues;
			ParallelFor(Size, [&](int32 Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const int32 Index = Y * Size + X;
					const int32 Triangle = Owner[Index];
					if (Triangle == INDEX_NONE)
					{
						continue;
					}
					float Sum = 0.0f;
					int32 Count = 0;
					for (int32 NY = FMath::Max(Y - 1, 0); NY <= FMath::Min(Y + 1, Size - 1); ++NY)
					{
						for (int32 NX = FMath::Max(X - 1, 0); NX <= FMath::Min(X + 1, Size - 1); ++NX)
						{
							const int32 Neighbor = NY * Size + NX;
							if (Owner[Neighbor] != INDEX_NONE && TrianglesTouch(Welded, Triangle, Owner[Neighbor]))
							{
								Sum += OutValues[Neighbor];
								++Count;
							}
						}
					}
					Next[Index] = Sum / Count; // Count >= 1: the texel itself
				}
			});
			Swap(OutValues, Next);
		}
		return true;
	}

	/**
	 * The whole bake: validate inputs, then 1. analyze the mesh, 2. rasterize, 3. trace AO, 4. pad, 5. write textures, 6. report.
	 * Unchecked maps cost nothing: their arrays stay empty and are skipped.
	 */
	bool FJob::Bake(FMeshMapBakeResult& OutResult)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh.Get(), 0);
		if (!MeshDescription)
		{
			OutResult.Message = LOCTEXT("NoMeshData", "Pick a Static or Skeletal Mesh with source mesh data for LOD 0.");
			return false;
		}
		if (!HasAnyMap())
		{
			OutResult.Message = LOCTEXT("NoMaps", "Check at least one map to bake.");
			return false;
		}

		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		if (UVChannel >= UVs.GetNumChannels())
		{
			OutResult.Message = FText::Format(LOCTEXT("NoUVChannel", "The mesh has no UV channel {0}."), UVChannel);
			return false;
		}

		const bool bMap[NumMaps] = { bBakeCavity, bBakeEdge, bBakeCurvature, bBakeAO };
		// Neutral value per map where nothing is baked: no cavity (white), no edge (black), flat (gray), unoccluded (white)
		const float Background[NumMaps] = { 1.0f, 0.0f, 0.5f, 1.0f };
		const TCHAR* Suffix[NumMaps] = { TEXT("_Cavity"), TEXT("_Edge"), TEXT("_Curvature"), TEXT("_AO") };

		// The texture sets: one for the whole mesh, or one per shading model with the polygon groups (sections) it draws
		struct FTextureSet
		{
			FString BaseName;
			FText Label;
			TSet<int32> PolygonGroups;
			int32 NumConflicts = 0;
		};
		TArray<FTextureSet> Sets;
		if (bSplitByShadingModel)
		{
			for (const FConsolidateGroup& Group : MaterialConsolidator::GroupByShadingModel(MaterialConsolidator::GetSlots(Mesh.Get())))
			{
				FTextureSet& Set = Sets.AddDefaulted_GetRef();
				Set.BaseName = OutputBaseName + TEXT("_") + Group.Tag;
				Set.Label = Group.Label;
				for (const FConsolidateSlot& Slot : Group.Slots)
				{
					Set.PolygonGroups.Append(Slot.PolygonGroups);
				}
			}
		}
		if (Sets.IsEmpty())
		{
			Sets.AddDefaulted_GetRef().BaseName = OutputBaseName;
		}
		const bool bSplitSets = bSplitByShadingModel && !Sets[0].PolygonGroups.IsEmpty();

		// Progress dialog with a Cancel button. AO is the slow part, so it gets most of the bar
		constexpr float AOWork = 6.0f;
		const FBakeTimer TotalTimer;
		FScopedSlowTask SlowTask(1.0f + Sets.Num() * (4.0f + (bBakeAO ? AOWork : 0.0f)), LOCTEXT("BakingMaps", "Baking mesh maps..."));
		SlowTask.MakeDialog(true);
		const FText CancelledMessage = LOCTEXT("Cancelled", "Bake cancelled. No textures were changed.");

		// 1. Mesh analysis
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Analyzing", "Analyzing mesh..."));
		FWeldedMesh Welded;
		BuildWeldedMesh(*MeshDescription, WeldDistance, Welded);

		TArray<float> VertexValues[NumMaps];
		if (bBakeCavity)
		{
			VertexValues[Cavity] = ComputeCavity(Welded, CavitySpread, CavityDepth);
		}
		if (bBakeCurvature)
		{
			VertexValues[Curvature] = ComputeCurvature(Welded, CurvatureRadius, CurvatureBlur);
		}
		TArray<FSharpEdge> SharpEdges;
		TArray<TArray<int32>> SharpEdgesOfVertex;
		if (bBakeEdge)
		{
			FindSharpEdges(Welded, EdgeMinAngle, EdgeFullAngle, bEdgeConcave, SharpEdges, SharpEdgesOfVertex);
		}
		FTriangleBVH BVH;
		if (bBakeAO)
		{
			BVH.Build(Welded);
		}

		// Steps 2-5 per texture set; the whole mesh's analysis above is shared
		const int32 Size = Resolution;
		const int32 NumTexels = Size * Size;
		const int32 NumTriangles = Welded.Triangles.Num();
		int32 NumOutOfRange = 0;
		TArray<FString> Names;
		for (FTextureSet& Set : Sets)
		{
			// 2. Rasterize every triangle of the set in UV space
			SlowTask.EnterProgressFrame(2.0f, bSplitSets ? FText::Format(LOCTEXT("RasterizingSet", "{0}: rasterizing..."), Set.Label) : LOCTEXT("Rasterizing", "Rasterizing..."));
			TArray<float> Texels[NumMaps];
			for (int32 Map = 0; Map < NumMaps; ++Map)
			{
				if (bMap[Map])
				{
					Texels[Map].Init(Background[Map], NumTexels);
				}
			}
			TArray<int32> Owner;
			// Which triangle last wrote each texel: marks filled texels and detects overlapping UVs
			Owner.Init(INDEX_NONE, NumTexels);
			int32& NumConflicts = Set.NumConflicts;
			const float SafeEdgeWidth = FMath::Max(EdgeWidth, 0.0001f);
			TArray<int32> CandidateEdges;
			// AO is traced after rasterizing, from the owning triangle and these barycentric weights (W0, W1)
			TArray<FVector2f> Barycentrics;
			if (bBakeAO)
			{
				Barycentrics.SetNumUninitialized(NumTexels);
			}

			// Progress (and Cancel) every 1% of the triangles
			constexpr int32 RasterSteps = 100;
			const int32 TrianglesPerStep = FMath::Max(FMath::DivideAndRoundUp(NumTriangles, RasterSteps), 1);
			FScopedSlowTask RasterTask(static_cast<float>(FMath::DivideAndRoundUp(NumTriangles, TrianglesPerStep)), LOCTEXT("Rasterizing", "Rasterizing..."));
			const FBakeTimer RasterTimer;

			for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
			{
				if (Triangle % TrianglesPerStep == 0)
				{
					if (RasterTask.ShouldCancel())
					{
						OutResult.Message = CancelledMessage;
						return false;
					}
					RasterTask.EnterProgressFrame(1.0f, RasterTimer.Describe(LOCTEXT("RasterizingStep", "Rasterizing"), static_cast<float>(Triangle) / NumTriangles));
				}
				// Only the set's own triangles: another shading model's triangles may overlap these in UV space
				if (bSplitSets && !Set.PolygonGroups.Contains(Welded.PolygonGroups[Triangle]))
				{
					continue;
				}

				const FIntVector& Tri = Welded.Triangles[Triangle];
				const int32 C[3] = { Tri.X, Tri.Y, Tri.Z };

				FVector2f Pixel[3];
				bool bOutOfRange = false;
				for (int32 i = 0; i < 3; ++i)
				{
					// UV corners in texel units
					const FVector2f UV = UVs.Get(Welded.Corners[Triangle * 3 + i], UVChannel);
					bOutOfRange |= UV.X < -0.001f || UV.X > 1.001f || UV.Y < -0.001f || UV.Y > 1.001f;
					Pixel[i] = UV * static_cast<float>(Size);
				}
				NumOutOfRange += bOutOfRange ? 1 : 0;

				const float Area = EdgeFunction(Pixel[0], Pixel[1], Pixel[2]);
				if (FMath::Abs(Area) < UE_SMALL_NUMBER)
				{
					continue;
				}

				// Creases that can reach into this triangle: those touching any of its corners
				CandidateEdges.Reset();
				if (bBakeEdge)
				{
					for (int32 i = 0; i < 3; ++i)
					{
						for (const int32 SharpIndex : SharpEdgesOfVertex[C[i]])
						{
							CandidateEdges.AddUnique(SharpIndex);
						}
					}
				}

				const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
				const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
				const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);
				const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);

				for (int32 Y = MinY; Y <= MaxY; ++Y)
				{
					for (int32 X = MinX; X <= MaxX; ++X)
					{
						// Barycentric weights of the texel center; skip texels outside the triangle
						const FVector2f Center(X + 0.5f, Y + 0.5f);
						const float W0 = EdgeFunction(Pixel[1], Pixel[2], Center) / Area;
						const float W1 = EdgeFunction(Pixel[2], Pixel[0], Center) / Area;
						const float W2 = 1.0f - W0 - W1;
						constexpr float Epsilon = -1e-4f;
						if (W0 < Epsilon || W1 < Epsilon || W2 < Epsilon)
						{
							continue;
						}

						const int32 Index = Y * Size + X;

						// Overlapping or mirrored UVs: an unconnected part of the mesh already wrote this texel
						const int32 Previous = Owner[Index];
						if (Previous != INDEX_NONE && Previous != Triangle)
						{
							const FIntVector& Other = Welded.Triangles[Previous];
							bool bConnected = false;
							for (int32 i = 0; i < 3; ++i)
							{
								bConnected |= C[i] == Other.X || C[i] == Other.Y || C[i] == Other.Z;
							}
							NumConflicts += bConnected ? 0 : 1;
						}
						Owner[Index] = Triangle;
						if (bBakeAO)
						{
							Barycentrics[Index] = FVector2f(W0, W1);
						}

						// Vertex maps: blend the three corner values
						for (const int32 Map : { Cavity, Curvature })
						{
							if (bMap[Map])
							{
								const TArray<float>& V = VertexValues[Map];
								Texels[Map][Index] = V[C[0]] * W0 + V[C[1]] * W1 + V[C[2]] * W2;
							}
						}

						// Edge map: distance from this texel's 3D position to the nearest crease, faded out over EdgeWidth
						if (bBakeEdge)
						{
							const FVector3f Position = Welded.Positions[C[0]] * W0 + Welded.Positions[C[1]] * W1 + Welded.Positions[C[2]] * W2;
							float Value = 0.0f;
							for (const int32 SharpIndex : CandidateEdges)
							{
								const FSharpEdge& Sharp = SharpEdges[SharpIndex];
								const float Distance = DistanceToSegment(Position, Sharp.A, Sharp.B);
								Value = FMath::Max(Value, Sharp.Strength * (1.0f - FMath::SmoothStep(0.0f, SafeEdgeWidth, Distance)));
							}
							Texels[Edge][Index] = Value;
						}
					}
				}
			}

			// 3. Ambient occlusion per covered texel
			if (bBakeAO)
			{
				SlowTask.EnterProgressFrame(AOWork, LOCTEXT("TracingAO", "Tracing ambient occlusion..."));
				if (!TraceAmbientOcclusion(Welded, BVH, Owner, Barycentrics, Size, Texels[AmbientOcclusion]))
				{
					OutResult.Message = CancelledMessage;
					return false;
				}
			}
			if (SlowTask.ShouldCancel())
			{
				OutResult.Message = CancelledMessage;
				return false;
			}

			// 4. Pad the islands so filtering and mips at their edges stay on valid data
			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Padding", "Padding islands..."));
			TBitArray<> Filled(false, NumTexels);
			for (int32 Index = 0; Index < NumTexels; ++Index)
			{
				Filled[Index] = Owner[Index] != INDEX_NONE;
			}
			// Each pass fills empty texels next to filled ones with the average of those neighbors, growing the islands
			// by one texel. Every baked map is filled from the same neighbors, so the maps stay aligned.
			VrrCore::PadIslands(Size, Padding, Filled, [&](int32 Index, TConstArrayView<int32> Neighbors)
			{
				for (int32 Map = 0; Map < NumMaps; ++Map)
				{
					if (bMap[Map])
					{
						float Sum = 0.0f;
						for (const int32 Neighbor : Neighbors)
						{
							Sum += Texels[Map][Neighbor];
						}
						Texels[Map][Index] = Sum / Neighbors.Num();
					}
				}
			});

			// 5. Write the textures
			SlowTask.EnterProgressFrame(1.0f, LOCTEXT("CreatingTextures", "Creating textures..."));
			TArray<uint8> Pixels;

			auto Write = [&](const FString& TextureSuffix, bool bPacked, bool bWithAlpha) -> bool
			{
				FText Error;
				UTexture2D* Texture = WriteTexture(Set.BaseName, TextureSuffix, Size, Pixels, bPacked, bWithAlpha, Error);
				if (!Texture)
				{
					OutResult.Message = Error;
					return false;
				}
				OutResult.Textures.Add(Texture);
				Names.Add(Texture->GetName());
				return true;
			};

			// Unchecked maps get their neutral value in the packed texture
			auto TexelValue = [&](int32 Map, int32 Index) { return bMap[Map] ? Texels[Map][Index] : Background[Map]; };

			if (!bSplitTextures)
			{
				// One mask: R Curvature, G Cavity, B Edge (BGRA byte order). With AO: A Ambient Occlusion and the name _CCEA;
				// without it the alpha is dropped (_CCE, compressed as RGB-only) to keep memory down
				Pixels.SetNumUninitialized(NumTexels * 4);
				for (int32 Index = 0; Index < NumTexels; ++Index)
				{
					Pixels[Index * 4 + 0] = ToByte(TexelValue(Edge, Index));
					Pixels[Index * 4 + 1] = ToByte(TexelValue(Cavity, Index));
					Pixels[Index * 4 + 2] = ToByte(TexelValue(Curvature, Index));
					Pixels[Index * 4 + 3] = bBakeAO ? ToByte(Texels[AmbientOcclusion][Index]) : 255;
				}
				if (!Write(MeshMapBaker::PackedSuffix(bBakeAO), true, bBakeAO))
				{
					return false;
				}
			}
			else
			{
				for (int32 Map = 0; Map < NumMaps; ++Map)
				{
					if (!bMap[Map])
					{
						continue;
					}
					Pixels.SetNumUninitialized(NumTexels);
					for (int32 Index = 0; Index < NumTexels; ++Index)
					{
						Pixels[Index] = ToByte(Texels[Map][Index]);
					}
					if (!Write(Suffix[Map], false, false))
					{
						return false;
					}
				}
			}
		}

		// 6. Summary, plus notes for anything that went wrong
		FText Message = FText::Format(LOCTEXT("MapsBaked", "Baked {0} ({1}x{1}, UV {2}) in {3}. Save the textures to keep them."),
			FText::FromString(FString::Join(Names, TEXT(", "))), Size, UVChannel, FormatDuration(TotalTimer.Elapsed()));
		if (bBakeEdge && SharpEdges.IsEmpty())
		{
			Message = FText::Format(LOCTEXT("NoEdges", "{0} No creases reached the start angle, so the edge map is black."), Message);
		}
		if (bSplitSets)
		{
			TArray<FString> Labels;
			for (const FTextureSet& Set : Sets)
			{
				Labels.Add(Set.Label.ToString());
			}
			Message = FText::Format(LOCTEXT("SetsNote", "{0} One texture set per shading model: {1}."), Message, FText::FromString(FString::Join(Labels, TEXT(", "))));
		}
		for (const FTextureSet& Set : Sets)
		{
			if (Set.NumConflicts > 0)
			{
				Message = bSplitSets
					? FText::Format(LOCTEXT("ConflictNoteSet", "{0} Warning ({1}): {2} texels are shared by separate parts of its materials (overlapping or mirrored UVs); those areas mix values."),
						Message, Set.Label, Set.NumConflicts)
					: FText::Format(LOCTEXT("ConflictNote", "{0} Warning: {1} texels are shared by separate parts of the mesh (overlapping or mirrored UVs); those areas mix values. Use a UV channel with unique UVs, or one texture set per shading model if the overlap is between materials."),
						Message, Set.NumConflicts);
			}
		}
		if (NumOutOfRange > 0)
		{
			Message = FText::Format(LOCTEXT("OutOfRangeNote", "{0} {1} triangles have UVs outside 0-1 and were clipped."), Message, NumOutOfRange);
		}
		OutResult.Message = Message;
		return true;
	}
}

namespace MeshMapBaker
{
	/** _CCEA when the packed mask carries AO in alpha, _CCE (RGB only) when it doesn't. */
	FString PackedSuffix(bool bWithAO)
	{
		return bWithAO ? TEXT("_CCEA") : TEXT("_CCE");
	}

	/** SM_Rock -> T_Rock (the bake adds _CCE, _Cavity, ...). */
	FString SuggestBaseName(const UObject* Mesh)
	{
		return FString::Printf(TEXT("T_%s"), *VrrCore::GetBaseAssetName(Mesh));
	}

	/** Entry point: bakes the checked maps for LOD 0 of the mesh. */
	FMeshMapBakeResult Bake(UObject* Mesh, const FMeshMapBakeSettings& Settings)
	{
		FMeshMapBakeResult Result;
		BakeMapsImpl::FJob Job(Mesh, Settings);
		Result.bSuccess = Job.Bake(Result);
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
