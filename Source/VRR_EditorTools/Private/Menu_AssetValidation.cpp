// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Asset Validation page (UI only).
 *
 * Scans a folder or the Content Browser selection with AssetValidation::Validate (Core_AssetValidation.cpp),
 * which runs the Texture Validation (Core_TextureValidator.cpp), Mesh Validation (Core_MeshValidator.cpp) and
 * Material Validation (Core_MaterialValidator.cpp) checks, and lists each section's issues in its own table (errors first).
 * Click a row to find the asset, double-click to open it.
 */

#include "Menu_AssetValidation.h"

#include "ContentBrowserModule.h"
#include "Core_Common.h"
#include "Dialog/SCustomDialog.h"
#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Framework/Application/SlateApplication.h"
#include "IContentBrowserSingleton.h"
#include "IDesktopPlatform.h"
#include "MenuWidgets.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/CoreStyle.h"
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

#define LOCTEXT_NAMESPACE "SMenu_AssetValidation"

/** Column ids of the results table. */
namespace AssetValidationColumns
{
	const FName Severity("Severity");
	const FName Asset("Asset");
	const FName Rule("Rule");
	const FName Issue("Issue");
	const FName Fix("Fix");
}

/** One row of the results table. */
class SAssetIssueRow : public SMultiColumnTableRow<TSharedPtr<FAssetIssue>>
{
public:
	SLATE_BEGIN_ARGS(SAssetIssueRow) {}
		SLATE_ARGUMENT(TSharedPtr<FAssetIssue>, Issue)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STableViewBase>& OwnerTable)
	{
		Issue = InArgs._Issue;
		SMultiColumnTableRow::Construct(FSuperRowType::FArguments(), OwnerTable);
	}

	/** What fixing this row does, including the add / replace choices for an unknown prefix or suffix. */
	static FText GetFixToolTip(const FAssetIssue& Issue)
	{
		if (!Issue.CanFix())
		{
			return FText::GetEmpty();
		}
		if (Issue.RenameTo.IsEmpty() && !Issue.HasPrefixChoice() && !Issue.HasSuffixChoice())
		{
			return LOCTEXT("FixSettingsTip", "Fixing corrects this setting");
		}
		const FString OldName = Issue.RenameObject.IsNull() ? Issue.Asset.AssetName.ToString() : Issue.RenameObject.GetAssetName();
		FString Tip = FText::Format(LOCTEXT("FixRenameTip", "Fixing renames {0} to {1}"), FText::FromString(OldName), FText::FromString(Issue.RenameTo)).ToString();
		if (Issue.HasPrefixChoice())
		{
			Tip += TEXT("\n") + FText::Format(LOCTEXT("FixPrefixChoiceTip", "or {0}, replacing the unknown prefix"), FText::FromString(Issue.RenameReplacingPrefix)).ToString();
		}
		if (Issue.HasSuffixChoice())
		{
			Tip += TEXT("\n") + FText::Format(LOCTEXT("FixSuffixChoiceTip", "or {0}, replacing the unknown suffix"), FText::FromString(Issue.RenameReplacingSuffix)).ToString();
		}
		if (!Issue.RenameReplacingBoth.IsEmpty())
		{
			Tip += TEXT("\n") + FText::Format(LOCTEXT("FixBothChoiceTip", "or {0}, replacing both"), FText::FromString(Issue.RenameReplacingBoth)).ToString();
		}
		return FText::FromString(Tip);
	}

	virtual TSharedRef<SWidget> GenerateWidgetForColumn(const FName& ColumnName) override
	{
		namespace Columns = AssetValidationColumns;
		const bool bError = Issue->Severity == FAssetIssue::ESeverity::Error;

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

		if (ColumnName == Columns::Fix)
		{
			return SNew(SBox)
				.VAlign(VAlign_Center)
				.Padding(4.0f, 2.0f)
				[
					SNew(STextBlock)
					.Text(Issue->CanFix() ? LOCTEXT("FixAuto", "Auto") : FText::GetEmpty())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.ToolTipText(GetFixToolTip(*Issue))
				];
		}

		FText Text;
		if (ColumnName == Columns::Asset)		Text = FText::FromName(Issue->Asset.AssetName);
		else if (ColumnName == Columns::Rule)	Text = Issue->Rule;
		else if (ColumnName == Columns::Issue)	Text = Issue->Message;

		return SNew(SBox)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 2.0f)
			[
				SNew(STextBlock)
				.Text(Text)
				.ToolTipText(ColumnName == Columns::Asset ? FText::FromString(Issue->Asset.GetObjectPathString()) : Text)
			];
	}

private:
	TSharedPtr<FAssetIssue> Issue;
};

/** Builds the page: scan, then Texture, Mesh and Material Validation, each followed by its results. */
void SMenu_AssetValidation::Construct(const FArguments& InArgs)
{
	for (int32 Size : { 1024, 2048, 4096, 8192 })
	{
		MaxSizeOptions.Add(MakeShared<int32>(Size));
	}
	TextureSettings.MaxSize = 4096;

	ChildSlot
	[
		VrrMenu::Stack({
			CreateScanSection(),
			CreateTextureSection(),
			CreateResultsSection(FAssetIssue::EAssetType::Texture),
			CreateMeshSection(),
			CreateResultsSection(FAssetIssue::EAssetType::Mesh),
			CreateMaterialSection(),
			CreateResultsSection(FAssetIssue::EAssetType::Material),
		})
	];
}

// LAYOUT
/** A check section: the header checkbox turns it on, the rows set it up. */
static TSharedRef<SWidget> MakeValidationCard(const FText& Title, bool* bEnabled, const FText& ToolTip, const TArray<TSharedRef<SWidget>>& Rows)
{
	TSharedRef<SWidget> Content = VrrMenu::Rows(Rows);
	Content->SetEnabled(TAttribute<bool>::CreateLambda([bEnabled]() { return *bEnabled; }));
	return VrrMenu::Card(Title, Content, VrrMenu::CheckBox(bEnabled, LOCTEXT("ValidateThis", "Validate"), ToolTip));
}

/** Folder picker, subfolder option, and the Scan Folder / Scan Selection buttons. */
TSharedRef<SWidget> SMenu_AssetValidation::CreateScanSection()
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
					LOCTEXT("ScanFolderTip", "Validate every texture, mesh and material in the folder (the sections turned on below)"),
					[this]() { return GetScanBlocker(); },
					FOnClicked::CreateSP(this, &SMenu_AssetValidation::OnScanFolderClicked))
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("ScanSelection", "Scan Selection"))
				.ToolTipText(LOCTEXT("ScanSelectionTip", "Validate the textures, meshes and materials selected in the Content Browser"))
				.IsEnabled_Lambda([this]() { return bValidateTextures || bValidateMeshes || bValidateMaterials; })
				.OnClicked(this, &SMenu_AssetValidation::OnScanSelectionClicked)
			]
		],
	}));
}

/** Texture checks: naming, settings, resolution (with the max size) and mips. */
TSharedRef<SWidget> SMenu_AssetValidation::CreateTextureSection()
{
	auto SizeText = [](int32 Size) { return FText::AsNumber(Size, &FNumberFormattingOptions::DefaultNoGrouping()); };
	const TSharedPtr<int32>* InitialSize = MaxSizeOptions.FindByPredicate([this](const TSharedPtr<int32>& Option) { return *Option == TextureSettings.MaxSize; });

	return MakeValidationCard(LOCTEXT("TextureHeader", "Texture Validation"), &bValidateTextures,
		LOCTEXT("TextureHeaderTip", "Check textures when scanning"), {
		VrrMenu::Row(LOCTEXT("NamingLabel", "Naming"),
			VrrMenu::CheckBox(&TextureSettings.bCheckNaming, LOCTEXT("CheckNaming", "Naming convention"),
				FText::Format(LOCTEXT("CheckNamingTip", "Name starts with T_ (VT_ for virtual textures) and ends with a known suffix: {0}. Curve atlases start with CA_ instead and need no suffix (they get their own linear, no-mips rules)"), FText::FromString(TextureValidator::GetKnownSuffixList())))),
		VrrMenu::Row(LOCTEXT("SettingsLabel", "Settings"),
			VrrMenu::CheckBox(&TextureSettings.bCheckSettings, LOCTEXT("CheckSettings", "Settings match suffix"),
				LOCTEXT("CheckSettingsTip", "sRGB and compression fit what the suffix says the texture holds (color, normal, mask)"))),
		VrrMenu::Row(LOCTEXT("ResolutionLabel", "Resolution"),
			VrrMenu::CheckBox(&TextureSettings.bCheckResolution, LOCTEXT("CheckResolution", "Power of two, size limit"),
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
					TextureSettings.MaxSize = *Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this, SizeText]() { return SizeText(TextureSettings.MaxSize); })
			],
			FText::GetEmpty(),
			TAttribute<bool>::CreateLambda([this]() { return TextureSettings.bCheckResolution; })),
		VrrMenu::Row(LOCTEXT("MipsLabel", "Mips"),
			VrrMenu::CheckBox(&TextureSettings.bCheckMips, LOCTEXT("CheckMips", "Mips & streaming"),
				LOCTEXT("CheckMipsTip", "Has mipmaps, can stream, and normal maps use a normal map texture group (UI textures are skipped)"))),
		VrrMenu::Row(LOCTEXT("VirtualLabel", "Virtual"),
			VrrMenu::CheckBox(&TextureSettings.bCheckVirtual, LOCTEXT("CheckVirtual", "Virtual texture rules"),
				LOCTEXT("CheckVirtualTip", "VT_ textures have Virtual Texture Streaming on (and the project has virtual texturing), a power-of-two size or padding, mipmaps, not the ColorLookupTable group, and aren't under the min size. Virtual textures skip the max size and Never Stream checks"))),
		VrrMenu::Row(LOCTEXT("VirtualMinSize", "Min virtual size"),
			VrrMenu::IntField(&TextureSettings.VirtualMinSize, 0, 65536),
			LOCTEXT("VirtualMinSizeTip", "Virtual textures whose largest side is under this get a warning: they're cheaper as regular textures"),
			TAttribute<bool>::CreateLambda([this]() { return TextureSettings.bCheckVirtual; })),
	});
}

/** Mesh checks: naming, materials, collision, LODs (with the triangle limit), scale (with its range) and lightmap UVs. */
TSharedRef<SWidget> SMenu_AssetValidation::CreateMeshSection()
{
	return MakeValidationCard(LOCTEXT("MeshHeader", "Mesh Validation"), &bValidateMeshes,
		LOCTEXT("MeshHeaderTip", "Check static and skeletal meshes when scanning"), {
		VrrMenu::Row(LOCTEXT("MeshNamingLabel", "Naming"),
			VrrMenu::CheckBox(&MeshSettings.bCheckNaming, LOCTEXT("CheckMeshNaming", "Naming convention"),
				FText::Format(LOCTEXT("CheckMeshNamingTip", "Prefix fits the asset: {0}. After the prefix: letters, digits and single underscores"), FText::FromString(MeshValidator::GetPrefixList())))),
		VrrMenu::Row(LOCTEXT("MaterialsLabel", "Materials"),
			VrrMenu::CheckBox(&MeshSettings.bCheckMaterials, LOCTEXT("CheckMaterials", "Slots filled and named"),
				LOCTEXT("CheckMaterialsTip", "Every material slot has a material, and project materials start with M_ or MI_"))),
		VrrMenu::Row(LOCTEXT("CollisionLabel", "Collision"),
			VrrMenu::CheckBox(&MeshSettings.bCheckCollision, LOCTEXT("CheckCollision", "Has collision"),
				LOCTEXT("CheckCollisionTip", "Static meshes have simple collision (or use complex as simple); skeletal meshes have a physics asset"))),
		VrrMenu::Row(LOCTEXT("LODsLabel", "LODs"),
			VrrMenu::CheckBox(&MeshSettings.bCheckLODs, LOCTEXT("CheckLODs", "Dense meshes have LODs"),
				LOCTEXT("CheckLODsTip", "Meshes over the triangle limit have more than one LOD (Nanite static meshes are fine without)"))),
		VrrMenu::Row(LOCTEXT("MaxTriangles", "Triangles without LODs"),
			VrrMenu::IntField(&MeshSettings.MaxTrianglesWithoutLODs, 0, 10000000),
			LOCTEXT("MaxTrianglesTip", "The most triangles a mesh may have with only one LOD"),
			TAttribute<bool>::CreateLambda([this]() { return MeshSettings.bCheckLODs; })),
		VrrMenu::Row(LOCTEXT("ScaleLabel", "Scale"),
			VrrMenu::CheckBox(&MeshSettings.bCheckScale, LOCTEXT("CheckScale", "Size in range"),
				LOCTEXT("CheckScaleTip", "The mesh's bounds are between the min and max size. Far outside usually means it was imported at the wrong scale (meters vs centimeters)"))),
		VrrMenu::Row(LOCTEXT("MinSize", "Min size (cm)"),
			VrrMenu::FloatField(&MeshSettings.MinSize, 0.0f, 100000.0f, 0.1f),
			LOCTEXT("MinSizeTip", "Smaller than this across is flagged"),
			TAttribute<bool>::CreateLambda([this]() { return MeshSettings.bCheckScale; })),
		VrrMenu::Row(LOCTEXT("MaxMeshSize", "Max size (cm)"),
			VrrMenu::FloatField(&MeshSettings.MaxSize, 0.0f, 10000000.0f, 10.0f),
			LOCTEXT("MaxMeshSizeTip", "Larger than this across is flagged (10000 cm = 100 m)"),
			TAttribute<bool>::CreateLambda([this]() { return MeshSettings.bCheckScale; })),
		VrrMenu::Row(LOCTEXT("LightmapLabel", "Lightmap"),
			VrrMenu::CheckBox(&MeshSettings.bCheckLightmap, LOCTEXT("CheckLightmap", "Lightmap UVs"),
				LOCTEXT("CheckLightmapTip", "Static meshes: the lightmap channel exists and isn't UV0. Off by default: only matters with baked lighting"))),
	});
}

/** Material checks: naming and the _Inst suffix. */
TSharedRef<SWidget> SMenu_AssetValidation::CreateMaterialSection()
{
	return MakeValidationCard(LOCTEXT("MaterialHeader", "Material Validation"), &bValidateMaterials,
		LOCTEXT("MaterialHeaderTip", "Check materials, material instances, material functions and post process materials when scanning"), {
		VrrMenu::Row(LOCTEXT("MaterialNamingLabel", "Naming"),
			VrrMenu::CheckBox(&MaterialSettings.bCheckNaming, LOCTEXT("CheckMaterialNaming", "Naming convention"),
				FText::Format(LOCTEXT("CheckMaterialNamingTip", "Prefix fits the asset: {0}. After the prefix: letters, digits and single underscores"), FText::FromString(MaterialValidator::GetPrefixList())))),
		VrrMenu::Row(LOCTEXT("InstSuffixLabel", "Instances"),
			VrrMenu::CheckBox(&MaterialSettings.bCheckInstSuffix, LOCTEXT("CheckInstSuffix", "No _Inst suffix"),
				LOCTEXT("CheckInstSuffixTip", "Material instances don't keep the _Inst suffix Unreal adds to new instances (M_Rock_Inst). Fix All renames them (to MI_Rock, or PPMI_ for post process instances)"))),
	});
}

/** The wording that differs between the result sections. */
struct FResultsText
{
	FText Section;			// "Texture Validation" (export file and dialog)
	FText Header;			// card title
	FText Column;			// asset column label
	FText ScannedFormat;	// "{0} textures scanned"
	FText PassedFormat;		// "All {0} textures passed"
	FText NoneFoundTitle;
	FText NoneFoundHint;
	FText OffHint;			// the section was off for the last scan
};

static const FResultsText& GetResultsText(FAssetIssue::EAssetType Type)
{
	static const FResultsText Texts[] = {
		{
			LOCTEXT("TextureHeader", "Texture Validation"),
			LOCTEXT("TextureResultsHeader", "Texture Validation Results"),
			LOCTEXT("TextureColumn", "Texture"),
			LOCTEXT("TexturesScannedBadge", "{0} {0}|plural(one=texture,other=textures) scanned"),
			LOCTEXT("AllTexturesPassedTitle", "All {0} {0}|plural(one=texture,other=textures) passed"),
			LOCTEXT("NoTexturesTitle", "No textures found"),
			LOCTEXT("NoTexturesHint", "Pick another folder, include subfolders, or select textures in the Content Browser."),
			LOCTEXT("TexturesOffHint", "Texture Validation is off: turn it on above to include textures in the scan."),
		},
		{
			LOCTEXT("MeshHeader", "Mesh Validation"),
			LOCTEXT("MeshResultsHeader", "Mesh Validation Results"),
			LOCTEXT("MeshColumn", "Mesh"),
			LOCTEXT("MeshesScannedBadge", "{0} {0}|plural(one=mesh,other=meshes) scanned"),
			LOCTEXT("AllMeshesPassedTitle", "All {0} {0}|plural(one=mesh,other=meshes) passed"),
			LOCTEXT("NoMeshesTitle", "No meshes found"),
			LOCTEXT("NoMeshesHint", "Pick another folder, include subfolders, or select meshes in the Content Browser."),
			LOCTEXT("MeshesOffHint", "Mesh Validation is off: turn it on above to include meshes in the scan."),
		},
		{
			LOCTEXT("MaterialHeader", "Material Validation"),
			LOCTEXT("MaterialResultsHeader", "Material Validation Results"),
			LOCTEXT("MaterialColumn", "Material"),
			LOCTEXT("MaterialsScannedBadge", "{0} {0}|plural(one=material,other=materials) scanned"),
			LOCTEXT("AllMaterialsPassedTitle", "All {0} {0}|plural(one=material,other=materials) passed"),
			LOCTEXT("NoMaterialsTitle", "No materials found"),
			LOCTEXT("NoMaterialsHint", "Pick another folder, include subfolders, or select materials in the Content Browser."),
			LOCTEXT("MaterialsOffHint", "Material Validation is off: turn it on above to include materials in the scan."),
		},
	};
	static_assert(UE_ARRAY_COUNT(Texts) == static_cast<int32>(FAssetIssue::EAssetType::Count), "One entry per asset type");
	return Texts[static_cast<int32>(Type)];
}

/**
 * One section's results card ("Texture Validation Results" and so on): count badges, an All/Errors/Warnings filter,
 * and that section's issue table (or a message while there's nothing to list).
 */
TSharedRef<SWidget> SMenu_AssetValidation::CreateResultsSection(EAssetType Type)
{
	namespace Columns = AssetValidationColumns;
	const FResultsText& Text = GetResultsText(Type);
	FResults& Section = GetResults(Type);
	const FResults* R = &Section;

	auto CountText = [R](const FText& Format, int32 const FResults::* Count, bool bHideZero)
	{
		return TAttribute<FText>::CreateLambda([R, Format, Count, bHideZero]()
		{
			const int32 Value = R->*Count;
			return R->NumScanned == INDEX_NONE || (bHideZero && Value == 0) ? FText::GetEmpty() : FText::Format(Format, Value);
		});
	};

	// Badges: scanned count, then errors / warnings, or "all passed"
	TSharedRef<SWidget> Badges = SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(Text.ScannedFormat, &FResults::NumScanned, false), VrrMenu::NeutralColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(LOCTEXT("ErrorsBadge", "{0} {0}|plural(one=error,other=errors)"), &FResults::NumErrors, true), VrrMenu::ErrorColor())
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			VrrMenu::Badge(CountText(LOCTEXT("WarningsBadge", "{0} {0}|plural(one=warning,other=warnings)"), &FResults::NumWarnings, true), VrrMenu::WarningColor())
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			VrrMenu::Badge(TAttribute<FText>::CreateLambda([R]()
			{
				return R->NumScanned > 0 && R->AllIssues.IsEmpty() ? LOCTEXT("PassedBadge", "all passed") : FText::GetEmpty();
			}), VrrMenu::SuccessColor())
		];

	// Filter, shown once there is something to filter
	TSharedRef<SWidget> Filter = SNew(SSegmentedControl<EIssueFilter>)
		.Visibility_Lambda([R]() { return R->AllIssues.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
		.Value_Lambda([R]() { return R->Filter; })
		.OnValueChanged_Lambda([this, Type](EIssueFilter NewFilter)
		{
			FResults& Target = GetResults(Type);
			Target.Filter = NewFilter;
			ApplyFilter(Target);
		})
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::All).Text(LOCTEXT("FilterAll", "All"))
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::Errors).Text(LOCTEXT("FilterErrors", "Errors"))
		+ SSegmentedControl<EIssueFilter>::Slot(EIssueFilter::Warnings).Text(LOCTEXT("FilterWarnings", "Warnings"));

	// Message in place of the table: before the first scan, when nothing was found, or when everything passed
	TSharedRef<SWidget> EmptyState = SNew(SVerticalBox)
		.Visibility_Lambda([R]() { return R->Issues.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(SImage)
			.DesiredSizeOverride(FVector2D(40.0f, 40.0f))
			.Image_Lambda([R]()
			{
				return FAppStyle::GetBrush(R->NumScanned > 0 && R->AllIssues.IsEmpty() ? "Icons.SuccessWithColor" : "Icons.Search");
			})
			.ColorAndOpacity_Lambda([R]()
			{
				return R->NumScanned > 0 && R->AllIssues.IsEmpty() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground();
			})
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 0.0f, 0.0f, 4.0f)
		[
			SNew(STextBlock)
			.Font(VrrMenu::SectionFont())
			.Text_Lambda([this, Type]() { return GetEmptyTitle(Type); })
		]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(VrrMenu::HelpFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Justification(ETextJustify::Center)
			.Text_Lambda([this, Type]() { return GetEmptyHint(Type); })
		];

	TArray<TSharedRef<SWidget>> Rows = { Badges };

	// Fix All / Export List, once the section has been scanned
	Rows.Add(SNew(SHorizontalBox)
		.Visibility_Lambda([R]() { return R->NumScanned == INDEX_NONE ? EVisibility::Collapsed : EVisibility::Visible; })
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SButton)
			.Text_Lambda([this, Type]()
			{
				const int32 NumSelected = GetSelectedFixable(Type).Num();
				return NumSelected > 0 ? FText::Format(LOCTEXT("FixSelectedCount", "Fix Selected ({0})"), NumSelected) : LOCTEXT("FixSelected", "Fix Selected");
			})
			.ToolTipText(LOCTEXT("FixSelectedTip", "Fix the issues marked Auto among the selected rows (Ctrl or Shift click to select several). Fixed assets are checked again and left unsaved"))
			.IsEnabled_Lambda([this, Type]() { return !GetSelectedFixable(Type).IsEmpty(); })
			.OnClicked(this, &SMenu_AssetValidation::OnFixClicked, Type, true)
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SButton)
			.Text_Lambda([R]()
			{
				return R->NumFixable > 0 ? FText::Format(LOCTEXT("FixAllCount", "Fix All ({0})"), R->NumFixable) : LOCTEXT("FixAll", "Fix All");
			})
			.ToolTipText(LOCTEXT("FixAllTip", "Fix every issue marked Auto in this list: renames to the expected name (references are updated) and corrects settings. Fixed assets are checked again and left unsaved"))
			.IsEnabled_Lambda([R]() { return R->NumFixable > 0; })
			.OnClicked(this, &SMenu_AssetValidation::OnFixClicked, Type, false)
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SButton)
			.Text(LOCTEXT("ExportList", "Export List"))
			.ToolTipText(LOCTEXT("ExportListTip", "Save this section's assets and their issues as a JSON file (all issues, whatever the filter)"))
			.OnClicked(this, &SMenu_AssetValidation::OnExportClicked, Type)
		]);

	Rows.Add(
		// Fixed height: the page already scrolls, and a list needs a bounded height
		SNew(SBox)
		.HeightOverride(320.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(Section.List, SListView<TSharedPtr<FAssetIssue>>)
				.Visibility_Lambda([R]() { return R->Issues.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible; })
				.ListItemsSource(&Section.Issues)
				.SelectionMode(ESelectionMode::Multi)
				.OnGenerateRow(this, &SMenu_AssetValidation::OnGenerateIssueRow)
				.OnSelectionChanged(this, &SMenu_AssetValidation::OnIssueSelected)
				.OnMouseButtonDoubleClick(this, &SMenu_AssetValidation::OnIssueDoubleClicked)
				.HeaderRow
				(
					SNew(SHeaderRow)
					+ SHeaderRow::Column(Columns::Severity).DefaultLabel(FText::GetEmpty()).FixedWidth(28.0f)
					+ SHeaderRow::Column(Columns::Asset).DefaultLabel(Text.Column).FillWidth(0.3f)
					+ SHeaderRow::Column(Columns::Rule).DefaultLabel(LOCTEXT("RuleColumn", "Check")).FillWidth(0.15f)
					+ SHeaderRow::Column(Columns::Issue).DefaultLabel(LOCTEXT("IssueColumn", "Issue")).FillWidth(0.55f)
					+ SHeaderRow::Column(Columns::Fix).DefaultLabel(LOCTEXT("FixColumn", "Fix")).FixedWidth(44.0f)
				)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 50.0f, 0.0f, 0.0f)
			[
				EmptyState
			]
		]);

	return VrrMenu::Card(Text.Header, VrrMenu::Rows(Rows), Filter);
}

/** Table row widget for one issue. */
TSharedRef<ITableRow> SMenu_AssetValidation::OnGenerateIssueRow(TSharedPtr<FAssetIssue> Issue, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SAssetIssueRow, OwnerTable).Issue(Issue);
}

// BEHAVIOR
/**
 * Asks how to fix names with a prefix or suffix the validator doesn't know, showing what each answer gives.
 * Examples are "old name", "name when adding", "name when replacing". Returns 0 (add), 1 (replace) or INDEX_NONE (cancel).
 */
static int32 AskAddOrReplace(const FText& Title, const FText& Intro, const TArray<TTuple<FString, FString, FString>>& Examples,
	const FText& AddLabel, const FText& ReplaceLabel)
{
	constexpr int32 MaxShown = 8;
	const FSlateFontInfo MonoFont = FCoreStyle::GetDefaultFontStyle("Mono", 9);

	TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			SNew(STextBlock).Text(Intro).AutoWrapText(true)
		];
	for (int32 Index = 0; Index < FMath::Min(Examples.Num(), MaxShown); ++Index)
	{
		const TTuple<FString, FString, FString>& Example = Examples[Index];
		Content->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(STextBlock)
			.Font(MonoFont)
			.Text(FText::Format(LOCTEXT("AddReplaceExample", "{0}\n    add: {1}    replace: {2}"),
				FText::FromString(Example.Get<0>()), FText::FromString(Example.Get<1>()), FText::FromString(Example.Get<2>())))
		];
	}
	if (Examples.Num() > MaxShown)
	{
		Content->AddSlot().AutoHeight().Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(FText::Format(LOCTEXT("AddReplaceMore", "...and {0} more"), Examples.Num() - MaxShown))
		];
	}

	const TSharedRef<SCustomDialog> Dialog = SNew(SCustomDialog)
		.Title(Title)
		.Content()
		[
			Content
		]
		.Buttons({
			SCustomDialog::FButton(AddLabel),
			SCustomDialog::FButton(ReplaceLabel),
			SCustomDialog::FButton(LOCTEXT("Cancel", "Cancel")),
		});
	const int32 Pressed = Dialog->ShowModal();
	return Pressed == 0 || Pressed == 1 ? Pressed : INDEX_NONE;
}

bool SMenu_AssetValidation::IsValidating(EAssetType Type) const
{
	switch (Type)
	{
	case EAssetType::Texture:	return bValidateTextures;
	case EAssetType::Mesh:		return bValidateMeshes;
	case EAssetType::Material:	return bValidateMaterials;
	default:					return false;
	}
}

FAssetValidationSettings SMenu_AssetValidation::GetValidationSettings() const
{
	FAssetValidationSettings Settings;
	Settings.Textures = bValidateTextures ? &TextureSettings : nullptr;
	Settings.Meshes = bValidateMeshes ? &MeshSettings : nullptr;
	Settings.Materials = bValidateMaterials ? &MaterialSettings : nullptr;
	return Settings;
}

FAssetValidationSettings SMenu_AssetValidation::GetSectionSettings(EAssetType Type) const
{
	FAssetValidationSettings Settings;
	Settings.Textures = Type == EAssetType::Texture ? &TextureSettings : nullptr;
	Settings.Meshes = Type == EAssetType::Mesh ? &MeshSettings : nullptr;
	Settings.Materials = Type == EAssetType::Material ? &MaterialSettings : nullptr;
	return Settings;
}

FText SMenu_AssetValidation::GetScanBlocker() const
{
	if (!bValidateTextures && !bValidateMeshes && !bValidateMaterials)
	{
		return LOCTEXT("NeedSection", "Turn on Texture, Mesh or Material Validation.");
	}
	return ScanFolder.IsEmpty() ? LOCTEXT("NeedFolder", "Pick a folder first.") : FText::GetEmpty();
}

/** Validates every asset of the enabled types in the scan folder. */
FReply SMenu_AssetValidation::OnScanFolderClicked()
{
	const FAssetValidationSettings Settings = GetValidationSettings();
	ShowResult(AssetValidation::Validate(AssetValidation::FindAssets(ScanFolder, bIncludeSubfolders, Settings), Settings), Settings);
	return FReply::Handled();
}

/** Validates the assets of the enabled types selected in the Content Browser. */
FReply SMenu_AssetValidation::OnScanSelectionClicked()
{
	const FAssetValidationSettings Settings = GetValidationSettings();
	ShowResult(AssetValidation::Validate(AssetValidation::FilterAssets(VrrCore::GetSelectedAssets(), Settings), Settings), Settings);
	return FReply::Handled();
}

/** Sorts the issues into the per-section lists (keeping their errors-first order), counts them and refreshes the tables. */
void SMenu_AssetValidation::ShowResult(const FAssetValidationResult& Result, const FAssetValidationSettings& Scanned)
{
	const bool bScanned[] = { Scanned.Textures != nullptr, Scanned.Meshes != nullptr, Scanned.Materials != nullptr };
	static_assert(UE_ARRAY_COUNT(bScanned) == static_cast<int32>(EAssetType::Count), "One entry per asset type");

	for (int32 Index = 0; Index < static_cast<int32>(EAssetType::Count); ++Index)
	{
		FResults& Section = ResultLists[Index];
		Section.AllIssues.Reset();
		Section.NumErrors = 0;
		Section.NumWarnings = 0;
		Section.NumScanned = bScanned[Index] ? Result.GetNumScanned(static_cast<EAssetType>(Index)) : INDEX_NONE;
	}
	for (const FAssetIssue& Issue : Result.Issues)
	{
		GetResults(Issue.Type).AllIssues.Add(MakeShared<FAssetIssue>(Issue));
	}
	for (FResults& Section : ResultLists)
	{
		RefreshSection(Section);
	}
}

void SMenu_AssetValidation::RefreshSection(FResults& Section)
{
	Section.NumErrors = 0;
	Section.NumWarnings = 0;
	Section.NumFixable = 0;
	for (const TSharedPtr<FAssetIssue>& Issue : Section.AllIssues)
	{
		(Issue->Severity == FAssetIssue::ESeverity::Error ? Section.NumErrors : Section.NumWarnings)++;
		Section.NumFixable += Issue->CanFix() ? 1 : 0;
	}
	ApplyFilter(Section);
}

/**
 *  1. ask whether unknown prefixes / suffixes are kept (expected one added) or replaced, if there are any,
 *  2. confirm (renames and setting changes touch many assets),
 *  3. apply the fixable issues (all of the section's, or the selected ones),
 *  4. swap the fixed assets' old issues for a fresh check under their new names (other sections are untouched),
 *  5. say what happened.
 */
TArray<TSharedPtr<FAssetIssue>> SMenu_AssetValidation::GetSelectedFixable(EAssetType Type) const
{
	const FResults& Section = GetResults(Type);
	TArray<TSharedPtr<FAssetIssue>> Selected;
	if (Section.List.IsValid())
	{
		Selected = Section.List->GetSelectedItems();
		Selected.RemoveAll([](const TSharedPtr<FAssetIssue>& Issue) { return !Issue.IsValid() || !Issue->CanFix(); });
	}
	return Selected;
}

FReply SMenu_AssetValidation::OnFixClicked(EAssetType Type, bool bSelectedOnly)
{
	FResults& Section = GetResults(Type);
	TArray<const FAssetIssue*> Fixable;
	TSet<FSoftObjectPath> FixableAssets;
	for (const TSharedPtr<FAssetIssue>& Issue : bSelectedOnly ? GetSelectedFixable(Type) : Section.AllIssues)
	{
		if (Issue->CanFix())
		{
			Fixable.Add(Issue.Get());
			FixableAssets.Add(Issue->Asset.GetSoftObjectPath());
		}
	}
	if (Fixable.IsEmpty())
	{
		return FReply::Handled();
	}

	// 1. Unknown prefixes / suffixes: keep them and add the expected one, or replace them (asked once each)
	FAssetFixOptions Options;
	for (const bool bSuffix : { false, true })
	{
		TArray<TTuple<FString, FString, FString>> Examples;
		TSet<FSoftObjectPath> Seen;
		for (const FAssetIssue* Issue : Fixable)
		{
			const bool bHasChoice = bSuffix ? Issue->HasSuffixChoice() : Issue->HasPrefixChoice();
			if (bHasChoice && !Seen.Contains(Issue->Asset.GetSoftObjectPath()))
			{
				Seen.Add(Issue->Asset.GetSoftObjectPath());
				// Suffix examples follow the prefix answer already given
				const bool bPrefix = Options.bReplaceUnknownPrefix;
				Examples.Emplace(Issue->Asset.AssetName.ToString(),
					Issue->GetRenameTo(bSuffix && bPrefix, false), bSuffix ? Issue->GetRenameTo(bPrefix, true) : Issue->GetRenameTo(true, false));
			}
		}
		if (Examples.IsEmpty())
		{
			continue;
		}
		const int32 Choice = bSuffix
			? AskAddOrReplace(LOCTEXT("SuffixChoiceTitle", "Unknown Suffixes"),
				FText::Format(LOCTEXT("SuffixChoiceIntro", "{0} {0}|plural(one=asset ends,other=assets end) with a suffix the validator doesn't know. Keep it and add the expected suffix after it, or replace it?"), Examples.Num()),
				Examples, LOCTEXT("AddSuffix", "Add Suffix"), LOCTEXT("ReplaceSuffix", "Replace Suffix"))
			: AskAddOrReplace(LOCTEXT("PrefixChoiceTitle", "Unknown Prefixes"),
				FText::Format(LOCTEXT("PrefixChoiceIntro", "{0} {0}|plural(one=asset starts,other=assets start) with a prefix the validator doesn't know. Keep it and add the expected prefix in front, or replace it?"), Examples.Num()),
				Examples, LOCTEXT("AddPrefix", "Add Prefix"), LOCTEXT("ReplacePrefix", "Replace Prefix"));
		if (Choice == INDEX_NONE)
		{
			return FReply::Handled();
		}
		(bSuffix ? Options.bReplaceUnknownSuffix : Options.bReplaceUnknownPrefix) = Choice == 1;
	}

	// 2. Confirm
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel, FText::Format(
		LOCTEXT("ConfirmFix", "Fix {0} {2}{0}|plural(one=issue,other=issues) on {1} {1}|plural(one=asset,other=assets)?\n\n"
			"Assets are renamed to their expected names (references are updated, redirectors left) and settings are corrected. "
			"Changed assets are left unsaved: save them to keep the fixes."),
		Fixable.Num(), FixableAssets.Num(), bSelectedOnly ? LOCTEXT("SelectedWord", "selected ") : FText::GetEmpty()));
	if (Answer != EAppReturnType::Ok)
	{
		return FReply::Handled();
	}

	// 3. Fix (the issue pointers stay valid: AllIssues isn't touched until it's done)
	Section.List->ClearSelection();
	const FAssetFixResult Fix = AssetValidation::FixIssues(Fixable, Options);

	// 4. Check the fixed assets again
	const TSet<FSoftObjectPath> OldPaths(Fix.OldPaths);
	Section.AllIssues.RemoveAll([&OldPaths](const TSharedPtr<FAssetIssue>& Issue) { return OldPaths.Contains(Issue->Asset.GetSoftObjectPath()); });
	const FAssetValidationResult Recheck = AssetValidation::Validate(Fix.FixedAssets, GetSectionSettings(Type));
	for (const FAssetIssue& Issue : Recheck.Issues)
	{
		if (Issue.Type == Type)
		{
			Section.AllIssues.Add(MakeShared<FAssetIssue>(Issue));
		}
	}
	Section.AllIssues.StableSort([](const TSharedPtr<FAssetIssue>& A, const TSharedPtr<FAssetIssue>& B) { return AssetValidation::IssueLess(*A, *B); });
	RefreshSection(Section);

	// 5. Report
	VrrMenu::Notify(FText::Format(LOCTEXT("FixedSummary", "Fixed settings on {0} {0}|plural(one=asset,other=assets), renamed {1}. Save to keep the changes."),
		Fix.NumSettingsFixed, Fix.NumRenamed), VrrMenu::ENotify::Success);
	if (!Fix.SkippedRenames.IsEmpty())
	{
		TArray<FString> Shown(Fix.SkippedRenames.GetData(), FMath::Min(Fix.SkippedRenames.Num(), 5));
		VrrMenu::Notify(FText::Format(LOCTEXT("SkippedRenames", "Skipped {0} {0}|plural(one=rename,other=renames) (name taken or refused):\n{1}{2}"),
			Fix.SkippedRenames.Num(), FText::FromString(FString::Join(Shown, TEXT("\n"))),
			Fix.SkippedRenames.Num() > Shown.Num() ? LOCTEXT("AndMore", "\n...") : FText::GetEmpty()), VrrMenu::ENotify::Warning, 8.0f);
	}
	return FReply::Handled();
}

/** Save dialog (in the last export folder, or Saved/), then AssetValidation::ExportIssues. */
FReply SMenu_AssetValidation::OnExportClicked(EAssetType Type)
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (!DesktopPlatform)
	{
		return FReply::Handled();
	}
	const FResults& Section = GetResults(Type);
	const FString SectionName = GetResultsText(Type).Section.ToString();
	const FString DefaultFile = SectionName.Replace(TEXT(" "), TEXT("")) + FDateTime::Now().ToString(TEXT("_%Y%m%d_%H%M")) + TEXT(".json");
	const FString DefaultFolder = ExportFolder.IsEmpty() ? FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()) : ExportFolder;

	TArray<FString> Files;
	const void* ParentWindow = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(AsShared());
	if (!DesktopPlatform->SaveFileDialog(ParentWindow, FText::Format(LOCTEXT("ExportTitle", "Export {0} List"), GetResultsText(Type).Section).ToString(),
		DefaultFolder, DefaultFile, TEXT("JSON (*.json)|*.json"), EFileDialogFlags::None, Files) || Files.IsEmpty())
	{
		return FReply::Handled();
	}
	const FString FilePath = FPaths::ConvertRelativePathToFull(Files[0]);
	ExportFolder = FPaths::GetPath(FilePath);

	TArray<const FAssetIssue*> Issues;
	for (const TSharedPtr<FAssetIssue>& Issue : Section.AllIssues)
	{
		Issues.Add(Issue.Get());
	}
	if (AssetValidation::ExportIssues(FilePath, SectionName, FMath::Max(Section.NumScanned, 0), Issues))
	{
		VrrMenu::Notify(FText::Format(LOCTEXT("Exported", "Exported {0} {0}|plural(one=issue,other=issues) to {1}"), Issues.Num(), FText::FromString(FilePath)), VrrMenu::ENotify::Success);
	}
	else
	{
		VrrMenu::Notify(FText::Format(LOCTEXT("ExportFailed", "Couldn't write {0}"), FText::FromString(FilePath)), VrrMenu::ENotify::Error);
	}
	return FReply::Handled();
}

/** Fills a table's list with the issues that pass its All/Errors/Warnings filter. */
void SMenu_AssetValidation::ApplyFilter(FResults& Section)
{
	Section.Issues.Reset();
	for (const TSharedPtr<FAssetIssue>& Issue : Section.AllIssues)
	{
		const bool bError = Issue->Severity == FAssetIssue::ESeverity::Error;
		if (Section.Filter == EIssueFilter::All || (Section.Filter == EIssueFilter::Errors) == bError)
		{
			Section.Issues.Add(Issue);
		}
	}
	if (Section.List.IsValid())
	{
		Section.List->RequestListRefresh();
	}
}

void SMenu_AssetValidation::OnIssueSelected(TSharedPtr<FAssetIssue> Issue, ESelectInfo::Type SelectInfo)
{
	// Show the selected rows' assets in the Content Browser without stealing focus
	if (!Issue.IsValid() || SelectInfo == ESelectInfo::Direct)
	{
		return;
	}
	TArray<FAssetData> Assets;
	for (const FResults& Section : ResultLists)
	{
		if (Section.List.IsValid() && Section.List->IsItemSelected(Issue))
		{
			for (const TSharedPtr<FAssetIssue>& Selected : Section.List->GetSelectedItems())
			{
				Assets.AddUnique(Selected->Asset);
			}
		}
	}
	if (!Assets.IsEmpty())
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		ContentBrowser.Get().SyncBrowserToAssets(Assets, false, false);
	}
}

/** Opens the asset's editor (texture, mesh or material editor). */
void SMenu_AssetValidation::OnIssueDoubleClicked(TSharedPtr<FAssetIssue> Issue)
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
FText SMenu_AssetValidation::GetEmptyTitle(EAssetType Type) const
{
	const FResults& Section = GetResults(Type);
	if (Section.NumScanned == INDEX_NONE)
	{
		return LOCTEXT("NotScannedTitle", "Nothing scanned yet");
	}
	if (Section.NumScanned == 0)
	{
		return GetResultsText(Type).NoneFoundTitle;
	}
	if (Section.AllIssues.IsEmpty())
	{
		return FText::Format(GetResultsText(Type).PassedFormat, Section.NumScanned);
	}
	return Section.Filter == EIssueFilter::Errors ? LOCTEXT("NoErrorsTitle", "No errors") : LOCTEXT("NoWarningsTitle", "No warnings");
}

/** Line under the empty-state title: what to do next. */
FText SMenu_AssetValidation::GetEmptyHint(EAssetType Type) const
{
	const FResults& Section = GetResults(Type);
	if (Section.NumScanned == INDEX_NONE)
	{
		return IsValidating(Type)
			? LOCTEXT("NotScannedHint", "Scan a folder or the Content Browser selection.\nClick a result to find the asset, double-click to open it.")
			: GetResultsText(Type).OffHint;
	}
	if (Section.NumScanned == 0)
	{
		return GetResultsText(Type).NoneFoundHint;
	}
	if (Section.AllIssues.IsEmpty())
	{
		return LOCTEXT("AllPassedHint", "Every enabled check passed.");
	}
	return LOCTEXT("FilteredHint", "Switch the filter to All to see the other issues.");
}

#undef LOCTEXT_NAMESPACE
