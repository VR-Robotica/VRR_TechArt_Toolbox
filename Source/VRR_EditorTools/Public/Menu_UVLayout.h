// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_UVLayout.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

template <typename OptionType> class SComboBox;
class SUVLayoutPreview;

/**
 * Toolbox "UV Layouts" page: builds a new non-overlapping UV channel (usually UV1) from a mesh's existing islands,
 * with even texel density and one region per material slot. The layout itself is in UVLayout (Core_UVLayout.h).
 */
class SMenu_UVLayout : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_UVLayout) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Mesh (a UStaticMesh or USkeletalMesh)
	TWeakObjectPtr<UObject> Mesh;
	int32 NumUVChannels = 0;
	int32 NumMaterialSlots = 0;

	FUVLayoutSettings Settings;

	// Combo box options
	TArray<TSharedPtr<int32>> SourceOptions;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> SourceCombo;
	TArray<TSharedPtr<int32>> TargetOptions;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> TargetCombo;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> BalancedTargetCombo;
	TArray<TSharedPtr<int32>> ResolutionOptions;
	TArray<TSharedPtr<int32>> BalancedResolutionOptions;
	TArray<TSharedPtr<int32>> PackDetailOptions;
	TArray<TSharedPtr<EUVPackMethod>> PackMethodOptions;

	/** One preview of LOD 0: the layout shown, the settings it was made with, and its widget. */
	struct FPreviewSlot
	{
		TSharedPtr<FUVLayoutPreview> Data;
		FUVLayoutSettings MadeWith;
		TSharedPtr<class SUVLayoutPreview> Widget;
	};
	/** The grouped material layout and the balanced layout, each shown in its own card. */
	FPreviewSlot LayoutPreview;
	FPreviewSlot BalancedPreview;
	FPreviewSlot& GetPreviewSlot(bool bBalanced) { return bBalanced ? BalancedPreview : LayoutPreview; }
	const FPreviewSlot& GetPreviewSlot(bool bBalanced) const { return bBalanced ? BalancedPreview : LayoutPreview; }

	FText StatusText;
	bool bStatusIsError = false;
	FText BalancedStatusText;
	bool bBalancedStatusIsError = false;

	/** Side of the preview square (each layout card's right column). */
	static constexpr float PreviewSize = 320.0f;

	// Layout: General Settings (full width), then Balanced and Grouped Material (settings | preview)
	TSharedRef<SWidget> CreateGeneralSection();
	TSharedRef<SWidget> CreateBalancedSection();
	TSharedRef<SWidget> CreateGroupedSection();
	TSharedRef<SWidget> CreateChannelCombo(TSharedPtr<SComboBox<TSharedPtr<int32>>>& OutCombo, TArray<TSharedPtr<int32>>* Options, int32* Value, bool bTarget);

	// Behavior
	void SetMesh(UObject* NewMesh);
	/** Rebuilds the channel combo options for the current mesh. */
	void RefreshChannelOptions();
	FText GetChannelLabel(int32 Channel, bool bTarget) const;
	/** Why Apply can't run yet, or empty when it can. */
	FText GetApplyBlocker() const;
	FText GetMeshInfoText() const;
	// Previews (bBalanced picks which one)
	TSharedRef<SWidget> CreatePreviewPanel(bool bBalanced);
	FText GetPreviewInfoText(bool bBalanced) const;
	bool IsPreviewStale(bool bBalanced) const;
	FUVLayoutSettings GetPreviewLayoutSettings(bool bBalanced) const;
	void UpdatePreview(bool bBalanced);
	void UpdatePreviews();
	FReply OnUseSelectionClicked();
	FReply OnApplyClicked();
	/** Balanced padding/resolution defaults for its current use (lightmap or not). */
	void ResetBalancedDefaults();
	/** Why Create Balanced UV Layout can't run yet, or empty when it can. */
	FText GetBalancedBlocker() const;
	FReply OnCreateBalancedClicked();
	void SetStatus(const FText& Text, bool bError);
};
