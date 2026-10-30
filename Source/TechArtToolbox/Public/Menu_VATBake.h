// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Core_VATBake.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

class USkeletalMesh;
class ITableRow;
class STableViewBase;

/** One animation in the page's list, ticked or not. */
struct FVatAnimItem
{
	FAssetData Asset;
	bool bSelected = false;
};

/**
 * Toolbox "VAT" page: bakes Vertex Animation Textures with Unreal's AnimToTexture plugin, for every LOD.
 * Pick a skeletal mesh, tick its animations and set their order, choose Bone or Vertex mode and the Data
 * Asset settings, then Create (a static mesh and Data Asset per LOD) and Bake VAT (textures per LOD, one static
 * mesh with all LODs, material instances of the chosen master material, and a preview actor).
 * The asset work is in VatBaker (Core_VATBake.h).
 */
class SMenu_VATBake : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_VATBake) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<USkeletalMesh> Mesh;
	FVatBakeSettings Settings;

	/** What the last Create made; Bake VAT works on it. */
	FVatAssetSet CreatedSet;

	/** Every animation the mesh's skeleton can play, and the ones the search box lets through (what the list shows). */
	TArray<TSharedPtr<FVatAnimItem>> AllAnims;
	TArray<TSharedPtr<FVatAnimItem>> ShownAnims;
	FString SearchText;
	TSharedPtr<SListView<TSharedPtr<FVatAnimItem>>> AnimList;

	/** The ticked animations, in the order Create writes them to the Data Asset's animation array. */
	TArray<TSharedPtr<FVatAnimItem>> BakeOrder;
	TSharedPtr<SListView<TSharedPtr<FVatAnimItem>>> OrderList;

	/** The active master material's fit for its mode (from RefreshMaterialInfo). */
	FText MaterialFitText;
	bool bMaterialHasProblem = false;
	int32 MaterialAnimationSlots = 0;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateAnimationsSection();
	TSharedRef<SWidget> CreateOrderSection();
	TSharedRef<SWidget> CreateDataAssetSection();
	TSharedRef<SWidget> CreateOutputSection();
	TSharedRef<SWidget> CreateMaterialSection();
	TSharedRef<SWidget> CreateBuildSection();
	TSharedRef<ITableRow> OnGenerateAnimRow(TSharedPtr<FVatAnimItem> Item, const TSharedRef<STableViewBase>& OwnerTable);
	TSharedRef<ITableRow> OnGenerateOrderRow(TSharedPtr<FVatAnimItem> Item, const TSharedRef<STableViewBase>& OwnerTable);

	// Behavior
	void SetMesh(USkeletalMesh* NewMesh);
	void RefreshShownAnims();
	void SetShownSelected(bool bSelected);
	void SetItemSelected(const TSharedPtr<FVatAnimItem>& Item, bool bSelected);
	void MoveInOrder(const TSharedPtr<FVatAnimItem>& Item, int32 NewIndex);
	void SortOrderByName();
	void RefreshOrderList();
	int32 CountSelected() const;
	FText GetMeshInfo() const;
	FText GetOutputPreview() const;
	FText GetMaterialInfo() const;
	/** Re-reads the active master material's parameters (on material or mode change). */
	void RefreshMaterialInfo();
	FText GetCreatedSetInfo() const;
	FReply OnUseSelectedAnimsClicked();
	FReply OnCreateClicked();
	FReply OnBakeClicked();
	void ReportResult(const FVatResult& Result, const FText& SuccessToast);
	void SetStatus(const FText& Text, bool bError);
};
