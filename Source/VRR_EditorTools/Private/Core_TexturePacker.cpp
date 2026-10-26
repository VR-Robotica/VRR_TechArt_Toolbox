// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Texture packing: builds one texture whose R, G, B and A each come from a channel of another texture
 * (or a constant), e.g. Occlusion/Roughness/Metallic -> one _ORM mask. One sample instead of three.
 *
 * Process
 *  1. The output is as big as the largest source used; smaller sources are resized up to it.
 *  2. Sources are read from their imported source data (not the compressed GPU copy) as BGRA8,
 *     treated as linear so values copy bit for bit (no sRGB conversion on the way through).
 *  3. CombineChannels copies each picked source channel into its output channel (optionally inverted),
 *     or fills it with the constant when no source is picked.
 *  4. A new UTexture2D gets the pixels plus compression/sRGB settings that match the chosen output type.
 * The page preview runs the same CombineChannels on small cached copies of the sources, so it matches the result.
 */

#include "Core_TexturePacker.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"

#define LOCTEXT_NAMESPACE "TexturePacker"

// SETTINGS
/** Grayscale keeps only R and Normal only RGB; the other channels are filled automatically. */
bool FTexturePackSettings::IsChannelEnabled(int32 ChannelIndex) const
{
	switch (Compression)
	{
	case ETexturePackCompression::Grayscale:	return ChannelIndex == 0;
	case ETexturePackCompression::Normal:		return ChannelIndex < 3;
	default:									return true;
	}
}

/** True when the source slot has a texture and at least one enabled output channel reads from it. */
bool FTexturePackSettings::IsSourceUsed(int32 SourceIndex) const
{
	if (!Sources[SourceIndex].IsValid() || !Sources[SourceIndex]->Source.IsValid())
	{
		return false;
	}

	for (int32 Out = 0; Out < NumChannels; ++Out)
	{
		if (IsChannelEnabled(Out) && Channels[Out].SourceIndex == SourceIndex)
		{
			return true;
		}
	}
	return false;
}

/** Output size: the largest width and height among the used sources (zero when none is used). */
FIntPoint FTexturePackSettings::GetOutputSize() const
{
	FIntPoint Size = FIntPoint::ZeroValue;
	for (int32 i = 0; i < NumSources; ++i)
	{
		if (IsSourceUsed(i))
		{
			Size.X = FMath::Max(Size.X, Sources[i]->Source.GetSizeX());
			Size.Y = FMath::Max(Size.Y, Sources[i]->Source.GetSizeY());
		}
	}
	return Size;
}

namespace TexturePacker
{
	/** Reads mip 0 of the texture source data as linear BGRA8 at Width x Height (resizing if needed). */
	bool ReadSourceImage(UTexture2D* Source, int32 Width, int32 Height, FImage& OutImage)
	{
		FImage MipImage;
		if (!Source || !Source->Source.GetMipImage(MipImage, 0))
		{
			return false;
		}

		// Treat stored values as-is so channels copy exactly (no sRGB conversion)
		MipImage.GammaSpace = EGammaSpace::Linear;

		if (MipImage.SizeX == Width && MipImage.SizeY == Height)
		{
			MipImage.CopyTo(OutImage, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		}
		else
		{
			MipImage.ResizeTo(OutImage, Width, Height, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		}
		return true;
	}

	/**
	 * The core of the packer: writes the packed BGRA8 pixels.
	 *  1. For each output channel, copy the picked source channel (or the constant) into it, inverting if asked.
	 *  2. Fill the channels the compression ignores, so the saved texture is clean.
	 * Sources must already be Width x Height (null = slot unused).
	 */
	void CombineChannels(const FTexturePackSettings& Settings, const FImage* const Sources[FTexturePackSettings::NumSources], int32 Width, int32 Height, TArray<uint8>& OutBGRA)
	{
		const int32 NumPixels = Width * Height;
		OutBGRA.SetNumUninitialized(NumPixels * 4);

		// 1. One pass per output channel. BGRAOffset maps R/G/B/A to the byte order in memory (B, G, R, A)
		for (int32 Out = 0; Out < FTexturePackSettings::NumChannels; ++Out)
		{
			const FPackedChannel& Channel = Settings.Channels[Out];
			const FImage* Source = Channel.SourceIndex != INDEX_NONE ? Sources[Channel.SourceIndex] : nullptr;
			const uint8* SourceData = Source ? Source->RawData.GetData() : nullptr;
			const int32 SourceOffset = BGRAOffset[Channel.SourceChannel];
			const int32 DestOffset = BGRAOffset[Out];
			const uint8 Constant = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(Channel.DefaultValue, 0.0f, 1.0f) * 255.0f));

			for (int32 Pixel = 0; Pixel < NumPixels; ++Pixel)
			{
				const uint8 Value = SourceData ? SourceData[Pixel * 4 + SourceOffset] : Constant;
				OutBGRA[Pixel * 4 + DestOffset] = Channel.bInvert ? 255 - Value : Value;
			}
		}

		// 2. Fill channels the compression doesn't use
		const ETexturePackCompression Compression = Settings.Compression;
		if (Compression == ETexturePackCompression::Grayscale || Compression == ETexturePackCompression::Normal)
		{
			for (int32 Pixel = 0; Pixel < NumPixels; ++Pixel)
			{
				uint8* P = &OutBGRA[Pixel * 4];
				if (Compression == ETexturePackCompression::Grayscale)
				{
					// Copy R into G and B so the gray conversion keeps the R value
					P[0] = P[1] = P[2];
				}
				P[3] = 255;
			}
		}
	}

	/** Small copy of a source (longest side <= MaxSize) for the live preview. The page caches it, so changing a channel doesn't reload it. */
	void ReadPreviewSource(UTexture2D* Source, int32 MaxSize, FImage& OutImage)
	{
		OutImage = FImage();
		if (!Source || !Source->Source.IsValid())
		{
			return;
		}

		const int32 SizeX = Source->Source.GetSizeX();
		const int32 SizeY = Source->Source.GetSizeY();
		const float Scale = FMath::Min(1.0f, static_cast<float>(MaxSize) / FMath::Max(SizeX, SizeY));
		ReadSourceImage(Source,
			FMath::Max(1, FMath::RoundToInt(SizeX * Scale)),
			FMath::Max(1, FMath::RoundToInt(SizeY * Scale)),
			OutImage);
	}

	/** True when every pixel of a BGRA8 image has (almost) equal R, G and B. */
	static bool IsGrayImage(const FImage& Image)
	{
		if (Image.Format != ERawImageFormat::BGRA8 || Image.RawData.IsEmpty())
		{
			return false;
		}

		// A little slack for JPEG noise in gray images saved as color
		constexpr int32 Tolerance = 2;
		const uint8* Data = Image.RawData.GetData();
		const int64 NumPixels = Image.GetNumPixels();
		for (int64 Pixel = 0; Pixel < NumPixels; ++Pixel)
		{
			const uint8* P = Data + Pixel * 4;
			if (FMath::Abs(P[0] - P[2]) > Tolerance || FMath::Abs(P[1] - P[2]) > Tolerance)
			{
				return false;
			}
		}
		return true;
	}

	/**
	 * Channels a source really has:
	 *  1. single-channel source formats (G8, G16, R16F, R32F) only have R,
	 *  2. single-channel compressions keep only R (Grayscale, Alpha, Half/Single Float) or only A (Displacement, Distance Field Font),
	 *  3. otherwise R, G, B, reduced to R when the image is gray,
	 *  4. plus A when the format and compression keep alpha and it isn't fully opaque.
	 */
	uint8 GetAvailableChannels(const UTexture2D* Source, const FImage& Preview, FText& OutNote)
	{
		OutNote = FText::GetEmpty();
		if (!Source || !Source->Source.IsValid())
		{
			return 0;
		}

		const uint8 R = ChannelBit(0);
		const uint8 RGB = ChannelBit(0) | ChannelBit(1) | ChannelBit(2);
		const uint8 A = ChannelBit(3);

		// 1. Single-channel source data
		const ETextureSourceFormat Format = Source->Source.GetFormat();
		if (Format == TSF_G8 || Format == TSF_G16 || Format == TSF_R16F || Format == TSF_R32F)
		{
			OutNote = LOCTEXT("NoteSingleChannelSource", "Single-channel (grayscale) source image: only R");
			return R;
		}

		// 2. Single-channel compression
		const TextureCompressionSettings Compression = Source->CompressionSettings;
		switch (Compression)
		{
		case TC_Grayscale:
		case TC_Alpha:
		case TC_HalfFloat:
		case TC_SingleFloat:
			OutNote = LOCTEXT("NoteSingleChannelCompression", "Its compression keeps only the R channel");
			return R;
		case TC_Displacementmap:
		case TC_DistanceFieldFont:
			OutNote = LOCTEXT("NoteAlphaCompression", "Its compression keeps only the A channel");
			return A;
		default:
			break;
		}

		// 3. Color channels, or R alone for a gray image
		uint8 Mask = RGB;
		TArray<FText> Notes;
		if (IsGrayImage(Preview))
		{
			Mask = R;
			Notes.Add(LOCTEXT("NoteGrayImage", "R, G and B are equal (grayscale image): only R"));
		}

		// 4. Alpha
		if (Format == TSF_BGRE8)
		{
			Notes.Add(LOCTEXT("NoteNoAlphaFormat", "No alpha channel in the source image"));
		}
		else if (Compression == TC_Normalmap || Compression == TC_HDR_Compressed)
		{
			Notes.Add(LOCTEXT("NoteNoAlphaCompression", "Its compression has no alpha"));
		}
		else if (Source->CompressionNoAlpha)
		{
			Notes.Add(LOCTEXT("NoteCompressionNoAlpha", "Compression No Alpha is on"));
		}
		else if (!Preview.RawData.IsEmpty() && !FImageCore::DetectAlphaChannel(Preview))
		{
			Notes.Add(LOCTEXT("NoteOpaqueAlpha", "Alpha is fully opaque (no alpha data)"));
		}
		else
		{
			Mask |= A;
		}

		OutNote = FText::Join(INVTEXT("\n"), Notes);
		return Mask;
	}

	/** "RGBA", "RGB", "Grayscale", "Alpha", or the letters of whatever is set. */
	FText DescribeChannels(uint8 Mask)
	{
		if (Mask == ChannelBit(0))
		{
			return LOCTEXT("ChannelsGray", "Grayscale");
		}
		if (Mask == ChannelBit(3))
		{
			return LOCTEXT("ChannelsAlpha", "Alpha");
		}

		static const TCHAR* Letters[4] = { TEXT("R"), TEXT("G"), TEXT("B"), TEXT("A") };
		FString Text;
		for (int32 Channel = 0; Channel < 4; ++Channel)
		{
			if (Mask & ChannelBit(Channel))
			{
				Text += Letters[Channel];
			}
		}
		return FText::FromString(Text);
	}

	/**
	 * Preview pixels for the page:
	 *  1. scale the output size down to MaxSize, keeping its aspect ratio,
	 *  2. resize each cached source to that size,
	 *  3. pack them with CombineChannels (same code as the real output),
	 *  4. show RGB opaque, or a single channel (ViewChannel 0-3) as grayscale.
	 */
	bool BuildPreview(const FTexturePackSettings& Settings, const FImage (&PreviewSources)[FTexturePackSettings::NumSources], int32 MaxSize, int32 ViewChannel,
		TArray<uint8>& OutBGRA, FIntPoint& OutSize)
	{
		const FIntPoint OutputSize = Settings.GetOutputSize();
		if (OutputSize.X == 0 || OutputSize.Y == 0)
		{
			return false;
		}

		// 1. Preview keeps the output's aspect ratio
		const float Scale = FMath::Min(1.0f, static_cast<float>(MaxSize) / FMath::Max(OutputSize.X, OutputSize.Y));
		const int32 Width = FMath::Max(1, FMath::RoundToInt(OutputSize.X * Scale));
		const int32 Height = FMath::Max(1, FMath::RoundToInt(OutputSize.Y * Scale));

		// 2. Match each cached source to the preview size
		FImage Resized[FTexturePackSettings::NumSources];
		const FImage* Sources[FTexturePackSettings::NumSources] = {};
		for (int32 i = 0; i < FTexturePackSettings::NumSources; ++i)
		{
			const FImage& Cached = PreviewSources[i];
			if (!Settings.IsSourceUsed(i) || Cached.RawData.IsEmpty())
			{
				continue;
			}

			if (Cached.SizeX == Width && Cached.SizeY == Height)
			{
				Sources[i] = &Cached;
			}
			else
			{
				Cached.ResizeTo(Resized[i], Width, Height, ERawImageFormat::BGRA8, EGammaSpace::Linear);
				Sources[i] = &Resized[i];
			}
		}

		// 3. Same packing as the real texture
		CombineChannels(Settings, Sources, Width, Height, OutBGRA);

		// 4. Show RGB opaque, or one channel as grayscale
		for (int32 Pixel = 0; Pixel < Width * Height; ++Pixel)
		{
			uint8* P = &OutBGRA[Pixel * 4];
			if (ViewChannel != INDEX_NONE)
			{
				const uint8 Value = P[BGRAOffset[FMath::Clamp(ViewChannel, 0, 3)]];
				P[0] = P[1] = P[2] = Value;
			}
			P[3] = 255;
		}

		OutSize = FIntPoint(Width, Height);
		return true;
	}

	/** Output name from the sources' shared prefix: T_Rock_R + T_Rock_M -> T_Rock_Packed. */
	FString SuggestName(const FTexturePackSettings& Settings)
	{
		TArray<FString> InputNames;
		for (const TWeakObjectPtr<UTexture2D>& Source : Settings.Sources)
		{
			if (Source.IsValid())
			{
				InputNames.Add(Source->GetName());
			}
		}

		if (InputNames.Num() == 0)
		{
			return FString();
		}

		// Find common prefix
		FString CommonPrefix = InputNames[0];
		for (int32 i = 1; i < InputNames.Num(); ++i)
		{
			const FString& CurrentName = InputNames[i];
			const int32 MaxLen = FMath::Min(CommonPrefix.Len(), CurrentName.Len());
			int32 CommonLen = 0;
			while (CommonLen < MaxLen && CommonPrefix[CommonLen] == CurrentName[CommonLen])
			{
				++CommonLen;
			}
			CommonPrefix = CommonPrefix.Left(CommonLen);
		}

		FString BaseName = CommonPrefix.Len() >= 3 ? CommonPrefix : InputNames[0];

		// Enforce "T_" prefix
		if (!BaseName.StartsWith(TEXT("T_")))
		{
			BaseName = TEXT("T_") + BaseName;
		}

		// Remove trailing underscores
		while (BaseName.EndsWith(TEXT("_")))
		{
			BaseName.LeftChopInline(1);
		}

		return BaseName + TEXT("_Packed");
	}

	/** Packs the sources into a new texture asset in Folder (Name, or Name_01... if taken). Null + OutError on failure. */
	UTexture2D* CreatePackedTexture(const FTexturePackSettings& Settings, const FString& Folder, const FString& Name, FText& OutError)
	{
		// 1. Output size = largest source used
		const FIntPoint Size = Settings.GetOutputSize();
		if (Size.X == 0 || Size.Y == 0)
		{
			OutError = LOCTEXT("NothingToPack", "Pick a source texture for at least one channel");
			return nullptr;
		}

		// 2. Read each used source at full output size
		FImage SourceImages[FTexturePackSettings::NumSources];
		const FImage* Sources[FTexturePackSettings::NumSources] = {};
		for (int32 i = 0; i < FTexturePackSettings::NumSources; ++i)
		{
			if (!Settings.IsSourceUsed(i))
			{
				continue;
			}

			if (!ReadSourceImage(Settings.Sources[i].Get(), Size.X, Size.Y, SourceImages[i]))
			{
				OutError = FText::Format(LOCTEXT("ReadFailed", "Could not read {0}"), FText::FromString(Settings.Sources[i]->GetName()));
				return nullptr;
			}
			Sources[i] = &SourceImages[i];
		}

		// 3. Build the packed pixels
		TArray<uint8> PackedData;
		CombineChannels(Settings, Sources, Size.X, Size.Y, PackedData);

		// 4-5. Create the texture asset at the first free name in the output folder (registered with the Content Browser)
		UTexture2D* NewTexture = VrrCore::FindOrCreateTexture(Folder, Name, false, OutError);
		if (!NewTexture)
		{
			return nullptr;
		}

		NewTexture->PreEditChange(nullptr);
		NewTexture->Source.Init(Size.X, Size.Y, 1, 1, TSF_BGRA8, PackedData.GetData());

		// 6. Compression matching the output type, so the engine stores the data the right way
		const ETexturePackCompression Compression = Settings.Compression;
		switch (Compression)
		{
		case ETexturePackCompression::Grayscale:	NewTexture->CompressionSettings = TC_Grayscale; break;
		case ETexturePackCompression::Diffuse:		NewTexture->CompressionSettings = TC_Default;   break;
		case ETexturePackCompression::Normal:		NewTexture->CompressionSettings = TC_Normalmap; break;
		default:									NewTexture->CompressionSettings = TC_Masks;     break;
		}

		// 7. Only color (diffuse) data is sRGB; masks, grayscale data and normals are linear
		NewTexture->SRGB = Compression == ETexturePackCompression::Diffuse;

		// 8. Skip alpha in compression when it's unused or a plain opaque constant
		const FPackedChannel& Alpha = Settings.Channels[3];
		const bool bAlphaUnused = !Settings.IsChannelEnabled(3)
			|| (Alpha.SourceIndex == INDEX_NONE && FMath::IsNearlyEqual(Alpha.bInvert ? 1.0f - Alpha.DefaultValue : Alpha.DefaultValue, 1.0f));
		NewTexture->CompressionNoAlpha = bAlphaUnused;

		// 9. Build the platform data and mark it unsaved
		NewTexture->PostEditChange();
		NewTexture->MarkPackageDirty();

		return NewTexture;
	}
}

#undef LOCTEXT_NAMESPACE
