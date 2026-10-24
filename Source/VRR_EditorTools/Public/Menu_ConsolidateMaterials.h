// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_ConsolidateMaterials.h"
#include "Input/Reply.h"
#include "Styling/SlateBrush.h"
#include "UObject/GCObject.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class SWrapBox;
class UTexture2D;

/**
 * Toolbox "Consolidate Materials" page: bakes the outputs of every material on a multi-material mesh
 * (Base Color, Metallic, Roughness, Normal...) through one UV channel into a single texture set.
 * The work is in MaterialConsolidator (Core_ConsolidateMaterials.h). The finished textures are previewed under the button.
 */
class SMenu_ConsolidateMaterials : public SCompoundWidget, public FGCObject
{
public:
	SLATE_BEGIN_ARGS(SMenu_ConsolidateMaterials) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// FGCObject: keeps the previewed textures loaded while their brushes draw them
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("SMenu_ConsolidateMaterials"); }

private:
	/** Side of each result preview. */
	static constexpr float PreviewSize = 128.0f;

	TWeakObjectPtr<UObject> Mesh;
	int32 NumUVChannels = 0;
	TArray<FConsolidateSlot> Slots;

	FConsolidateSettings Settings;

	/** The mesh's slots, rebuilt when the mesh changes. */
	TSharedPtr<SVerticalBox> SlotList;

	/** The last run's textures and their preview brushes. */
	TArray<TObjectPtr<UTexture2D>> ResultTextures;
	TArray<TSharedPtr<FSlateBrush>> ResultBrushes;
	TSharedPtr<SWrapBox> ResultPreviews;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout: Mesh (picker and slots), Target, Outputs, Save (button, status and previews)
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateTargetSection();
	TSharedRef<SWidget> CreateMapsSection();
	TSharedRef<SWidget> CreateSaveSection();

	// Behavior
	void SetMesh(UObject* NewMesh);
	void RefreshSlotList();
	void RefreshResultPreviews();
	FText GetMeshInfoText() const;
	FText GetOutputNamesText() const;
	/** Why Consolidate can't run yet, or empty when it can. */
	FText GetConsolidateBlocker() const;
	FReply OnConsolidateClicked();
	void SetStatus(const FText& Text, bool bError);
};
