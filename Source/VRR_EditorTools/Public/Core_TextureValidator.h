// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_AssetValidation.h"

class UTexture2D;

/** Which texture checks to run. */
struct FTextureValidatorSettings
{
	bool bCheckNaming = true;		// T_ prefix (VT_ for virtual textures) + known suffix
	bool bCheckSettings = true;		// sRGB/compression match what the suffix says
	bool bCheckResolution = true;	// power of two, no larger than MaxSize
	bool bCheckMips = true;			// mips, streaming, normal map texture group
	int32 MaxSize = 4096;			// not for virtual textures: being large is what they're for
	bool bCheckVirtual = true;		// VT_ textures are virtual, and have what virtual texturing needs
	int32 VirtualMinSize = 2048;	// smaller virtual textures only add tile overhead
};

/**
 * Checks texture assets against the project rules:
 * naming (T_ or VT_ prefix + known suffix), settings that match the suffix, resolution, mips/streaming,
 * and virtual texture requirements.
 * AssetValidation (Core_AssetValidation.h) finds the textures and runs this on each.
 */
namespace TextureValidator
{
	/** "_D, _BC, ..." for tooltips. */
	FString GetKnownSuffixList();

	/** Checks one loaded texture, adding what it finds to OutIssues. */
	void ValidateTexture(const FAssetData& Asset, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues);
}
