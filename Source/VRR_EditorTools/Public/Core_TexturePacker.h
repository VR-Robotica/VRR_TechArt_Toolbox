// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "ImageCore.h"

class UTexture2D;

/** Where one output channel (R, G, B or A) gets its value from. */
struct FPackedChannel
{
	/** Index into the source textures, or INDEX_NONE to use DefaultValue. */
	int32 SourceIndex = INDEX_NONE;

	/** 0 = R, 1 = G, 2 = B, 3 = A of the source texture. */
	int32 SourceChannel = 0;

	bool bInvert = false;

	/** 0-1 value used when no source is picked. */
	float DefaultValue = 0.0f;
};

/** Compression of the packed texture. Grayscale keeps R only; Normal keeps RGB. */
enum class ETexturePackCompression : int32 { Masks, Grayscale, Diffuse, Normal };

/** Up to four source textures and which of their channels go into the R, G, B and A of the result. */
struct FTexturePackSettings
{
	static constexpr int32 NumSources = 4;
	static constexpr int32 NumChannels = 4;

	TWeakObjectPtr<UTexture2D> Sources[NumSources];
	FPackedChannel Channels[NumChannels];
	ETexturePackCompression Compression = ETexturePackCompression::Masks;

	FTexturePackSettings()
	{
		// Alpha defaults to opaque, color channels to black
		Channels[3].DefaultValue = 1.0f;
	}

	/** Whether the compression keeps this output channel. */
	bool IsChannelEnabled(int32 ChannelIndex) const;

	/** Whether a kept channel reads from this source (and the source has data). */
	bool IsSourceUsed(int32 SourceIndex) const;

	/** Largest width and height of the sources in use; zero when none is used. */
	FIntPoint GetOutputSize() const;
};

/** Packs channels of several textures into one new texture asset. */
namespace TexturePacker
{
	/** Byte offset of R, G, B, A inside a BGRA8 pixel. */
	constexpr int32 BGRAOffset[4] = { 2, 1, 0, 3 };

	/** Source mip 0 as BGRA8 at this size, values copied as stored (no sRGB conversion). */
	bool ReadSourceImage(UTexture2D* Source, int32 Width, int32 Height, FImage& OutImage);

	/** Builds BGRA8 pixels from the sources (each already Width x Height, or null). */
	void CombineChannels(const FTexturePackSettings& Settings, const FImage* const Sources[FTexturePackSettings::NumSources], int32 Width, int32 Height, TArray<uint8>& OutBGRA);

	/** A source shrunk so its longest side is at most MaxSize (for fast previews). Empty when it has no data. */
	void ReadPreviewSource(UTexture2D* Source, int32 MaxSize, FImage& OutImage);

	/** Channel bits for GetAvailableChannels: bit 0 = R, 1 = G, 2 = B, 3 = A. */
	constexpr uint8 ChannelBit(int32 Channel) { return static_cast<uint8>(1u << Channel); }

	/**
	 * Which channels of a source hold data, as ChannelBit flags (0 when there is no texture).
	 * Looks at the source format and compression settings, and at Preview (from ReadPreviewSource) to spot
	 * gray images (R = G = B, so only R counts) and alpha that is fully opaque. OutNote says why channels are missing.
	 */
	uint8 GetAvailableChannels(const UTexture2D* Source, const FImage& Preview, FText& OutNote);

	/** Short label for a channel mask: "RGBA", "RGB", "Grayscale", "Alpha"... Empty for 0. */
	FText DescribeChannels(uint8 Mask);

	/**
	 * Preview pixels (BGRA8, opaque) at most MaxSize on the longest side, from sources made by ReadPreviewSource.
	 * ViewChannel: INDEX_NONE for RGB, 0-3 to show one channel as grayscale. Returns false when nothing is used.
	 */
	bool BuildPreview(const FTexturePackSettings& Settings, const FImage (&PreviewSources)[FTexturePackSettings::NumSources], int32 MaxSize, int32 ViewChannel,
		TArray<uint8>& OutBGRA, FIntPoint& OutSize);

	/** T_<common prefix of the source names>_Packed, or empty when no source is set. */
	FString SuggestName(const FTexturePackSettings& Settings);

	/** Creates the packed texture asset (a free name in Folder, numbered if taken). Null with an error on failure. */
	UTexture2D* CreatePackedTexture(const FTexturePackSettings& Settings, const FString& Folder, const FString& Name, FText& OutError);
}
