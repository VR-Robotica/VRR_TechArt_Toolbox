// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Toolbox > VAT page (UI only).
 *
 * Flow: pick a skeletal mesh -> the page lists every Anim Sequence its skeleton can play -> tick the ones to
 * bake and set their order in the Data Asset (Bake Order card) -> Create makes a static mesh and AnimToTexture
 * Data Asset per LOD (Bone mode) -> Bake VAT bakes each LOD, merges them into
 * one static mesh, makes its material instances and places a preview actor (Core_VATBake.cpp).
 */

#include "Menu_VATBake.h"

#include "Animation/AnimSequence.h"
#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Input/DragAndDrop.h"
#include "Materials/MaterialInterface.h"
#include "MenuWidgets.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "TechArtToolboxSettings.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "SMenu_VATBake"

namespace
{
	/** Dragging a row of the Bake Order list onto another row moves it there. */
	class FVatAnimDragOp : public FDragDropOperation
	{
	public:
		DRAG_DROP_OPERATOR_TYPE(FVatAnimDragOp, FDragDropOperation)

		TSharedPtr<FVatAnimItem> Item;

		static TSharedRef<FVatAnimDragOp> New(const TSharedPtr<FVatAnimItem>& InItem)
		{
			TSharedRef<FVatAnimDragOp> Op = MakeShared<FVatAnimDragOp>();
			Op->Item = InItem;
			Op->Construct();
			return Op;
		}

		virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
		{
			return SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Menu.Background"))
				.Padding(FMargin(8.0f, 4.0f))
				[
					SNew(STextBlock).Text(FText::FromName(Item->Asset.AssetName))
				];
		}
	};

	/** Small borderless icon button for the Bake Order rows. */
	TSharedRef<SWidget> IconButton(FName Icon, const FText& ToolTip, TFunction<bool()> IsEnabled, TFunction<void()> OnClicked)
	{
		return SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText(ToolTip)
			.IsEnabled_Lambda(MoveTemp(IsEnabled))
			.OnClicked_Lambda([OnClicked = MoveTemp(OnClicked)]() { OnClicked(); return FReply::Handled(); })
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(Icon))
				.ColorAndOpacity(FSlateColor::UseForeground())
			];
	}
}

/** Builds the page: mesh, animations, bake order, bake mode, output and create cards. */
void SMenu_VATBake::Construct(const FArguments& InArgs)
{
	Settings.Material = UTechArtToolboxSettings::Get().DefaultVatMaterial.ToSoftObjectPath();
	RefreshMaterialInfo();

	ChildSlot
	[
		VrrMenu::Stack({
			CreateMeshSection(),

			// Animations and Bake Order side by side, same height (the list picks, the order arranges)
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Fill).Padding(0.0f, 0.0f, VrrMenu::CardGap * 0.5f, 0.0f)
			[
				CreateAnimationsSection()
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Fill).Padding(VrrMenu::CardGap * 0.5f, 0.0f, 0.0f, 0.0f)
			[
				CreateOrderSection()
			],

			CreateDataAssetSection(),
			CreateOutputSection(),
			CreateMaterialSection(),
			CreateBuildSection(),
		})
	];
}

// LAYOUT

/** Skeletal mesh picker (with "Use Selected"), and a line with its skeleton, bones and vertices. */
TSharedRef<SWidget> SMenu_VATBake::CreateMeshSection()
{
	return VrrMenu::HelpCard(LOCTEXT("MeshHeader", "Skeletal Mesh"),
		LOCTEXT("MeshHelp", "Builds the Data Asset that Unreal's AnimToTexture plugin bakes Vertex Animation Textures from. The skeletal mesh is the source the animations are played on."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("MeshLabel", "Skeletal mesh"),
				VrrMenu::MeshPicker(VrrMenu::EMeshTypes::SkeletalOnly,
					[this]() { return Mesh.Get(); },
					[this](UObject* NewMesh) { SetMesh(Cast<USkeletalMesh>(NewMesh)); },
					[this](const FText& Error) { SetStatus(Error, true); })),
			VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_VATBake::GetMeshInfo)),
		}));
}

/** Search box, All/None/Use Selected buttons, a count badge, and the checkbox list of animations. */
TSharedRef<SWidget> SMenu_VATBake::CreateAnimationsSection()
{
	TSharedRef<SWidget> CountBadge = VrrMenu::Badge(TAttribute<FText>::CreateLambda([this]()
	{
		return AllAnims.IsEmpty() ? FText::GetEmpty()
			: FText::Format(LOCTEXT("SelectedCount", "{0} of {1} selected"), CountSelected(), AllAnims.Num());
	}), VrrMenu::NeutralColor());

	auto HasAnims = [this]() { return !AllAnims.IsEmpty(); };

	TSharedRef<SWidget> EmptyState = SNew(STextBlock)
		.Font(VrrMenu::HelpFont())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Visibility_Lambda([this]() { return ShownAnims.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Text_Lambda([this]()
		{
			if (!Mesh.IsValid())
			{
				return LOCTEXT("EmptyNoMesh", "Pick a skeletal mesh first.");
			}
			if (AllAnims.IsEmpty())
			{
				return LOCTEXT("EmptyNoAnims", "No Animation Sequences use this mesh's skeleton.");
			}
			return LOCTEXT("EmptyNoMatch", "No animations match the search.");
		});

	return VrrMenu::Card(LOCTEXT("AnimsHeader", "Animations"), VrrMenu::Rows({
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search animations"))
			.IsEnabled_Lambda(HasAnims)
			.OnTextChanged_Lambda([this](const FText& NewText)
			{
				SearchText = NewText.ToString().TrimStartAndEnd();
				RefreshShownAnims();
			})
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("All", "All"))
			.ToolTipText(LOCTEXT("AllTip", "Tick every animation the list shows"))
			.IsEnabled_Lambda(HasAnims)
			.OnClicked_Lambda([this]() { SetShownSelected(true); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("None", "None"))
			.ToolTipText(LOCTEXT("NoneTip", "Untick every animation the list shows"))
			.IsEnabled_Lambda(HasAnims)
			.OnClicked_Lambda([this]() { SetShownSelected(false); return FReply::Handled(); })
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.Text(LOCTEXT("UseSelected", "Use Selected"))
			.ToolTipText(LOCTEXT("UseSelectedAnimsTip", "Tick the animations selected in the Content Browser"))
			.IsEnabled_Lambda(HasAnims)
			.OnClicked(this, &SMenu_VATBake::OnUseSelectedAnimsClicked)
		],

		// Fixed height: the page already scrolls, and a list needs a bounded height
		SNew(SBox)
		.HeightOverride(260.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SAssignNew(AnimList, SListView<TSharedPtr<FVatAnimItem>>)
				.ListItemsSource(&ShownAnims)
				.SelectionMode(ESelectionMode::None)
				.OnGenerateRow(this, &SMenu_VATBake::OnGenerateAnimRow)
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 40.0f, 0.0f, 0.0f)
			[
				EmptyState
			]
		],
	}), CountBadge);
}

/** The ticked animations in Data Asset order: drag rows or use the arrows to reorder, Sort by Name, Clear. */
TSharedRef<SWidget> SMenu_VATBake::CreateOrderSection()
{
	auto HasOrder = [this]() { return !BakeOrder.IsEmpty(); };

	TSharedRef<SWidget> EmptyState = SNew(STextBlock)
		.Font(VrrMenu::HelpFont())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Visibility_Lambda([this]() { return BakeOrder.IsEmpty() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		.Text(LOCTEXT("OrderEmpty", "Tick animations above to add them here."));

	return VrrMenu::HelpCard(LOCTEXT("OrderHeader", "Bake Order"),
		LOCTEXT("OrderHelp", "The order the animations are written to the Data Asset's animation array. The number on the left is each animation's index, which is what you pass to pick an animation in the VAT material.\n\n"
			"Newly ticked animations go to the end. Drag a row, or use the arrows, to move it."),
		VrrMenu::Rows({
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("SortByName", "Sort by Name"))
				.ToolTipText(LOCTEXT("SortByNameTip", "Order the ticked animations alphabetically"))
				.IsEnabled_Lambda(HasOrder)
				.OnClicked_Lambda([this]() { SortOrderByName(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ClearOrder", "Clear"))
				.ToolTipText(LOCTEXT("ClearOrderTip", "Untick every animation"))
				.IsEnabled_Lambda(HasOrder)
				.OnClicked_Lambda([this]()
				{
					for (const TSharedPtr<FVatAnimItem>& Item : AllAnims)
					{
						Item->bSelected = false;
					}
					BakeOrder.Reset();
					RefreshOrderList();
					return FReply::Handled();
				})
			],

			// Same height as the Animations list, so the two cards line up
			SNew(SBox)
			.HeightOverride(260.0f)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					// Single selection: rows only start a drag when the list can select
					SAssignNew(OrderList, SListView<TSharedPtr<FVatAnimItem>>)
					.ListItemsSource(&BakeOrder)
					.SelectionMode(ESelectionMode::Single)
					.OnGenerateRow(this, &SMenu_VATBake::OnGenerateOrderRow)
				]
				+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Top).Padding(0.0f, 40.0f, 0.0f, 0.0f)
				[
					EmptyState
				]
			],
		}));
}

/** What Create writes into the Data Assets: loop trimming, texture size and precision, bone influences, power of two. */
TSharedRef<SWidget> SMenu_VATBake::CreateDataAssetSection()
{
	return VrrMenu::HelpCard(LOCTEXT("DataAssetHeader", "Data Asset Settings"),
		LOCTEXT("DataAssetHelp", "Create writes these into every LOD's Data Asset. To change them afterwards, edit the Data Assets before baking, or Create again.\n\n"
			"The bake stores bones (AnimToTexture's Bone mode): per-bone position and rotation per frame, shared by every LOD and reusable by every mesh on this skeleton, plus a bone weight texture per LOD. That's what Bone mode VAT master materials read."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("LoopsLabel", "Loops"),
				VrrMenu::CheckBox(&Settings.bSeamlessLoops, LOCTEXT("SeamlessLoops", "Seamless loops"),
					LOCTEXT("SeamlessLoopsTip", "Leave out each animation's last key. In a looping animation it repeats the first pose, which shows as a hitch every loop. Untick for animations that don't loop and must end on their final pose."))),

			VrrMenu::Row(LOCTEXT("MaxSizeLabel", "Max texture size"),
				SNew(SSegmentedControl<int32>)
				.Value_Lambda([this]() { return Settings.MaxTextureSize; })
				.OnValueChanged_Lambda([this](int32 Size) { Settings.MaxTextureSize = Size; })
				+ SSegmentedControl<int32>::Slot(2048).Text(LOCTEXT("Size2048", "2048"))
				+ SSegmentedControl<int32>::Slot(4096).Text(LOCTEXT("Size4096", "4096"))
				+ SSegmentedControl<int32>::Slot(8192).Text(LOCTEXT("Size8192", "8192")),
				LOCTEXT("MaxSizeTip", "Largest width and height a baked texture may use (a smaller size is used when the data fits). Height is the total frames of all animations x rows per frame, so long animation sets need more.")),

			VrrMenu::Row(LOCTEXT("PrecisionLabel", "Precision"),
				SNew(SSegmentedControl<EAnimToTexturePrecision>)
				.Value_Lambda([this]() { return Settings.Precision; })
				.OnValueChanged_Lambda([this](EAnimToTexturePrecision Precision) { Settings.Precision = Precision; })
				+ SSegmentedControl<EAnimToTexturePrecision>::Slot(EAnimToTexturePrecision::EightBits).Text(LOCTEXT("EightBits", "8-bit"))
				+ SSegmentedControl<EAnimToTexturePrecision>::Slot(EAnimToTexturePrecision::SixteenBits).Text(LOCTEXT("SixteenBits", "16-bit")),
				LOCTEXT("PrecisionTip", "16-bit textures are smoother (less stepping in positions and weights) at twice the memory.")),

			VrrMenu::Row(LOCTEXT("InfluencesLabel", "Bone influences"),
				SNew(SSegmentedControl<EAnimToTextureNumBoneInfluences>)
				.Value_Lambda([this]() { return Settings.NumBoneInfluences; })
				.OnValueChanged_Lambda([this](EAnimToTextureNumBoneInfluences Influences) { Settings.NumBoneInfluences = Influences; })
				+ SSegmentedControl<EAnimToTextureNumBoneInfluences>::Slot(EAnimToTextureNumBoneInfluences::One).Text(LOCTEXT("OneInfluence", "1"))
				+ SSegmentedControl<EAnimToTextureNumBoneInfluences>::Slot(EAnimToTextureNumBoneInfluences::Two).Text(LOCTEXT("TwoInfluences", "2"))
				+ SSegmentedControl<EAnimToTextureNumBoneInfluences>::Slot(EAnimToTextureNumBoneInfluences::Four).Text(LOCTEXT("FourInfluences", "4")),
				LOCTEXT("InfluencesTip", "Bones blended per vertex. More is smoother at joints and costs more in the vertex shader. Also sets the material's Use Two / Use Four Influences switches.")),

			VrrMenu::Row(LOCTEXT("PowerOfTwoLabel", "Power of two"),
				VrrMenu::CheckBox(&Settings.bEnforcePowerOfTwo, LOCTEXT("PowerOfTwo", "Enforce power-of-two textures"),
					LOCTEXT("PowerOfTwoTip", "Round texture sizes up to powers of two (wastes some space; only needed for platforms or tools that require it)."))),

			VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
			{
				const int32 NumBones = Mesh.IsValid() ? Mesh->GetRefSkeleton().GetRawBoneNum() : 0;
				return Settings.Precision == EAnimToTexturePrecision::EightBits && NumBones > 256
					? FText::Format(LOCTEXT("ForcedSixteen", "This skeleton has {0} bones, more than 8-bit weights can index, so Create uses 16-bit."), NumBones)
					: FText::GetEmpty();
			})),
		}));
}

/** Master material for the material instances (per mode), what it supports, and the preview actor option. */
TSharedRef<SWidget> SMenu_VATBake::CreateMaterialSection()
{
	return VrrMenu::HelpCard(LOCTEXT("MaterialHeader", "Material & Preview"),
		LOCTEXT("MaterialHelp", "Bake makes MI_<name>, an instance of this master material with the baked settings, and a child of it for every material slot of every LOD with that LOD's bone weights. Default picks the master set in Project Settings > Plugins > TechArt Toolbox.\n\n"
			"Parameters the master doesn't have are skipped; the note below lists required ones that are missing. Clear the master to skip the material instances.\n\n"
			"The preview actor shows SM_<name> at the level origin. Its class, and a component that drives VAT Selection so you can switch animations on it, can be set in Project Settings. Re-baking updates the same actor."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("MasterLabel", "Master material"),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(VrrMenu::WideWidth)
					[
						SNew(SObjectPropertyEntryBox)
						.AllowedClass(UMaterialInterface::StaticClass())
						.ObjectPath_Lambda([this]() { return Settings.Material.ToString(); })
						.OnObjectChanged_Lambda([this](const FAssetData& AssetData)
						{
							Settings.Material = AssetData.IsValid() ? AssetData.GetSoftObjectPath() : FSoftObjectPath();
							RefreshMaterialInfo();
						})
						.DisplayThumbnail(true)
					]
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SButton)
					.Text(LOCTEXT("DefaultMaterial", "Default"))
					.ToolTipText(LOCTEXT("DefaultMaterialTip", "Use the default master set in Project Settings > Plugins > TechArt Toolbox"))
					.Visibility_Lambda([]() { return UTechArtToolboxSettings::Get().DefaultVatMaterial.IsNull() ? EVisibility::Collapsed : EVisibility::Visible; })
					.OnClicked_Lambda([this]()
					{
						Settings.Material = UTechArtToolboxSettings::Get().DefaultVatMaterial.ToSoftObjectPath();
						RefreshMaterialInfo();
						return FReply::Handled();
					})
				]),
			VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_VATBake::GetMaterialInfo)),
			VrrMenu::Row(LOCTEXT("PreviewLabel", "Preview"),
				VrrMenu::CheckBox(&Settings.bSpawnPreviewActor, LOCTEXT("SpawnPreview", "Place a preview actor after baking"),
					LOCTEXT("SpawnPreviewTip", "Place SM_<name> at the level origin and select it (or update the actor an earlier bake placed)"))),
		}));
}

/** The master's fit (cached by RefreshMaterialInfo), plus a warning when more animations are ticked than it has frame ranges for. */
FText SMenu_VATBake::GetMaterialInfo() const
{
	if (!bMaterialHasProblem && MaterialAnimationSlots > 0 && BakeOrder.Num() > MaterialAnimationSlots)
	{
		return FText::Format(LOCTEXT("MaterialSlotsExceeded", "{0}\n{1} animations are ticked; only the first {2} in the bake order get frame ranges in the material."),
			MaterialFitText, BakeOrder.Num(), MaterialAnimationSlots);
	}
	return MaterialFitText;
}

void SMenu_VATBake::RefreshMaterialInfo()
{
	const FSoftObjectPath& Path = Settings.Material;
	const UMaterialInterface* Material = Path.IsNull() ? nullptr : Cast<UMaterialInterface>(Path.TryLoad());
	MaterialAnimationSlots = VatBaker::GetNumAnimationSlots(Material);
	bMaterialHasProblem = false;

	if (Path.IsNull())
	{
		MaterialFitText = LOCTEXT("NoMaster", "No master material, so Bake won't make material instances.");
		return;
	}
	if (!Material)
	{
		bMaterialHasProblem = true;
		MaterialFitText = FText::Format(LOCTEXT("MasterNotFound", "{0} wasn't found."), FText::FromString(Path.ToString()));
		return;
	}

	TArray<FString> Missing;
	for (const FName& Name : VatBaker::FindMissingParameters(Material, Settings.Mode))
	{
		Missing.Add(Name.ToString());
	}
	bMaterialHasProblem = !Missing.IsEmpty();
	MaterialFitText = bMaterialHasProblem
		? FText::Format(LOCTEXT("MasterMissing", "This isn't a bone VAT material. It's missing: {0}."), FText::FromString(FString::Join(Missing, TEXT(", "))))
		: FText::Format(LOCTEXT("MasterFits", "Has every parameter Bake needs, with frame ranges for {0} animation(s)."), MaterialAnimationSlots);
}

/** Output folder and base name, with the asset names they give. */
TSharedRef<SWidget> SMenu_VATBake::CreateOutputSection()
{
	return VrrMenu::Card(LOCTEXT("OutputHeader", "Output"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("Name", "Base name"),
			VrrMenu::NameField(&Settings.OutputName),
			LOCTEXT("NameTip", "Every asset is named from this: DA_<name>_LOD0, SM_<name>, T_<name>_BonePos... A number is added if any of them already exists; existing assets are never overwritten")),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_VATBake::GetOutputPreview)),
	}));
}

/** Step 1 Create, step 2 Bake VAT, what Create made, and the status line. */
TSharedRef<SWidget> SMenu_VATBake::CreateBuildSection()
{
	return VrrMenu::HelpCard(LOCTEXT("BuildHeader", "Create & Bake"),
		LOCTEXT("BuildHelp", "Create converts each LOD of the skeletal mesh to a static mesh and makes an AnimToTexture Data Asset for it, with empty textures. You can still edit the Data Assets (frame ranges, sample rate, texture size) before baking.\n\n"
			"Bake VAT runs AnimToTexture on every LOD's Data Asset, then copies the baked LODs into one static mesh. Each LOD gets its own material slots (_LOD#), since each LOD reads its own bone weight texture.\n\n"
			"It also makes the material instances (see Material & Preview) and places the preview actor. Re-baking updates them in place."),
		VrrMenu::Rows({
			VrrMenu::ActionButton(LOCTEXT("Create", "1. Create Data Assets"),
				LOCTEXT("CreateTip", "Create a static mesh, Data Asset and textures for every LOD of this mesh, with these animations and this mode"),
				[this]()
				{
					if (!Mesh.IsValid())
					{
						return LOCTEXT("NeedMesh", "Pick a skeletal mesh first.");
					}
					return CountSelected() > 0 ? FText::GetEmpty() : LOCTEXT("NeedAnims", "Tick at least one animation.");
				},
				FOnClicked::CreateSP(this, &SMenu_VATBake::OnCreateClicked)),
			VrrMenu::ActionButton(LOCTEXT("Bake", "2. Bake VAT"),
				LOCTEXT("BakeTip", "Bake the textures of every LOD, then build one static mesh with all LODs (rebuilt in place when baked again)"),
				[this]()
				{
					return CreatedSet.IsEmpty() ? LOCTEXT("NeedCreate", "Create the Data Assets first.") : FText::GetEmpty();
				},
				FOnClicked::CreateSP(this, &SMenu_VATBake::OnBakeClicked)),
			VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_VATBake::GetCreatedSetInfo)),
			VrrMenu::StatusLine(&StatusText, &bStatusIsError),
		}));
}

/** List row: checkbox, animation name, its folder (dimmed), and its index in the bake order once ticked. */
TSharedRef<ITableRow> SMenu_VATBake::OnGenerateAnimRow(TSharedPtr<FVatAnimItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(STableRow<TSharedPtr<FVatAnimItem>>, OwnerTable)
		.Padding(FMargin(4.0f, 2.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([Item]() { return Item->bSelected ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this, Item](ECheckBoxState State) { SetItemSelected(Item, State == ECheckBoxState::Checked); })
				.ToolTipText(FText::FromString(Item->Asset.GetObjectPathString()))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock).Text(FText::FromName(Item->Asset.AssetName))
					]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(12.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromName(Item->Asset.PackagePath))
						.Font(VrrMenu::HelpFont())
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
					]
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(VrrMenu::HelpFont())
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.ToolTipText(LOCTEXT("AnimIndexTip", "Index in the Data Asset's animation array (see Bake Order)"))
				.Text_Lambda([this, Item]()
				{
					const int32 Index = BakeOrder.IndexOfByKey(Item);
					return Index == INDEX_NONE ? FText::GetEmpty() : FText::Format(LOCTEXT("AnimIndex", "#{0}"), Index);
				})
			]
		];
}

/** Bake Order row: index, drag handle, name, and move up / move down / remove buttons. Drag onto another row to move it. */
TSharedRef<ITableRow> SMenu_VATBake::OnGenerateOrderRow(TSharedPtr<FVatAnimItem> Item, const TSharedRef<STableViewBase>& OwnerTable)
{
	auto IndexOf = [this, Item]() { return BakeOrder.IndexOfByKey(Item); };

	return SNew(STableRow<TSharedPtr<FVatAnimItem>>, OwnerTable)
		.Padding(FMargin(4.0f, 1.0f))
		.ToolTipText(FText::FromString(Item->Asset.GetObjectPathString()))
		.OnDragDetected_Lambda([Item](const FGeometry&, const FPointerEvent&)
		{
			return FReply::Handled().BeginDragDrop(FVatAnimDragOp::New(Item));
		})
		.OnCanAcceptDrop_Lambda([](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FVatAnimItem>) -> TOptional<EItemDropZone>
		{
			if (!Event.GetOperationAs<FVatAnimDragOp>().IsValid())
			{
				return TOptional<EItemDropZone>();
			}
			// Insert between rows only; dropping "onto" a row means just below it
			return Zone == EItemDropZone::AboveItem ? EItemDropZone::AboveItem : EItemDropZone::BelowItem;
		})
		.OnAcceptDrop_Lambda([this](const FDragDropEvent& Event, EItemDropZone Zone, TSharedPtr<FVatAnimItem> Target)
		{
			const TSharedPtr<FVatAnimDragOp> Op = Event.GetOperationAs<FVatAnimDragOp>();
			if (!Op.IsValid() || !Op->Item.IsValid() || Op->Item == Target)
			{
				return FReply::Unhandled();
			}
			BakeOrder.Remove(Op->Item);
			const int32 TargetIndex = BakeOrder.IndexOfByKey(Target);
			BakeOrder.Insert(Op->Item, Zone == EItemDropZone::AboveItem ? TargetIndex : TargetIndex + 1);
			RefreshOrderList();
			return FReply::Handled();
		})
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(28.0f)
				[
					SNew(STextBlock)
					.Font(VrrMenu::HelpFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Justification(ETextJustify::Right)
					.Text_Lambda([IndexOf]() { return FText::AsNumber(IndexOf()); })
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 4.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.DragHandle"))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).Padding(4.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromName(Item->Asset.AssetName))
				.OverflowPolicy(ETextOverflowPolicy::Ellipsis)
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				IconButton("Icons.ChevronUp", LOCTEXT("MoveUpTip", "Move up"),
					[IndexOf]() { return IndexOf() > 0; },
					[this, Item, IndexOf]() { MoveInOrder(Item, IndexOf() - 1); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				IconButton("Icons.ChevronDown", LOCTEXT("MoveDownTip", "Move down"),
					[this, IndexOf]() { return IndexOf() < BakeOrder.Num() - 1; },
					[this, Item, IndexOf]() { MoveInOrder(Item, IndexOf() + 1); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				IconButton("Icons.X", LOCTEXT("RemoveTip", "Untick this animation"),
					[]() { return true; },
					[this, Item]() { SetItemSelected(Item, false); })
			]
		];
}

// BEHAVIOR

/** Switches to a new mesh: suggests the output and lists the animations its skeleton can play (none ticked). */
void SMenu_VATBake::SetMesh(USkeletalMesh* NewMesh)
{
	Mesh = NewMesh;
	StatusText = FText::GetEmpty();

	AllAnims.Reset();
	BakeOrder.Reset();
	RefreshOrderList();
	for (const FAssetData& Asset : VatBaker::FindAnimations(NewMesh))
	{
		TSharedPtr<FVatAnimItem> Item = MakeShared<FVatAnimItem>();
		Item->Asset = Asset;
		AllAnims.Add(Item);
	}

	if (NewMesh)
	{
		Settings.OutputFolder = FPackageName::GetLongPackagePath(NewMesh->GetOutermost()->GetName()) / TEXT("VAT");
		Settings.OutputName = VatBaker::SuggestName(NewMesh);
	}
	RefreshShownAnims();
}

/** Re-applies the search box to the list. */
void SMenu_VATBake::RefreshShownAnims()
{
	ShownAnims.Reset();
	for (const TSharedPtr<FVatAnimItem>& Item : AllAnims)
	{
		if (SearchText.IsEmpty() || Item->Asset.AssetName.ToString().Contains(SearchText))
		{
			ShownAnims.Add(Item);
		}
	}
	if (AnimList.IsValid())
	{
		AnimList->RequestListRefresh();
	}
}

/** Ticks or unticks every animation the list shows (the search box narrows it), in list order. */
void SMenu_VATBake::SetShownSelected(bool bSelected)
{
	for (const TSharedPtr<FVatAnimItem>& Item : ShownAnims)
	{
		SetItemSelected(Item, bSelected);
	}
}

/** Ticking adds the animation to the end of the bake order; unticking takes it out. */
void SMenu_VATBake::SetItemSelected(const TSharedPtr<FVatAnimItem>& Item, bool bSelected)
{
	if (!Item.IsValid() || Item->bSelected == bSelected)
	{
		return;
	}
	Item->bSelected = bSelected;
	if (bSelected)
	{
		BakeOrder.Add(Item);
	}
	else
	{
		BakeOrder.Remove(Item);
	}
	RefreshOrderList();
}

/** Moves an animation to another index of the bake order (clamped). */
void SMenu_VATBake::MoveInOrder(const TSharedPtr<FVatAnimItem>& Item, int32 NewIndex)
{
	const int32 OldIndex = BakeOrder.IndexOfByKey(Item);
	NewIndex = FMath::Clamp(NewIndex, 0, BakeOrder.Num() - 1);
	if (OldIndex == INDEX_NONE || OldIndex == NewIndex)
	{
		return;
	}
	BakeOrder.RemoveAt(OldIndex);
	BakeOrder.Insert(Item, NewIndex);
	RefreshOrderList();
}

void SMenu_VATBake::SortOrderByName()
{
	BakeOrder.Sort([](const TSharedPtr<FVatAnimItem>& A, const TSharedPtr<FVatAnimItem>& B)
	{
		return A->Asset.AssetName.LexicalLess(B->Asset.AssetName);
	});
	RefreshOrderList();
}

void SMenu_VATBake::RefreshOrderList()
{
	if (OrderList.IsValid())
	{
		OrderList->RequestListRefresh();
	}
}

int32 SMenu_VATBake::CountSelected() const
{
	return BakeOrder.Num();
}

/** "Skeleton SK_Mannequin  ·  68 bones  ·  4 LODs  ·  12,345 vertices (LOD 0)". */
FText SMenu_VATBake::GetMeshInfo() const
{
	const USkeletalMesh* SkeletalMesh = Mesh.Get();
	if (!SkeletalMesh)
	{
		return FText::GetEmpty();
	}

	const USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
	const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	const int32 NumVertices = RenderData && !RenderData->LODRenderData.IsEmpty() ? RenderData->LODRenderData[0].GetNumVertices() : 0;

	return FText::Format(LOCTEXT("MeshInfo", "Skeleton {0}  \u00B7  {1} bones  \u00B7  {2} {2}|plural(one=LOD,other=LODs)  \u00B7  {3} vertices (LOD 0)"),
		Skeleton ? FText::FromString(Skeleton->GetName()) : LOCTEXT("NoSkeleton", "none"),
		SkeletalMesh->GetRefSkeleton().GetRawBoneNum(),
		VatBaker::GetNumLODs(SkeletalMesh),
		NumVertices);
}

/** The assets Create and Bake will make with the current mesh, mode and name. */
FText SMenu_VATBake::GetOutputPreview() const
{
	if (!Mesh.IsValid() || Settings.OutputName.IsEmpty())
	{
		return FText::GetEmpty();
	}

	const FText Name = FText::FromString(Settings.OutputName);
	const int32 LastLOD = VatBaker::GetNumLODs(Mesh.Get()) - 1;
	return FText::Format(LOCTEXT("PreviewBone", "Per LOD (0-{1}): DA_{0}_LOD#, SM_{0}_LOD#, T_{0}_BoneWeights_LOD#\nShared: T_{0}_BonePos, T_{0}_BoneRot\nBake makes SM_{0} with every LOD, MI_{0} and MI_{0}_<Slot>_LOD# per material slot."), Name, LastLOD);
}

/** What Bake VAT will work on (from the last Create), or empty before Create. */
FText SMenu_VATBake::GetCreatedSetInfo() const
{
	if (CreatedSet.IsEmpty())
	{
		return FText::GetEmpty();
	}

	const FText MeshName = CreatedSet.SkeletalMesh.IsValid() ? FText::FromString(CreatedSet.SkeletalMesh->GetName()) : LOCTEXT("MissingMesh", "(missing mesh)");
	return FText::Format(LOCTEXT("SetInfo", "Bake VAT uses DA_{0}_LOD0-{1} in {2} ({3}). Animations and settings are read from those Data Assets."),
		FText::FromString(CreatedSet.BaseName), CreatedSet.DataAssets.Num() - 1, FText::FromString(CreatedSet.Folder), MeshName);
}

/** Ticks every listed animation that is selected in the Content Browser; reports the ones this skeleton can't use. */
FReply SMenu_VATBake::OnUseSelectedAnimsClicked()
{
	int32 NumTicked = 0, NumSkipped = 0;
	for (const FAssetData& Asset : VrrCore::GetSelectedAssets())
	{
		if (!Asset.IsInstanceOf(UAnimSequence::StaticClass()))
		{
			continue;
		}
		const TSharedPtr<FVatAnimItem>* Item = AllAnims.FindByPredicate([&Asset](const TSharedPtr<FVatAnimItem>& I) { return I->Asset.PackageName == Asset.PackageName; });
		if (Item)
		{
			SetItemSelected(*Item, true);
			++NumTicked;
		}
		else
		{
			++NumSkipped;
		}
	}

	if (NumTicked == 0 && NumSkipped == 0)
	{
		SetStatus(LOCTEXT("NoAnimsSelected", "Select Animation Sequences in the Content Browser first."), true);
	}
	else if (NumSkipped > 0)
	{
		SetStatus(FText::Format(LOCTEXT("AnimsSkipped", "Ticked {0}. {1} selected animation(s) use a different skeleton and were skipped."), NumTicked, NumSkipped), true);
	}
	else
	{
		SetStatus(FText::Format(LOCTEXT("AnimsTicked", "Ticked {0} animation(s)."), NumTicked), false);
	}
	return FReply::Handled();
}

/** Text (and error color) for the status line under the Create button. */
void SMenu_VATBake::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

/** Loads the ticked animations in bake order, creates the per-LOD meshes, Data Assets and textures, and reports the result. */
FReply SMenu_VATBake::OnCreateClicked()
{
	TArray<UAnimSequence*> Animations;
	for (const TSharedPtr<FVatAnimItem>& Item : BakeOrder)
	{
		if (UAnimSequence* Animation = Cast<UAnimSequence>(Item->Asset.GetAsset()))
		{
			Animations.Add(Animation);
		}
	}

	FVatAssetSet NewSet;
	const FVatResult Result = VatBaker::CreateAssets(Mesh.Get(), Animations, Settings, NewSet);
	if (Result.bSuccess)
	{
		CreatedSet = NewSet;
	}
	ReportResult(Result, LOCTEXT("CreatedToast", "VAT Data Assets created"));
	return FReply::Handled();
}

/** Bakes every LOD of the created set and builds the combined static mesh. */
FReply SMenu_VATBake::OnBakeClicked()
{
	ReportResult(VatBaker::Bake(CreatedSet, Settings), LOCTEXT("BakedToast", "Vertex Animation Textures baked"));
	return FReply::Handled();
}

/** Status line, toast, and selects the assets in the Content Browser. */
void SMenu_VATBake::ReportResult(const FVatResult& Result, const FText& SuccessToast)
{
	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? SuccessToast : Result.Message, Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error, 6.0f);
	if (!Result.Assets.IsEmpty())
	{
		VrrCore::ShowInContentBrowser(Result.Assets);
	}
}

#undef LOCTEXT_NAMESPACE
