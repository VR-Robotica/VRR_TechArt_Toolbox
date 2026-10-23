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

	// Ambient Occlusion: dark where nearby geometry blocks the sky (rays cast over the hemisphere above each texel)
	bool bBakeAO = true;
	int32 AORays = 64;				// rays per texel: more is smoother but slower
	float AODistance = 10.0f;		// cm; geometry farther than this doesn't occlude
	float AOBias = 0.02f;			// cm the rays start above the surface, so a face doesn't hit itself
	float AOStrength = 1.0f;		// contrast exponent: >1 darker, <1 lighter
	bool bAODistanceFalloff = true;	// near hits occlude fully, hits near AODistance barely
	bool bAOIgnoreBackFaces = true;	// rays hitting the inside of a surface don't count (open or overlapping shells)
	int32 AOBlur = 1;				// texel-space smoothing passes, stays within connected surface

	// Output
	int32 UVChannel = 0;
	int32 Resolution = 2048;
	int32 Padding = 8;
	bool bSplitTextures = false;	// default: one packed mask, _CCE (RGB) or with AO _CCEA (RGBA, A = AO)
	bool bOverwrite = true;
	FString OutputFolder;
	FString OutputBaseName;

	bool HasAnyMap() const { return bBakeCavity || bBakeEdge || bBakeCurvature || bBakeAO; }
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
 * Bakes linear grayscale mesh maps (Cavity, Edge, Curvature, Ambient Occlusion) from a Static or Skeletal Mesh into textures,
 * through one of its UV channels. Each map is computed from the mesh's own geometry (LOD 0).
 * By default they go into one packed mask: <Base>_CCE (RGB: R Curvature, G Cavity, B Edge), or with AO checked
 * <Base>_CCEA (the same plus A Ambient Occlusion).
 */
namespace MeshMapBaker
{
	/** _CCEA when the packed mask carries AO in alpha, _CCE (RGB only) when it doesn't. */
	FString PackedSuffix(bool bWithAO);

	/** T_<mesh name without SM_/SK_> */
	FString SuggestBaseName(const UObject* Mesh);

	FMeshMapBakeResult Bake(UObject* Mesh, const FMeshMapBakeSettings& Settings);
}
