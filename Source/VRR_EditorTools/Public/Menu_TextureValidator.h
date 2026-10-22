// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_TextureValidator.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Views/SListView.h"

/**
 * Toolbox "Validate" page: scans a folder or the Content Browser selection with TextureValidator
 * (Core_TextureValidator.h) and lists what it finds.
 */
class SMenu_TextureValidator : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_TextureValidator) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Scan options
	FString ScanFolder = TEXT("/Game");
	bool bIncludeSubfolders = true;

	// Checks
	FTextureValidatorSettings Settings;
	TArray<TSharedPtr<int32>> MaxSizeOptions;

	// Results
	enum class EIssueFilter : uint8 { All, Errors, Warnings };
	EIssueFilter IssueFilter = EIssueFilter::All;
	TArray<TSharedPtr<FTextureIssue>> AllIssues;	// everything the last scan found
	TArray<TSharedPtr<FTextureIssue>> Issues;		// AllIssues after the filter; the table's items
	TSharedPtr<SListView<TSharedPtr<FTextureIssue>>> IssueList;
	int32 NumScanned = INDEX_NONE;	// INDEX_NONE until the first scan
	int32 NumErrors = 0;
	int32 NumWarnings = 0;

	// Layout
	TSharedRef<SWidget> CreateScanSection();
	TSharedRef<SWidget> CreateChecksSection();
	TSharedRef<SWidget> CreateResultsSection();
	TSharedRef<class ITableRow> OnGenerateIssueRow(TSharedPtr<FTextureIssue> Issue, const TSharedRef<class STableViewBase>& OwnerTable);

	// Behavior
	FReply OnScanFolderClicked();
	FReply OnScanSelectionClicked();
	void OnIssueSelected(TSharedPtr<FTextureIssue> Issue, ESelectInfo::Type SelectInfo);
	void OnIssueDoubleClicked(TSharedPtr<FTextureIssue> Issue);
	void ShowResult(const FTextureValidationResult& Result);
	void ApplyFilter();
	FText GetEmptyTitle() const;
	FText GetEmptyHint() const;
};
