// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_RebakeTexture.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

template <typename OptionType> class SComboBox;
class UTexture2D;

/**
 * Toolbox "Rebake Texture" page: re-bakes a texture made for one UV channel of a mesh into another channel
 * (for example from overlapping UV0 into a unique UV1 from UV Layouts). The work is in TextureRebaker (Core_RebakeTexture.h).
 * The source texture and the finished bake are previewed side by side with their settings.
 */
class SMenu_RebakeTexture : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SMenu_RebakeTexture) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// FGCObject: keeps the previewed textures loaded while their brushes draw them
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SMenu_RebakeTexture"); }

private:
	/** Side of the preview squares. */
	static constexpr float PreviewSize = 256.0f;

	TWeakObjectPtr<UObject> Mesh;
	TWeakObjectPtr<UTexture2D> Texture;
	int32 NumUVChannels = 0;

	/** The textures shown in the previews (the source, and the last bake) and their brushes. */
	TObjectPtr<UTexture2D> SourcePreviewTexture;
	TObjectPtr<UTexture2D> ResultPreviewTexture;
	FSlateBrush SourceBrush;
	FSlateBrush ResultBrush;

	FRebakeTextureSettings Settings;

	// Combo box options
	TArray<TSharedPtr<int32>> ChannelOptions;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> SourceCombo;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> TargetCombo;
	TArray<TSharedPtr<int32>> ResolutionOptions;
	TArray<TSharedPtr<int32>> SupersamplingOptions;
	TArray<TSharedPtr<ERebakeFilter>> FilterOptions;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout: Source (settings | source preview), Target (settings | bake preview), Compression, Save
	TSharedRef<SWidget> CreateSourceSection();
	TSharedRef<SWidget> CreateTargetSection();
	/** A preview square of the brush with a caption under it. */
	TSharedRef<SWidget> CreateTexturePreview(const FSlateBrush* Brush, TAttribute<bool> HasTexture, TAttribute<FText> Caption);
	/** Points a brush at a texture (or nothing), keeping it alive through OutHeld. */
	static void SetPreviewTexture(FSlateBrush& Brush, TObjectPtr<UTexture2D>& OutHeld, UTexture2D* NewTexture);
	FText GetResultCaption() const;
	TSharedRef<SWidget> CreateCompressionSection();
	TSharedRef<SWidget> CreateSaveSection();
	TSharedRef<SWidget> CreateChannelCombo(TSharedPtr<SComboBox<TSharedPtr<int32>>>& OutCombo, int32* Value);

	// Behavior
	void SetMesh(UObject* NewMesh);
	void SetTexture(UTexture2D* NewTexture);
	FText GetMeshInfoText() const;
	FText GetTextureInfoText() const;
	/** Why Rebake can't run yet, or empty when it can. */
	FText GetRebakeBlocker() const;
	FReply OnUseSelectedMeshClicked();
	FReply OnUseSelectedTextureClicked();
	FReply OnCopySettingsClicked();
	FReply OnRebakeClicked();
	void SetStatus(const FText& Text, bool bError);
};
