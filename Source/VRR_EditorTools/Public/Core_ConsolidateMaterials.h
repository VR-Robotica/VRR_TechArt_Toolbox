// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UMaterialInterface;
class UTexture2D;

/** A material output that can be consolidated into its own texture. */
enum class EConsolidateMap : uint8
{
	BaseColor,
	Metallic,
	Specular,
	Roughness,
	Normal,
	Emissive,
	Opacity,
	OpacityMask,
	AmbientOcclusion,
	Count
};

struct FConsolidateSettings
{
	/** The UV channel every material's output is laid out in. It should be unique (no overlapping islands across all slots). */
	int32 TargetChannel = 1;
	/** Width and height of each texture. */
	int32 Resolution = 2048;
	/** Pixels each island grows by, so filtering and mips don't pull in empty texels. */
	int32 Padding = 8;
	/**
	 * One texture set per shading model (Default Lit, Preintegrated Skin, Subsurface Profile...) instead of one for
	 * the whole mesh. Each set is <Base>_<ShadingModel>_<suffix>, and only that shading model's slots are baked into it.
	 */
	bool bSplitByShadingModel = false;

	/** Which outputs to write, indexed by EConsolidateMap. */
	bool bMaps[static_cast<int32>(EConsolidateMap::Count)] = { true, true, true, true, true, true, true, true, true };
	/** Don't write an output that is one flat value everywhere (metallic 0, no emissive...); the message lists its value instead. */
	bool bSkipFlatMaps = true;

	// Output
	FString OutputFolder;
	/** T_<Mesh>: each texture is <Base>_<suffix> (_D, _N, _R...). */
	FString OutputBaseName;
	bool bOverwrite = true;
	bool bSaveToDisk = true;

	bool HasAnyMap() const
	{
		for (const bool bMap : bMaps)
		{
			if (bMap)
			{
				return true;
			}
		}
		return false;
	}
};

/** One material slot of the mesh (LOD 0) that has triangles. */
struct FConsolidateSlot
{
	int32 SlotIndex = INDEX_NONE;
	FName SlotName;
	UMaterialInterface* Material = nullptr;
	int32 NumTriangles = 0;
	/** The mesh data's polygon groups drawn with this slot (what the material baker calls section indices). */
	TArray<int32> PolygonGroups;
	/** The material's shading model(s), for texture names: "DefaultLit", "PreintegratedSkin"... */
	FString ShadingModelTag;
	/** "Default Lit", "Preintegrated Skin"... */
	FText ShadingModelLabel;
};

/** Slots baked into one texture set: every slot, or the slots sharing a shading model. */
struct FConsolidateGroup
{
	/** Empty for the whole mesh, otherwise the shading model tag that goes in the texture names. */
	FString Tag;
	FText Label;
	TArray<FConsolidateSlot> Slots;
};

struct FConsolidateResult
{
	bool bSuccess = false;
	/** What was written plus any warnings, or the error. */
	FText Message;
	/** Textures created or updated. */
	TArray<UTexture2D*> Textures;
};

/**
 * Consolidates the materials of a multi-material mesh into one texture set: every slot's material is rendered
 * (its final Base Color, Metallic, Roughness, Normal... after all its math, textures and parameters) through one
 * UV channel of the mesh, and the slots are combined, so one texture per output covers the whole mesh.
 * Uses LOD 0 of a Static or Skeletal Mesh and Unreal's MaterialBaking module (the one Merge Actors uses).
 */
namespace MaterialConsolidator
{
	/** "Base Color", "Roughness"... */
	FText GetMapLabel(EConsolidateMap Map);

	/** The texture suffix the Validate page expects: D, M, S, R, N, E, O, OM, AO. */
	const TCHAR* GetMapSuffix(EConsolidateMap Map);

	/** The mesh's material slots that have triangles on LOD 0, in slot order (empty when it has no LOD 0 data). */
	TArray<FConsolidateSlot> GetSlots(UObject* Mesh);

	/** T_<mesh name without SM_/SK_> */
	FString SuggestBaseName(const UObject* Mesh);

	/** The texture sets the settings produce: one with every slot, or one per shading model (in first-slot order). */
	TArray<FConsolidateGroup> GetGroups(const TArray<FConsolidateSlot>& Slots, const FConsolidateSettings& Settings);

	/** One group per shading model (tagged "DefaultLit", "PreintegratedSkin"...), in first-slot order. Also used by the Mesh Maps bake. */
	TArray<FConsolidateGroup> GroupByShadingModel(const TArray<FConsolidateSlot>& Slots);

	/** <Base> for the whole mesh, <Base>_<Tag> for a shading model's set. Each texture adds _<suffix>. */
	FString GetGroupBaseName(const FConsolidateGroup& Group, const FConsolidateSettings& Settings);

	FConsolidateResult Consolidate(UObject* Mesh, const FConsolidateSettings& Settings);
}
