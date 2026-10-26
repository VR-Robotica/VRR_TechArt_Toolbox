// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_TexturePacker.h"
#include "ImageCore.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class UTexture2D;

/**
 * Toolbox "Packer" page: pick up to four textures and wire their channels into the R, G, B and A of a
 * new texture in a material-graph style view (STexturePackerGraph). The packing itself is in TexturePacker (Core_TexturePacker.h).
 */
class SMenu_TexturePacker : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_TexturePacker) {}
	SLATE_END_ARGS()

	static constexpr int32 NumSources = FTexturePackSettings::NumSources;
	static constexpr int32 NumChannels = FTexturePackSettings::NumChannels;

	/** Largest side of the preview image, in pixels. */
	static constexpr int32 PreviewMaxSize = 256;

	void Construct(const FArguments& InArgs);

private:
	// Inputs
	FTexturePackSettings Settings;

	// What each source slot holds: TexturePacker::ChannelBit flags, and why channels are missing
	uint8 SourceChannels[NumSources] = {};
	FText SourceNotes[NumSources];

	// Combo box options
	TArray<TSharedPtr<FString>> CompressionOptions;	// same order as ETexturePackCompression

	// Output settings
	FString OutputFileName = TEXT("T_Packed");
	FString OutputFolder = TEXT("/Game/Textures");	// project Content/Textures
	bool bFileNameManuallyEdited = false;

	// Preview
	enum class EPreviewMode : int32 { RGB, R, G, B, A };
	EPreviewMode PreviewMode = EPreviewMode::RGB;
	FImage PreviewSourceCache[NumSources];		// each source shrunk to preview size
	TStrongObjectPtr<UTexture2D> PreviewTexture;
	FSlateBrush PreviewBrush;
	FIntPoint OutputSize = FIntPoint::ZeroValue;

	// Layout
	TSharedRef<SWidget> CreateGraph();
	TSharedRef<SWidget> CreateOutputSettings();
	TSharedRef<SWidget> CreatePreview();

	// Labels
	FText GetCompressionText() const;

	// Behavior
	/** Why Pack can't run yet, or empty when it can. */
	FText GetPackBlocker() const;
	FReply OnPackClicked();
	void OnSourcesChanged();

	// Preview
	void RefreshPreviewSources();
	void UpdatePreview();
};
