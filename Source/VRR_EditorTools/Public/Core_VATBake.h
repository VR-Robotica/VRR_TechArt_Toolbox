// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AnimToTextureDataAsset.h"
#include "AssetRegistry/AssetData.h"

class UAnimSequence;
class UMaterialInterface;
class USkeletalMesh;
class UStaticMesh;

/** Default VAT master material (VRR_Materials plugin). */
namespace VatDefaults
{
	inline const TCHAR* Material = TEXT("/VRR_Materials/Masters/M_BasicColor_VAT.M_BasicColor_VAT");
}

/** What the VAT page puts into the new assets. */
struct FVatBakeSettings
{
	/**
	 * Bone: per-bone position/rotation plus per-vertex weights (meshes sharing a skeleton can reuse it).
	 * The page only offers Bone, since the VRR VAT materials are Bone mode; Vertex support is kept in the baker.
	 */
	EAnimToTextureMode Mode = EAnimToTextureMode::Bone;

	FString OutputFolder = TEXT("/Game");
	/** Base of every asset name: DA_<Name>_LOD0, SM_<Name>, T_<Name>_BonePos... */
	FString OutputName;

	// Data Asset settings (written by Create; edit the Data Assets to change them before baking)

	/** Drop each animation's last key, which repeats its first pose in a looping animation (no hitch when it loops). */
	bool bSeamlessLoops = true;
	/** Max width and height of the baked textures. Height is total frames x rows per frame, so long animation sets need more. */
	int32 MaxTextureSize = 8192;
	/** Texture precision. Bone mode always uses 16 bits when the skeleton has more than 256 bones. */
	EAnimToTexturePrecision Precision = EAnimToTexturePrecision::SixteenBits;
	/** Bone mode: bones blended per vertex. Two is cheaper and matches the master material's default switch. */
	EAnimToTextureNumBoneInfluences NumBoneInfluences = EAnimToTextureNumBoneInfluences::Two;
	bool bEnforcePowerOfTwo = false;

	// Bake settings

	/** Master material the VAT material instances are made from. Empty skips the instances. */
	FSoftObjectPath Material = FSoftObjectPath(VatDefaults::Material);
	/** Place (or update) an actor showing SM_<Name> at the level origin after baking. */
	bool bSpawnPreviewActor = true;
};

/**
 * The assets Create made for one skeletal mesh, which Bake then fills in.
 * One Data Asset per LOD, each with its own static mesh (SM_<Name>_LOD#) and textures.
 */
struct FVatAssetSet
{
	TWeakObjectPtr<USkeletalMesh> SkeletalMesh;
	EAnimToTextureMode Mode = EAnimToTextureMode::Bone;
	FString Folder;
	FString BaseName;

	/** Index = LOD. */
	TArray<TWeakObjectPtr<UAnimToTextureDataAsset>> DataAssets;

	/** SM_<Name>: all LODs in one mesh, made by Bake. */
	TWeakObjectPtr<UStaticMesh> CombinedMesh;

	bool IsEmpty() const { return DataAssets.IsEmpty(); }
	FString GetCombinedMeshName() const { return TEXT("SM_") + BaseName; }
	/** MI_<Name>: instance of the master material with the baked settings, parent of every slot's instance. */
	FString GetBaseMaterialName() const { return TEXT("MI_") + BaseName; }
};

/** Outcome of Create or Bake: message for the status line, and the assets to show in the Content Browser. */
struct FVatResult
{
	bool bSuccess = false;
	FText Message;
	TArray<UObject*> Assets;
};

/**
 * Builds Vertex Animation Textures with Unreal's AnimToTexture plugin, one LOD at a time.
 * No UI here: the Toolbox "VAT" page (Menu_VATBake) collects the choices.
 *
 * Create: for every skeletal LOD, a static mesh copy of that LOD and a Data Asset pointing at it, plus the
 *         (empty) textures the plugin writes into. Bone mode shares the bone position/rotation textures
 *         across LODs and gives each LOD its own weight texture; Vertex mode gives each LOD its own
 *         position/normal textures.
 * Bake:   runs the plugin on each LOD's Data Asset (textures + VAT UVs), then copies the baked LOD meshes
 *         into one static mesh (SM_<Name>) with a material slot per LOD, since each LOD reads its own textures.
 *         It also makes MI_<Name> (the mode's master material with the baked settings) and a child of it per
 *         slot (MI_<Name>_<Slot>_LOD#, with that LOD's own textures), assigned to SM_<Name> and to each
 *         SM_<Name>_LOD#, and can place a
 *         preview actor in the level.
 */
namespace VatBaker
{
	/** Animation Sequences whose skeleton is (or is compatible with) the mesh's skeleton, sorted by name. */
	TArray<FAssetData> FindAnimations(const USkeletalMesh* Mesh);

	/** <Mesh>_VAT, without the mesh's type prefix. */
	FString SuggestName(const USkeletalMesh* Mesh);

	/** Number of LODs the mesh has (each gets baked). */
	int32 GetNumLODs(const USkeletalMesh* Mesh);

	/** Creates the per-LOD static meshes, Data Assets and textures (unsaved). Never overwrites: the name gets a number when any of them is taken. */
	FVatResult CreateAssets(USkeletalMesh* Mesh, const TArray<UAnimSequence*>& Animations, const FVatBakeSettings& Settings, FVatAssetSet& OutSet);

	/** Bakes every LOD's Data Asset, then builds (or rebuilds) SM_<Name> with all LODs, its VAT material instances and the preview actor. */
	FVatResult Bake(FVatAssetSet& Set, const FVatBakeSettings& Settings);

	/** The parameters a master material needs for this mode and doesn't have (empty when it fits). */
	TArray<FName> FindMissingParameters(const UMaterialInterface* Material, EAnimToTextureMode Mode);

	/** How many animations the material has frame ranges for (Anim0, Anim1... - Start Frame). */
	int32 GetNumAnimationSlots(const UMaterialInterface* Material);
}
