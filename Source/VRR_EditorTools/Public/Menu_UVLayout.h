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

	// Packing combo options (the int combos build their own, see VrrMenu::IntCombo)
	TArray<TSharedPtr<EUVPackMethod>> PackMethodOptions;

	/** The page's three previews, one per section, each in its own card beside the section's settings. */
	enum class EUVPreview : uint8 { Density, Balanced, Grouped, Num };

	/** One preview of LOD 0: the layout shown, the settings it was made with, and its widget. */
	struct FPreviewSlot
	{
		TSharedPtr<FUVLayoutPreview> Data;
		FUVLayoutSettings MadeWith;
		TSharedPtr<class SUVLayoutPreview> Widget;
	};
	FPreviewSlot Previews[static_cast<int32>(EUVPreview::Num)];
	FPreviewSlot& GetPreviewSlot(EUVPreview Which) { return Previews[static_cast<int32>(Which)]; }
	const FPreviewSlot& GetPreviewSlot(EUVPreview Which) const { return Previews[static_cast<int32>(Which)]; }

	FText StatusText;
	bool bStatusIsError = false;
	FText BalancedStatusText;
	bool bBalancedStatusIsError = false;
	FText DensityStatusText;
	bool bDensityStatusIsError = false;

	/** LOD 0's current density in the texel density channel, measured on mesh/channel change and after writes (not per frame). */
	FUVDensityInfo DensityInfo;

	/** Side of the preview square (inside each preview card). */
	static constexpr float PreviewSize = 320.0f;

	// Layout: General Settings (full width), then a row per section: [settings card] [preview card]
	TSharedRef<SWidget> CreateGeneralSection();
	TSharedRef<SWidget> CreateBalancedSection();
	TSharedRef<SWidget> CreateGroupedSection();
	TSharedRef<SWidget> CreateDensitySection();
	TSharedRef<SWidget> CreateChannelCombo(int32* Value, bool bTarget);

	// Behavior
	void SetMesh(UObject* NewMesh);
	/** The channels a source or target combo offers for the current mesh. */
	TArray<int32> GetChannelOptions(bool bTarget) const;
	FText GetChannelLabel(int32 Channel, bool bTarget) const;
	/** Why Apply can't run yet, or empty when it can. */
	FText GetApplyBlocker() const;
	FText GetMeshInfoText() const;
	// Previews (Which picks one)
	/** The preview card: square, info line, Update button and "Out of date" badge. */
	TSharedRef<SWidget> CreatePreviewCard(EUVPreview Which);
	FText GetPreviewInfoText(EUVPreview Which) const;
	bool IsPreviewStale(EUVPreview Which) const;
	FUVLayoutSettings GetPreviewLayoutSettings(EUVPreview Which) const;
	void UpdatePreview(EUVPreview Which);
	void UpdatePreviews();
	FReply OnApplyClicked();
	/** Balanced padding/resolution defaults for its current use (lightmap or not). */
	void ResetBalancedDefaults();
	/** Why Create Balanced UV Layout can't run yet, or empty when it can. */
	FText GetBalancedBlocker() const;
	FReply OnCreateBalancedClicked();
	// Texel density
	void RefreshDensityInfo();
	/** "Target ... / Now ..." line under the density settings. */
	FText GetDensityInfoText() const;
	FReply OnApplyDensityClicked();
	void SetStatus(const FText& Text, bool bError);
};
