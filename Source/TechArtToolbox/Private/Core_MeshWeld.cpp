// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Position welding for shape analysis (see Core_MeshWeld.h for the why).
 * Moved here from Core_BakeMaps.cpp unchanged, plus FaceSign, so the Edge Mesh tool can share it.
 */

#include "Core_MeshWeld.h"

#include "StaticMeshAttributes.h"

namespace VrrMeshWeld
{
	void BuildWeldedMesh(const FMeshDescription& MeshDescription, float WeldDistance, FWeldedMesh& Out)
	{
		FStaticMeshConstAttributes Attributes(MeshDescription);
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesConstRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();

		// 1. Weld by position with a hash grid
		TArray<int32> ClusterOfVertex;
		ClusterOfVertex.Init(INDEX_NONE, MeshDescription.Vertices().GetArraySize());
		TMap<FIntVector, TArray<int32>> Grid;
		const float CellSize = FMath::Max(WeldDistance, 0.001f);
		const float WeldDistanceSq = FMath::Square(WeldDistance);

		for (const FVertexID VertexID : MeshDescription.Vertices().GetElementIDs())
		{
			const FVector3f Position = Positions[VertexID];
			const FIntVector Cell(FMath::FloorToInt32(Position.X / CellSize), FMath::FloorToInt32(Position.Y / CellSize), FMath::FloorToInt32(Position.Z / CellSize));

			int32 Cluster = INDEX_NONE;
			for (int32 X = -1; X <= 1 && Cluster == INDEX_NONE; ++X)
			for (int32 Y = -1; Y <= 1 && Cluster == INDEX_NONE; ++Y)
			for (int32 Z = -1; Z <= 1 && Cluster == INDEX_NONE; ++Z)
			{
				if (const TArray<int32>* Clusters = Grid.Find(Cell + FIntVector(X, Y, Z)))
				{
					for (const int32 Candidate : *Clusters)
					{
						if (FVector3f::DistSquared(Out.Positions[Candidate], Position) <= WeldDistanceSq)
						{
							Cluster = Candidate;
							break;
						}
					}
				}
			}
			if (Cluster == INDEX_NONE)
			{
				Cluster = Out.Positions.Add(Position);
				Grid.FindOrAdd(Cell).Add(Cluster);
			}
			ClusterOfVertex[VertexID.GetValue()] = Cluster;
		}

		// 2. Face winding: match the mesh's own normals, whatever the handedness
		double Orientation = 0.0;
		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = MeshDescription.GetTriangleVertexInstances(TriangleID);
			const FVector3f P0 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[0])];
			const FVector3f P1 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[1])];
			const FVector3f P2 = Positions[MeshDescription.GetVertexInstanceVertex(Corners[2])];
			Orientation += FVector3f::DotProduct(FVector3f::CrossProduct(P1 - P0, P2 - P0), InstanceNormals[Corners[0]] + InstanceNormals[Corners[1]] + InstanceNormals[Corners[2]]);
		}
		const float FaceSign = Orientation < 0.0 ? -1.0f : 1.0f;
		Out.FaceSign = FaceSign;

		// 3. Triangles, face normals, angle-weighted vertex normals, adjacency
		const int32 NumClusters = Out.Positions.Num();
		Out.Normals.SetNumZeroed(NumClusters);
		Out.Neighbors.SetNum(NumClusters);

		for (const FTriangleID TriangleID : MeshDescription.Triangles().GetElementIDs())
		{
			TArrayView<const FVertexInstanceID> Corners = MeshDescription.GetTriangleVertexInstances(TriangleID);
			int32 C[3];
			FVector3f P[3];
			for (int32 i = 0; i < 3; ++i)
			{
				C[i] = ClusterOfVertex[MeshDescription.GetVertexInstanceVertex(Corners[i]).GetValue()];
				P[i] = Out.Positions[C[i]];
			}
			const FVector3f FaceNormal = FVector3f::CrossProduct(P[1] - P[0], P[2] - P[0]).GetSafeNormal() * FaceSign;
			if (C[0] == C[1] || C[1] == C[2] || C[0] == C[2] || FaceNormal.IsNearlyZero())
			{
				continue; // degenerate
			}

			const int32 Triangle = Out.Triangles.Add(FIntVector(C[0], C[1], C[2]));
			Out.Corners.Append({ Corners[0], Corners[1], Corners[2] });
			Out.FaceNormals.Add(FaceNormal);
			Out.PolygonGroups.Add(MeshDescription.GetTrianglePolygonGroup(TriangleID).GetValue());

			for (int32 i = 0; i < 3; ++i)
			{
				const FVector3f EdgeA = (P[(i + 1) % 3] - P[i]).GetSafeNormal();
				const FVector3f EdgeB = (P[(i + 2) % 3] - P[i]).GetSafeNormal();
				// Corner angle weighting: the result doesn't depend on how the faces are triangulated
				Out.Normals[C[i]] += FaceNormal * FMath::Acos(FMath::Clamp(FVector3f::DotProduct(EdgeA, EdgeB), -1.0f, 1.0f));

				const int32 A = C[i];
				const int32 B = C[(i + 1) % 3];
				TArray<int32, TInlineAllocator<2>>& Users = Out.EdgeTriangles.FindOrAdd(EdgeKey(A, B));
				// First time this edge is seen: record the two vertices as neighbors
				if (Users.IsEmpty())
				{
					Out.Neighbors[A].Add(B);
					Out.Neighbors[B].Add(A);
				}
				Users.Add(Triangle);
			}
		}

		for (FVector3f& Normal : Out.Normals)
		{
			Normal = Normal.GetSafeNormal();
		}
	}
}
