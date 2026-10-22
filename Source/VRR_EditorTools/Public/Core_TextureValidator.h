// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

class UTexture2D;

/** One problem found on one texture. */
struct FTextureIssue
{
	enum class ESeverity : uint8 { Warning, Error };

	FAssetData Asset;
	ESeverity Severity = ESeverity::Warning;
	FText Rule;
	FText Message;
};

/** Which checks to run. */
struct FTextureValidatorSettings
{
	bool bCheckNaming = true;		// T_ prefix + known suffix
	bool bCheckSettings = true;		// sRGB/compression match what the suffix says
	bool bCheckResolution = true;	// power of two, no larger than MaxSize
	bool bCheckMips = true;			// mips, streaming, normal map texture group
	int32 MaxSize = 4096;
};

struct FTextureValidationResult
{
	TArray<FTextureIssue> Issues;	// errors first, then by texture name
	int32 NumScanned = 0;
	int32 NumErrors = 0;
	int32 NumWarnings = 0;
};

/**
 * Checks texture assets against the project rules:
 * naming (T_ prefix + known suffix), settings that match the suffix, resolution, and mips/streaming.
 */
namespace TextureValidator
{
	/** "_D, _BC, ..." for tooltips. */
	FString GetKnownSuffixList();

	/** Every texture in a content folder. */
	TArray<FAssetData> FindTextures(const FString& Folder, bool bIncludeSubfolders);

	/** Only the textures among these assets. */
	TArray<FAssetData> FilterTextures(const TArray<FAssetData>& Assets);

	/** Loads and checks each texture (with a cancelable progress dialog). */
	FTextureValidationResult Validate(const TArray<FAssetData>& Assets, const FTextureValidatorSettings& Settings);

	/** Checks one loaded texture, adding what it finds to OutIssues. */
	void ValidateTexture(const FAssetData& Asset, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, TArray<FTextureIssue>& OutIssues);
}
