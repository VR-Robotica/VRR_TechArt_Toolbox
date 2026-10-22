// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_BakeNormals.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

template <typename OptionType> class SComboBox;

/**
 * Toolbox "Normals" page: bakes smooth normals for toon outlines into vertex colors or a texture.
 * The baking itself is in SmoothNormalBaker (Core_BakeNormals.h).
 */
class SMenu_BakeNormals : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_BakeNormals) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Mesh (a UStaticMesh or USkeletalMesh)
	TWeakObjectPtr<UObject> Mesh;
	int32 NumUVChannels = 0;
	bool bMeshHasVertexColors = false;

	FSmoothNormalBakeSettings Settings;

	// Combo box options
	TArray<TSharedPtr<int32>> UVChannelOptions;
	TSharedPtr<SComboBox<TSharedPtr<int32>>> UVChannelCombo;
	TArray<TSharedPtr<int32>> ResolutionOptions;
	TSharedPtr<int32> Resolution;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateSmoothingSection();
	TSharedRef<SWidget> CreateMaskSection();
	TSharedRef<SWidget> CreateEncodingSection();
	TSharedRef<SWidget> CreateOutputSection();
	TSharedRef<SWidget> CreateBakeSection();

	// Behavior
	void SetMesh(UObject* NewMesh);
	bool IsSkeletal() const;
	/** Why Bake can't run yet, or empty when it can. */
	FText GetBakeBlocker() const;
	FText GetMeshInfoText() const;
	FReply OnUseSelectionClicked();
	FReply OnBakeClicked();
	void SetStatus(const FText& Text, bool bError);
};
