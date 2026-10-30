// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/** How islands are packed into their region. */
enum class EUVPackMethod : uint8
{
	/** Bounding boxes on shelves: fast, but leaves the space around irregular islands empty. */
	Boxes,
	/** Island shapes on a grid: islands nest into each other's gaps and holes, and try all four quarter turns. */
	Shapes,
};

struct FUVLayoutSettings
{
	// Channels
	int32 SourceChannel = 0;			// islands (seams) come from this channel; it is never changed
	int32 TargetChannel = 1;			// the new layout goes here; a lightmap already using it moves up one channel

	// Layout
	bool bRotateIslands = true;			// turn each island to its tightest bounding box, lying flat, for denser packing
	bool bGroupByMaterial = true;		// one region per material slot: columns first (2 = two columns, 3 = three), then more rows, always filling the square
	bool bMatchDensityAcrossMaterials = false;	// same texel density in every region (some regions won't be full)
	bool bGroupAsUDIMs = false;			// one whole UDIM tile per material slot (1001, 1002, ... 1010, then 1011 one row down) instead of sharing the 0-1 square
	int32 Padding = 4;					// pixels between islands, at PaddingResolution
	int32 PaddingResolution = 1024;		// texture size the padding is measured against

	// Packing (the grouped layout uses the defaults; MakeBalancedSettings copies the Balanced* options in)
	EUVPackMethod PackMethod = EUVPackMethod::Boxes;
	int32 PackDetail = 512;				// Shapes: grid cells across the square (at most PaddingResolution, so lightmaps pack per texel)
	bool bStretchToFill = false;		// scale each axis separately so the islands reach the region's edges (density no longer even)

	// Mesh
	bool bAllLODs = true;				// also lay out LODs that have their own source data (each laid out separately)

	// Balanced UV layout: every island at even density in one region (material slots ignored)
	bool bBalancedAsLightmap = true;	// static meshes: write it as the lightmap UVs, replacing Unreal's generated ones
	int32 BalancedTargetChannel = 1;	// when not a lightmap: where it goes (a lightmap there moves up one channel)
	int32 BalancedPadding = 2;			// pixels (texels) between islands, at BalancedResolution
	int32 BalancedResolution = 64;		// texture size the padding is measured against; as a lightmap, the mesh's Light Map Resolution
	EUVPackMethod BalancedPackMethod = EUVPackMethod::Shapes;
	int32 BalancedPackDetail = 512;
	bool bBalancedStretchToFill = false;

	// Texel density: rescale an existing channel so one UV tile covers DensityMeters (e.g. a 512 px texture = 0.5 m)
	int32 DensityChannel = 0;			// the channel rescaled in place (usually UV0, for tiling textures)
	int32 DensityTextureSize = 1024;	// the texture that tiles over it: only used to show pixels per meter
	float DensityMeters = 1.0f;			// meters one UV tile (the whole texture) covers
	bool bDensityPerIsland = true;		// each island to the target (even density); off: one scale for the whole channel
};

/** How dense a UV channel is now: UV units per cm of surface (multiply by texture size * 100 for pixels per meter). */
struct FUVDensityInfo
{
	bool bValid = false;
	/** Whole channel: sqrt(total UV area / total 3D area). */
	float AverageUVPerCm = 0.0f;
	/** Least and most dense island (islands with no UV or surface area are left out). */
	float MinUVPerCm = 0.0f;
	float MaxUVPerCm = 0.0f;
	int32 NumIslands = 0;
	/** Islands with no UV area: they can't be scaled and are left as they are. */
	int32 NumFlat = 0;
};

/** Where the balanced layout (or the lightmap UVs) will go. */
struct FUVLayoutBalancedPlan
{
	/** Why lightmap UVs can't be created (or empty). */
	FText Blocker;
	FText Summary;
	/** The channel they will be written to. */
	int32 Channel = INDEX_NONE;
};

/** What the layout looks like, for the preview. All coordinates are in the 0-1 UV square. */
struct FUVLayoutPreview
{
	/** Triangles: 3 points each. */
	TArray<FVector2f> Triangles;
	/** Region (material group) of each triangle. */
	TArray<int32> TriangleGroups;
	/** Island outlines: 2 points per segment. */
	TArray<FVector2f> Outlines;
	/** One rectangle per region. */
	TArray<FBox2f> Regions;
	/** Material slot name of each region. */
	TArray<FName> RegionNames;

	int32 NumIslands = 0;
	/** Fraction of the regions' area covered by islands (the 0-1 square, or all UDIM tiles). */
	float Coverage = 0.0f;
	/** Regions are whole UDIM tiles (coordinates go past 0-1). */
	bool bUDIM = false;
	/** Texel density: UVs run past 0-1 over whole tiles; Regions[0] is the tiles covered (0-1 always included). */
	bool bTiled = false;
	/** Islands with no UV area in the source channel, laid flat from their 3D shape instead. */
	int32 NumProjected = 0;

	bool IsEmpty() const { return Triangles.IsEmpty(); }
};

/** What applying will do to the mesh's UV channels and lightmap settings. */
struct FUVLayoutChannelPlan
{
	/** Why the layout can't be applied (or empty). */
	FText Blocker;
	/** One-line summary for the page, e.g. "UV1 holds the lightmap: it moves to UV2 and the layout goes into UV1." */
	FText Summary;
	/** The target channel holds the lightmap, which moves up one channel. */
	bool bMovesLightmap = false;
	/** The target channel already exists (and isn't the lightmap): its UVs are replaced. */
	bool bOverwrites = false;
};

struct FUVLayoutResult
{
	bool bSuccess = false;
	FText Message;
};

/**
 * Builds a new non-overlapping UV layout for a Static or Skeletal Mesh from the islands of an existing channel:
 * every island scaled to the same texel density (by its 3D surface area), optionally rotated, grouped into one
 * region per material slot and packed to fill the 0-1 square. Useful as a unique channel for baking mesh maps.
 */
namespace UVLayout
{
	/** Highest UV channel count the mesh type supports (static 8, skeletal 4). */
	int32 GetMaxUVChannels(const UObject* Mesh);

	/** The lightmap channel of a static mesh (-1 for skeletal meshes, which have no lightmaps). */
	int32 GetLightmapChannel(const UObject* Mesh);

	/** Distinct material slots used by LOD 0's triangles. */
	int32 GetNumMaterialSlots(UObject* Mesh);

	/** What Apply would do with these settings. */
	FUVLayoutChannelPlan PlanChannels(UObject* Mesh, const FUVLayoutSettings& Settings);

	/** Computes the LOD 0 layout without changing the mesh. False (with OutError) if it can't. */
	bool ComputePreview(UObject* Mesh, const FUVLayoutSettings& Settings, FUVLayoutPreview& OutPreview, FText& OutError);

	/** Writes the layout into the target channel (undoable), moving the lightmap channel first when needed. */
	FUVLayoutResult Apply(UObject* Mesh, const FUVLayoutSettings& Settings);

	// BALANCED UV LAYOUT (one region, even density; optionally the lightmap)

	/** Static meshes only: skeletal meshes have no lightmaps. */
	bool SupportsLightmaps(const UObject* Mesh);

	/** Will the balanced layout be written as the lightmap? (Option on, and a static mesh.) */
	bool IsBalancedLightmap(const UObject* Mesh, const FUVLayoutSettings& Settings);

	/** The layout settings the balanced layout uses: one region for all islands, its own padding and target channel. */
	FUVLayoutSettings MakeBalancedSettings(const FUVLayoutSettings& Settings);

	/** Where the balanced layout will go: "Blocker" if it can't, else a one-line summary. */
	FUVLayoutBalancedPlan PlanBalanced(UObject* Mesh, const FUVLayoutSettings& Settings);

	/** As the lightmap: ApplyLightmap. Otherwise: Apply with the balanced settings into BalancedTargetChannel. */
	FUVLayoutResult ApplyBalanced(UObject* Mesh, const FUVLayoutSettings& Settings);

	/**
	 * The lightmap channel: where Generate Lightmap UVs writes, else the mesh's lightmap index (if it isn't the
	 * source), else a new last channel.
	 */
	FUVLayoutBalancedPlan PlanLightmap(UObject* Mesh, const FUVLayoutSettings& Settings);

	/**
	 * Writes lightmap UVs into that channel on every LOD laid out (undoable), turns off Generate Lightmap UVs so the
	 * build keeps them, and points the mesh's lightmap index and resolution at them.
	 */
	FUVLayoutResult ApplyLightmap(UObject* Mesh, const FUVLayoutSettings& Settings);

	// TEXEL DENSITY (rescale an existing channel in place)

	/** UV units per cm the settings ask for: one tile per DensityMeters. */
	float GetTargetUVPerCm(const FUVLayoutSettings& Settings);

	/** LOD 0's current density in Channel (bValid false if the mesh or channel is missing). */
	FUVDensityInfo MeasureDensity(UObject* Mesh, int32 Channel);

	/**
	 * LOD 0's channel as it will look after scaling, without changing the mesh: triangles colored per island,
	 * island outlines, and the whole tiles the UVs cover (bTiled). NumProjected counts the flat islands left as-is.
	 */
	bool ComputeDensityPreview(UObject* Mesh, const FUVLayoutSettings& Settings, FUVLayoutPreview& OutPreview, FText& OutError);

	/** Why the channel can't be rescaled (no mesh or channel, or it's the lightmap), or empty. */
	FText GetDensityBlocker(UObject* Mesh, const FUVLayoutSettings& Settings);

	/**
	 * Scales DensityChannel (every LOD laid out, undoable) to the target density: each island about its own center,
	 * or with bDensityPerIsland off, the whole channel by one factor about its center (relative sizes kept).
	 */
	FUVLayoutResult ApplyTexelDensity(UObject* Mesh, const FUVLayoutSettings& Settings);
}
