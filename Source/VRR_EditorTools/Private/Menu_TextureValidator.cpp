// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Validate page (UI only).
 *
 * Scans a folder or the Content Browser selection with TextureValidator::Validate (Core_TextureValidator.cpp)
 * and lists the issues in a table (errors first). Click a row to find the texture, double-click to open it.
 */

#include "Menu_TextureValidator.h"

#include "ContentBrowserModule.h"
#include "Core_Common.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "MenuWidgets.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SHeaderRow.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SMenu_TextureValidator"

/** Column ids of the results table. */
namespace TextureValidatorColumns
{
	const FName Severity("Severity");
	const FName Texture("Texture");
	const FName Rule("Rule");
	const FName Issue("Issue");
}

/** One row of the results table. */
class STextureIssueRow : public SMultiColumnTableRow<TSharedPtr<FTextureIssue>>
{
public:
	SLATE_BEGIN_ARGS(STextureIssueRow) {}
		SLATE_ARGUMENT(TSharedPtr<FTextureIssue>, Issue)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Issue = InArgs._Issue;
		SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		namespace Columns = TextureValidatorColumns;
		const bool bError = Issue->Severity == FTextureIssue::ESeverity::Error;

		if (ColumnName == Columns::Severity)
		{
			return SNew(SBox)
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ToolTipText(bError ? LOCTEXT("Error", "Error") : LOCTEXT("Warning", "Warning"))
				[
					SNew(SImage).Image(FAppStyle::GetBrush(bError ? TEXT("Icons.ErrorWithColor") : TEXT("Icons.WarningWithColor")))
				];
		}

		FText Text;
		if (ColumnName == Columns::Texture)		Text = FText::FromName(Issue->Asset.AssetName);
		else if (ColumnName == Columns::Rule)	Text = Issue->Rule;
		else if (ColumnName == Columns::Issue)	Text = Issue->Message;

		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(Text)
				.ToolTipText(ColumnName == Columns::Texture ? FText::FromString(Issue->Asset.GetObjectPathString()) : Text)
			];
	}

private:
	TSharedPtr<FTextureIssue> Issue;
};

/** Builds the page: scan, checks and results cards. */
void SMenu_TextureValidator::Construct(const FArguments& InArgs)
{
	for (int32 Size : { 1024, 2048, 4096, 8192 })
	{
		MaxSizeOptions.Add(MakeShared<int32>(Size));
	}
	Settings.MaxSize = 4096;

	ChildSlot
	[
		VrrMenu::Stack({
			CreateScanSection(),
			CreateChecksSection(),
			CreateResultsSection(),
		})
	];
}

// LAYOUT
/** Folder picker, subfolder option, and the Scan Folder / Scan Selection buttons. */
TSharedRef<SWidget> SMenu_TextureValidator::CreateScanSection()
{
	return VrrMenu::Card(LOCTEXT("ScanHeader", "Scan"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("FolderLabel", "Folder"),
			VrrMenu::FolderPicker(&ScanFolder, TEXT("/Game"), [](const FText& Error) { VrrMenu::Notify(Error, VrrMenu::ENotify::Warning, 4.0f); })),
		VrrMenu::Row(LOCTEXT("SubfoldersLabel", "Subfolders"),
			VrrMenu::CheckBox(&bIncludeSubfolders, LOCTEXT("Subfolders", "Include subfolders"), LOCTEXT("SubfoldersTip", "Also scan every folder inside this one"))),
		SNew(SBox)
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				VrrMenu::ActionButton(LOCTEXT("ScanFolder", "Scan Folder"),
					LOCTEXT("ScanFolderTip", "Validate every texture in the folder"),
					[this]() { return ScanFolder.IsEmpty() ? LOCTEXT("NeedFolder", "Pick a folder first.") : FText::GetEmpty(); },
					FOnClicked::CreateSP(this, &SMenu_TextureValidator::OnScanFolderClicked))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("ScanSelection", "Scan Selection"))
				.ToolTipText(LOCTEXT("ScanSelectionTip", "Validate the textures selected in the Content Browser"))
				.OnClicked(this, &SMenu_TextureValidator::OnScanSelectionClicked)
			]
		],
	}));
}

/** One checkbox per check, plus the max texture size. */
TSharedRef<SWidget> SMenu_TextureValidator::CreateChecksSection()
{
	auto SizeText = [](int32 Size) { return FText::AsNumber(Size, &FNumberFormattingOptions::DefaultNoGrouping()); };
	const TSharedPtr<int32>* InitialSize = MaxSizeOptions.FindByPredicate([this](const TSharedPtr<int32>& Option) { return *Option == Settings.MaxSize; });

	return VrrMenu::Card(LOCTEXT("ChecksHeader", "Checks"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("NamingLabel", "Naming"),
			VrrMenu::CheckBox(&Settings.bCheckNaming, LOCTEXT("CheckNaming", "Naming convention"),
				FText::Format(LOCTEXT("CheckNamingTip", "Name starts with T_ and ends with a known suffix: {0}"), FText::FromString(TextureValidator::GetKnownSuffixList())))),
		VrrMenu::Row(LOCTEXT("SettingsLabel", "Settings"),
			VrrMenu::CheckBox(&Settings.bCheckSettings, LOCTEXT("CheckSettings", "Settings match suffix"),
				LOCTEXT("CheckSettingsTip", "sRGB and compression fit what the suffix says the texture holds (color, normal, mask)"))),
		VrrMenu::Row(LOCTEXT("ResolutionLabel", "Resolution"),
			VrrMenu::CheckBox(&Settings.bCheckResolution, LOCTEXT("CheckResolution", "Power of two, size limit"),
				LOCTEXT("CheckResolutionTip", "Width and height are powers of two and no larger than the max size"))),
		VrrMenu::Row(LOCTEXT("MaxSize", "Max size"),
			SNew(SComboBox<TSharedPtr<int32>>)
			.OptionsSource(&MaxSizeOptions)
			.InitiallySelectedItem(InitialSize ? *InitialSize : nullptr)
			.OnGenerateWidget_Lambda([SizeText](TSharedPtr<int32> Option)
			{
				return SNew(STextBlock).Text(SizeText(*Option));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Settings.MaxSize = *Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this, SizeText]() { return SizeText(Settings.MaxSize); })
			],
			FText::GetEmpty(),
			TAttribute<bool>::CreateLambda([this]() { return Settings.bCheckResolution; })),
		VrrMenu::Row(LOCTEXT("MipsLabel", "Mips"),
			VrrMenu::CheckBox(&Settings.bCheckMips, LOCTEXT("CheckMips", "Mips & streaming"),
				LOCTEXT("CheckMipsTip", "Has mipmaps, can stream, and normal maps use a normal map texture group (UI textures are skipped)"))),
	}));
}

/** Count badges, an All/Errors/Warnings filter, and the issue table (or a message while there's nothing to list). */
TSharedRef<SWidget> SMenu_TextureValidator::CreateResultsSection()
{
	namespace Columns = TextureValidatorColumns;

	auto CountText = [this](const FText& Format, TFunction<int32()> GetCount, bool bHideZero)
	{
		return TAttribute<FText>::CreateLambda([this, Format, GetCount, bHideZero]()
		{
			const int32 Count = GetCount();
			return NumScanned == INDEX_NONE || (bHideZero && Count == 0) ? FText::GetEmpty() : FText::Format(Format, Count);
		});
	};

	// Badges: scanned count, then errors / warnings, or "all passed"
	TSharedRef<SWidget> Badges = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(LOCTEXT("ScannedBadge", "{0} scanned"), [this]() { return NumScanned; }, false), VrrMenu::NeutralColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(LOCTEXT("ErrorsBadge", "{0} {0}|plural(one=error,other=errors)"), [this]() { return NumErrors; }, true), VrrMenu::ErrorColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(LOCTEXT("WarningsBadge", "{0} {0}|plural(one=warning,other=warnings)"), [this]() { return NumWarnings; }, true), VrrMenu::WarningColor())
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VrrMenu::Badge(TAttribute<FText>::CreateLambda([this]()
			{
				return NumScanned > 0 && AllIssues.IsEmpty() ? LOCTEXT("PassedBadge", "all passed") : FText::GetEmpty();
			}), VrrMenu::SuccessColor())
		];

	// Filter, shown once there is something to filter
	TSharedRef<SWidget> Filter = SNew(SSegmentedControl<EIssueFilter>)
		.Visibility_Lambda([this]() { return AllIssues.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.Value_Lambda([this]() { return IssueFilter; })
		.OnValueChanged_Lambda([this](EIssueFilter NewFilter)
		{
			IssueFilter = NewFilter;
			ApplyFilter();
		})
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::All).Text(LOCTEXT("FilterAll", "All"))
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::Errors).Text(LOCTEXT("FilterErrors", "Errors"))
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::Warnings).Text(LOCTEXT("FilterWarnings", "Warnings"));

	// Message in place of the table: before the first scan, when nothing was found, or when everything passed
	TSharedRef<SWidget> EmptyState = SNew(SVerticalBox)
		.Visibility_Lambda([this]() { return Issues.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(SImage)
			.DesiredSizeOverride(FVector2D(40.0f, 40.0f))
			.Image_Lambda([this]()
			{
				return FAppStyle::GetBrush(NumScanned > 0 && AllIssues.IsEmpty() ? "Icons.SuccessWithColor" : "Icons.Search");
			})
			.ColorAndOpacity_Lambda([this]()
			{
				return NumScanned > 0 && AllIssues.IsEmpty() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Font(VrrMenu::SectionFont())
			.Text(this, &SMenu_TextureValidator::GetEmptyTitle)
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(VrrMenu::HelpFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Justification(ETextJustify::Center)
			.Text(this, &SMenu_TextureValidator::GetEmptyHint)
		];

	return VrrMenu::Card(LOCTEXT("ResultsHeader", "Results"), VrrMenu::Rows({
		Badges,

		// Fixed height: the page already scrolls, and a list needs a bounded height
		SNew(SBox)
		.HeightOverride(420.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(IssueList, SListView<TSharedPtr<FTextureIssue>>)
				.Visibility_Lambda([this]() { return Issues.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible; })
				.ListItemsSource(&Issues)
				.SelectionMode(ESelectionMode::Single)
				.OnGenerateRow(this, &SMenu_TextureValidator::OnGenerateIssueRow)
				.OnSelectionChanged(this, &SMenu_TextureValidator::OnIssueSelected)
				.OnMouseButtonDoubleClick(this, &SMenu_TextureValidator::OnIssueDoubleClicked)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(Columns::Severity).DefaultLabel(FText::GetEmpty()).FixedWidth(28.0f)
					+ SHeaderRow::Column(Columns::Texture).DefaultLabel(LOCTEXT("TextureColumn", "Texture")).FillWidth(0.3f)
					+ SHeaderRow::Column(Columns::Rule).DefaultLabel(LOCTEXT("RuleColumn", "Check")).FillWidth(0.15f)
					+ SHeaderRow::Column(Columns::Issue).DefaultLabel(LOCTEXT("IssueColumn", "Issue")).FillWidth(0.55f)
				)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 60.0f, 0.0f, 0.0f)
			[
				EmptyState
			]
		],
	}), Filter);
}

/** Table row widget for one issue. */
TSharedRef<ITableRow> SMenu_TextureValidator::OnGenerateIssueRow(TSharedPtr<FTextureIssue> Issue, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STextureIssueRow, OwnerTable).Issue(Issue);
}

// BEHAVIOR
/** Validates every texture in the scan folder. */
FReply SMenu_TextureValidator::OnScanFolderClicked()
{
	ShowResult(TextureValidator::Validate(TextureValidator::FindTextures(ScanFolder, bIncludeSubfolders), Settings));
	return FReply::Handled();
}

/** Validates the textures selected in the Content Browser. */
FReply SMenu_TextureValidator::OnScanSelectionClicked()
{
	ShowResult(TextureValidator::Validate(TextureValidator::FilterTextures(VrrCore::GetSelectedAssets()), Settings));
	return FReply::Handled();
}

/** Copies a validation result into the issue list and the counts, then refreshes the table. */
void SMenu_TextureValidator::ShowResult(const FTextureValidationResult& Result)
{
	AllIssues.Reset();
	for (const FTextureIssue& Issue : Result.Issues)
	{
		AllIssues.Add(MakeShared<FTextureIssue>(Issue));
	}
	NumScanned = Result.NumScanned;
	NumErrors = Result.NumErrors;
	NumWarnings = Result.NumWarnings;

	ApplyFilter();
}

/** Fills the table's list with the issues that pass the All/Errors/Warnings filter. */
void SMenu_TextureValidator::ApplyFilter()
{
	Issues.Reset();
	for (const TSharedPtr<FTextureIssue>& Issue : AllIssues)
	{
		const bool bError = Issue->Severity == FTextureIssue::ESeverity::Error;
		if (IssueFilter == EIssueFilter::All || (IssueFilter == EIssueFilter::Errors) == bError)
		{
			Issues.Add(Issue);
		}
	}

	if (IssueList.IsValid())
	{
		IssueList->RequestListRefresh();
	}
}

void SMenu_TextureValidator::OnIssueSelected(TSharedPtr<FTextureIssue> Issue, ESelectInfo::Type SelectInfo)
{
	// Show the texture in the Content Browser without stealing focus
	if (Issue.IsValid() && SelectInfo != ESelectInfo::Direct)
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowser.Get().SyncBrowserToAssets(TArray<FAssetData>{ Issue->Asset }, false, false);
	}
}

/** Opens the texture editor for the issue's texture. */
void SMenu_TextureValidator::OnIssueDoubleClicked(TSharedPtr<FTextureIssue> Issue)
{
	if (Issue.IsValid() && GEditor)
	{
		if (UObject* Asset = Issue->Asset.GetAsset())
		{
			GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
		}
	}
}

/** Title of the message shown instead of an empty table. */
FText SMenu_TextureValidator::GetEmptyTitle() const
{
	if (NumScanned == INDEX_NONE)
	{
		return LOCTEXT("NotScannedTitle", "Nothing scanned yet");
	}
	if (NumScanned == 0)
	{
		return LOCTEXT("NothingScannedTitle", "No textures found");
	}
	if (AllIssues.IsEmpty())
	{
		return FText::Format(LOCTEXT("AllPassedTitle", "All {0} {0}|plural(one=texture,other=textures) passed"), NumScanned);
	}
	return IssueFilter == EIssueFilter::Errors ? LOCTEXT("NoErrorsTitle", "No errors") : LOCTEXT("NoWarningsTitle", "No warnings");
}

/** Line under the empty-state title: what to do next. */
FText SMenu_TextureValidator::GetEmptyHint() const
{
	if (NumScanned == INDEX_NONE)
	{
		return LOCTEXT("NotScannedHint", "Scan a folder or the Content Browser selection.\nClick a result to find the texture, double-click to open it.");
	}
	if (NumScanned == 0)
	{
		return LOCTEXT("NothingScannedHint", "Pick another folder, include subfolders, or select textures in the Content Browser.");
	}
	if (AllIssues.IsEmpty())
	{
		return LOCTEXT("AllPassedHint", "Every enabled check passed.");
	}
	return LOCTEXT("FilteredHint", "Switch the filter to All to see the other issues.");
}

#undef LOCTEXT_NAMESPACE
