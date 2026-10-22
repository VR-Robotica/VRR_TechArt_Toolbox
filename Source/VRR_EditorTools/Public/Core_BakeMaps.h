// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

struct FMeshMapBakeSettings
{
	float WeldDistance = 0.01f;

	// Cavity: dark where the surface sits below its smoothed neighborhood
	bool bBakeCavity = true;
	int32 CavitySpread = 10;		// smoothing iterations: how wide a pocket counts as a cavity
	float CavityDepth = 0.3f;		// cm below the neighborhood that reaches full black

	// Edge: white along sharp creases
	bool bBakeEdge = true;
	float EdgeMinAngle = 15.0f;		// degrees between faces where an edge starts to show
	float EdgeFullAngle = 60.0f;	// degrees between faces where an edge is fully white
	float EdgeWidth = 0.3f;			// cm from the crease to where the line fades out
	bool bEdgeConcave = false;		// also mark inward (valley) creases

	// Curvature: 0.5 flat, brighter convex, darker concave
	bool bBakeCurvature = true;
	float CurvatureRadius = 2.0f;	// radius (cm) whose curvature reaches full white/black
	int32 CurvatureBlur = 2;

	// Output
	int32 UVChannel = 0;
	int32 Resolution = 2048;
	int32 Padding = 8;
	bool bSplitTextures = false;	// default: one packed RGB mask (R Curvature, G Cavity, B Edge)
	bool bOverwrite = true;
	FString OutputFolder;
	FString OutputBaseName;

	bool HasAnyMap() const { return bBakeCavity || bBakeEdge || bBakeCurvature; }
};

struct FMeshMapBakeResult
{
	bool bSuccess = false;
	/** What was baked plus any warnings, or the error. */
	FText Message;
	/** Textures created or updated (unsaved). */
	TArray<UTexture2D*> Textures;
};

/**
 * Bakes linear grayscale mesh maps (Cavity, Edge, Curvature) from a Static or Skeletal Mesh into textures,
 * through one of its UV channels. Each map is computed from the mesh's own geometry (LOD 0).
 * By default the three go into one packed mask, <Base>_CCE: R Curvature, G Cavity, B Edge.
 */
namespace MeshMapBaker
{
	/** T_<mesh name without SM_/SK_> */
	FString SuggestBaseName(const UObject* Mesh);

	FMeshMapBakeResult Bake(UObject* Mesh, const FMeshMapBakeSettings& Settings);
}
