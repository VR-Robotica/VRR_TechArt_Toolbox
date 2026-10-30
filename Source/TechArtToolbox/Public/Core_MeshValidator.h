// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core_AssetValidation.h"

/** Which mesh checks to run. */
struct FMeshValidatorSettings
{
	bool bCheckNaming = true;			// SM_ / SK_ prefix for the mesh type, clean name; SKEL_ and PHYS_ for a skeletal mesh's skeleton and physics asset
	bool bCheckMaterials = true;		// every slot has a material, named M_ or MI_
	bool bCheckCollision = true;		// static meshes have simple collision; skeletal meshes have a physics asset
	bool bCheckLODs = true;				// meshes over MaxTrianglesWithoutLODs have LODs (or Nanite)
	int32 MaxTrianglesWithoutLODs = 10000;
	bool bCheckScale = true;			// bounds between MinSize and MaxSize (catches meshes imported at the wrong scale)
	float MinSize = 1.0f;				// cm
	float MaxSize = 10000.0f;			// cm (100 m)
	bool bCheckLightmap = false;		// static meshes: the lightmap channel exists and isn't UV0
};

/**
 * Checks Static and Skeletal Mesh assets against the project rules: naming (like the texture validator's T_ rules),
 * materials, collision, LODs, scale and lightmap UVs.
 * AssetValidation (Core_AssetValidation.h) finds the meshes and runs this on each.
 */
namespace MeshValidator
{
	/** "SM_ (Static Mesh), SK_ or SKM_ (Skeletal Mesh), ..." for tooltips. */
	FString GetPrefixList();

	/** Checks one loaded mesh (a UStaticMesh or USkeletalMesh), adding what it finds to OutIssues. */
	void ValidateMesh(const FAssetData& Asset, UObject* Mesh, const FMeshValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues);
}
