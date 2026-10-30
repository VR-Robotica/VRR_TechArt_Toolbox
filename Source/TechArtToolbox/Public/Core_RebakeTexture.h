// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Engine/TextureDefines.h"

class UTexture2D;

/** How the source texture is read between its texels. */
enum class ERebakeFilter : uint8
{
	Bilinear,
	Nearest,
};

struct FRebakeTextureSettings
{
	// UVs
	int32 SourceChannel = 0;		// the channel the texture was made for
	int32 TargetChannel = 1;		// the channel the new texture is laid out in

	// Sampling
	ERebakeFilter Filter = ERebakeFilter::Bilinear;
	int32 Supersampling = 2;		// samples per texel along each axis (2 = 4 samples), smooths edges and shrinking
	bool bNormalMap = false;		// tangent-space normal map: rotate the normals from the source UVs' tangents to the target's
	int32 Resolution = 2048;		// width and height of the new texture
	int32 Padding = 8;				// pixels each island grows by, so filtering and mips don't pull in empty texels

	// Compression (start as the source texture's)
	TEnumAsByte<TextureCompressionSettings> CompressionSettings = TC_Default;
	bool bSRGB = true;
	bool bCompressionNoAlpha = false;
	TEnumAsByte<TextureGroup> LODGroup = TEXTUREGROUP_World;
	TEnumAsByte<TextureMipGenSettings> MipGenSettings = TMGS_FromTextureGroup;

	// Output
	FString OutputFolder;
	FString OutputName;
	bool bOverwrite = true;
	bool bSaveToDisk = true;		// save the texture's package when done (otherwise it is left unsaved)
};

struct FRebakeTextureResult
{
	bool bSuccess = false;
	FText Message;
	UTexture2D* Texture = nullptr;
};

/**
 * Re-bakes a texture from one UV channel of a mesh to another: every texel of the new texture finds the triangle
 * covering it in the target channel, takes the same surface point's position in the source channel, and samples
 * the source texture there. Uses LOD 0 of a Static or Skeletal Mesh.
 */
namespace TextureRebaker
{
	/** Fills the compression settings, resolution and normal map option from the source texture. */
	void CopyTextureSettings(const UTexture2D* Texture, FRebakeTextureSettings& InOutSettings);

	/** <Texture name>_UV<target channel>. */
	FString SuggestName(const UTexture2D* Texture, int32 TargetChannel);

	/** Why the texture can't be read as a source (UDIM, no source data...), or empty. */
	FText GetTextureBlocker(const UTexture2D* Texture);

	FRebakeTextureResult Rebake(UObject* Mesh, UTexture2D* Texture, const FRebakeTextureSettings& Settings);
}
