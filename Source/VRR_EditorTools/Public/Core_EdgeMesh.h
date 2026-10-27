// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UStaticMesh;

struct FEdgeMeshSettings
{
	// Which edges
	float MinAngle = 40.0f;			// degrees between two faces for their shared edge to count as a crease
	bool bConvexOnly = true;		// outer (convex) creases only; off also takes inner (valley) creases
	float MinLength = 15.0f;		// cm: crease chains shorter than this are dropped (small details)
	float MaxTurnAngle = 50.0f;		// degrees: a chain ends where the crease turns sharper than this (box corners)

	// Strips
	float Width = 1.5f;				// cm the strip reaches onto each face from the crease
	float SurfaceOffset = 0.1f;		// cm the strip floats above the surface, so it never z-fights
	float UTileLength = 0.0f;		// cm of edge per texture repeat along U; 0 = 2 x Width (square texels)
	float WeldDistance = 0.01f;		// cm: vertices this close count as one when finding creases

	// Output
	TWeakObjectPtr<UMaterialInterface> Material;	// put on the new mesh's only slot (empty = engine default)
	FString OutputFolder;
	FString OutputName;
	bool bOverwrite = true;			// rebuild an existing mesh with this name in place (keeps references)
};

/** What the settings pick out, for the preview: every crease, sorted into kept and dropped. */
struct FEdgeMeshPreview
{
	/** Kept crease segments (2 points each), in the mesh's space. */
	TArray<FVector3f> Kept;
	/** Creases that passed the angle test but were dropped (chain shorter than Min Length). */
	TArray<FVector3f> Dropped;
	/** Every mesh edge (2 points each), drawn faintly for context. */
	TArray<FVector3f> Wireframe;
	FBox3f Bounds = FBox3f(ForceInit);

	int32 NumChains = 0;
	int32 NumKeptChains = 0;
	float KeptLength = 0.0f;		// cm
	int32 NumTriangles = 0;			// in the strips that would be built

	bool IsEmpty() const { return Wireframe.IsEmpty(); }
};

struct FEdgeMeshResult
{
	bool bSuccess = false;
	FText Message;
	UStaticMesh* Mesh = nullptr;
};

/**
 * Builds an "edge mesh" from a Static Mesh: thin folded strips that hug its sharp outer edges, as their own Static
 * Mesh with the same pivot. Put it on the same transform as the source (e.g. as a child component) and give it a
 * decal-style material (masked or translucent edge wear, chipped paint, grime) to paint the edges without touching
 * the source's textures. UV U runs along the edge, V across it (0 one side, 0.5 on the crease, 1 the other side).
 */
namespace EdgeMeshBuilder
{
	/** SM_<Base>_Edges */
	FString SuggestName(const UStaticMesh* Mesh);

	/** Finds and sorts the creases without building anything. False (with OutError) if the mesh can't be read. */
	bool ComputePreview(UStaticMesh* Mesh, const FEdgeMeshSettings& Settings, FEdgeMeshPreview& OutPreview, FText& OutError);

	/** Builds the strips and writes them to OutputFolder/OutputName (created, or rebuilt in place). Not saved. */
	FEdgeMeshResult Build(UStaticMesh* Mesh, const FEdgeMeshSettings& Settings);
}
