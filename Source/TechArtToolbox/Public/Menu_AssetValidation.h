// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core_AssetValidation.h"
#include "Core_MaterialValidator.h"
#include "Core_MeshValidator.h"
#include "Core_TextureValidator.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * Toolbox "Asset Validation" page: scans a folder or the Content Browser selection with AssetValidation
 * (Core_AssetValidation.h), running the Texture, Mesh and Material Validation checks, and lists what each finds.
 */
class SMenu_AssetValidation : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_AssetValidation) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Scan options
	FString ScanFolder = TEXT("/Game");
	bool bIncludeSubfolders = true;

	// Sections: each can be turned off
	bool bValidateTextures = true;
	bool bValidateMeshes = true;
	bool bValidateMaterials = true;
	FTextureValidatorSettings TextureSettings;
	FMeshValidatorSettings MeshSettings;
	FMaterialValidatorSettings MaterialSettings;
	TArray<TSharedPtr<int32>> MaxSizeOptions;

	using EAssetType = FAssetIssue::EAssetType;
	bool IsValidating(EAssetType Type) const;
	/** The settings of the sections that are turned on. */
	FAssetValidationSettings GetValidationSettings() const;
	/** Just one section's settings (to check fixed assets again). */
	FAssetValidationSettings GetSectionSettings(EAssetType Type) const;
	FString ExportFolder;	// where Export List last saved

	// Results: one list per section (Texture, Mesh and Material Validation Results)
	enum class EIssueFilter : uint8 { All, Errors, Warnings };
	struct FResults
	{
		EIssueFilter Filter = EIssueFilter::All;
		TArray<TSharedPtr<FAssetIssue>> AllIssues;	// everything the last scan found for this asset type
		TArray<TSharedPtr<FAssetIssue>> Issues;		// AllIssues after the filter; the table's items
		TSharedPtr<SListView<TSharedPtr<FAssetIssue>>> List;
		int32 NumScanned = INDEX_NONE;	// INDEX_NONE until this section is scanned
		int32 NumErrors = 0;
		int32 NumWarnings = 0;
		int32 NumFixable = 0;	// issues Fix All can repair
	};
	FResults ResultLists[static_cast<int32>(EAssetType::Count)];
	FResults& GetResults(EAssetType Type) { return ResultLists[static_cast<int32>(Type)]; }
	const FResults& GetResults(EAssetType Type) const { return ResultLists[static_cast<int32>(Type)]; }

	// Layout
	TSharedRef<SWidget> CreateScanSection();
	TSharedRef<SWidget> CreateTextureSection();
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateMaterialSection();
	/** "Texture / Mesh / Material Validation Results": badges, filter, table. */
	TSharedRef<SWidget> CreateResultsSection(EAssetType Type);
	TSharedRef<class ITableRow> OnGenerateIssueRow(TSharedPtr<FAssetIssue> Issue, const TSharedRef<class STableViewBase>& OwnerTable);

	// Behavior
	FText GetScanBlocker() const;
	FReply OnScanFolderClicked();
	FReply OnScanSelectionClicked();
	void OnIssueSelected(TSharedPtr<FAssetIssue> Issue, ESelectInfo::Type SelectInfo);
	void OnIssueDoubleClicked(TSharedPtr<FAssetIssue> Issue);
	/** Splits a validation result into the per-section lists (a section that was off is reset to "not scanned"). */
	void ShowResult(const FAssetValidationResult& Result, const FAssetValidationSettings& Scanned);
	void ApplyFilter(FResults& Results);
	/** Recounts a section's errors, warnings and fixable issues, then filters it. */
	void RefreshSection(FResults& Results);
	/** Fixable issues among the rows selected in a section's list. */
	TArray<TSharedPtr<FAssetIssue>> GetSelectedFixable(EAssetType Type) const;
	/** Applies the section's fixable issues (all, or only the selected rows), then checks the fixed assets again. */
	FReply OnFixClicked(EAssetType Type, bool bSelectedOnly);
	/** Saves a section's issues (all of them, whatever the filter) as a JSON file. */
	FReply OnExportClicked(EAssetType Type);
	FText GetEmptyTitle(EAssetType Type) const;
	FText GetEmptyHint(EAssetType Type) const;
};
