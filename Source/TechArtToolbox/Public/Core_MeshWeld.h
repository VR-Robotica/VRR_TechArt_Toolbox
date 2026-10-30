// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MeshDescription.h"

/**
 * A mesh welded by position, shared by the tools that analyze a mesh's shape (Bake Mesh Maps, Edge Mesh).
 *
 * Strategy
 *  Mesh data splits a vertex wherever its normal or UV changes (hard edges, UV seams), so neighboring faces often
 *  don't share vertices. Shape analysis needs them to: curvature, cavities and creases are all about how faces
 *  meet. Welding merges vertices closer than a small distance, then rebuilds the connectivity on the merged set:
 *  which vertices neighbor each other and which triangles share each edge.
 */
namespace VrrMeshWeld
{
	/** The mesh with vertices welded by position, so hard edges and UV seams don't break up neighborhoods. */
	struct FWeldedMesh
	{
		TArray<FVector3f> Positions;				// per welded vertex
		TArray<FVector3f> Normals;					// per welded vertex, smooth (angle-weighted face normals)
		TArray<TArray<int32>> Neighbors;			// per welded vertex
		TArray<FIntVector> Triangles;				// welded vertex indices, non-degenerate triangles only
		TArray<FVertexInstanceID> Corners;			// 3 per triangle, for UV lookups
		TArray<FVector3f> FaceNormals;				// per triangle
		TArray<int32> PolygonGroups;				// per triangle: the mesh data's polygon group (material section)
		TMap<uint64, TArray<int32, TInlineAllocator<2>>> EdgeTriangles;	// edge key -> triangles using it
		/**
		 * +1 or -1: the winding the mesh uses for front faces. FaceNormal = normalize(cross(P1 - P0, P2 - P0)) * FaceSign,
		 * so new geometry built with the same rule faces the same way as the source.
		 */
		float FaceSign = 1.0f;
	};

	/** Order-independent key for the edge between two welded vertices (smaller index in the high bits). */
	inline uint64 EdgeKey(int32 A, int32 B)
	{
		if (A > B)
		{
			Swap(A, B);
		}
		return (static_cast<uint64>(static_cast<uint32>(A)) << 32) | static_cast<uint32>(B);
	}

	/** EdgeKey back to its two vertex indices. */
	inline void SplitEdgeKey(uint64 Key, int32& OutA, int32& OutB)
	{
		OutA = static_cast<int32>(Key >> 32);
		OutB = static_cast<int32>(Key & 0xffffffffu);
	}

	/**
	 * Builds FWeldedMesh from the mesh description:
	 *  1. weld vertices closer than WeldDistance (spatial grid lookup),
	 *  2. detect face winding from the mesh's own normals,
	 *  3. collect triangles, face normals, angle-weighted smooth normals, neighbors and edge -> triangle links.
	 */
	void BuildWeldedMesh(const FMeshDescription& MeshDescription, float WeldDistance, FWeldedMesh& Out);
}
