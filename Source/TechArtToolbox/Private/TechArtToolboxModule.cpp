// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * TechArtToolbox module: registers the Toolbox window and the menu/toolbar entries that open it.
 *
 * The window is a dockable tab: a header image on top, a page toolbar on the left, and a widget switcher
 * holding one page per tool (Validate, Texture Packer, Bake Normals, UV Layouts, Rebake, Consolidate, Mesh Maps, Control Rig, VAT).
 * Each page lives in its own Menu_<Page> widget (UI) backed by Core_<Page> (the actual work), so this file
 * only wires things together.
 */

#include "TechArtToolboxModule.h"
#include "TechArtToolboxStyle.h"
#include "TechArtToolboxCommands.h"
#include "Menu_TexturePacker.h"
#include "Menu_AssetValidation.h"
#include "Menu_BakeNormals.h"
#include "Menu_UVLayout.h"
#include "Menu_RebakeTexture.h"
#include "Menu_ConsolidateMaterials.h"
#include "Menu_BakeMaps.h"
#include "Menu_EdgeMesh.h"
#include "Menu_ControlRig.h"
#include "Menu_VATBake.h"

#include "ToolMenus.h"
#include "LevelEditor.h"

#include "Animation/Skeleton.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"
#include "MenuWidgets.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/SOverlay.h"

static const FName TechArtToolboxTabName("TechArtToolbox");

#define LOCTEXT_NAMESPACE "TechArtToolbox"


// MODULE BEHAVIORS

/** Loads the style, maps the commands, registers the Toolbox tab spawner and the menus. */
void FTechArtToolboxModule::StartupModule()
{
	FTechArtToolboxStyle::Initialize();
	FTechArtToolboxStyle::ReloadTextures();
	
	MapCommandListActions();
	
	// Register a function to be called when menu system is initialized
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FTechArtToolboxModule::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TechArtToolboxTabName, FOnSpawnTab::CreateRaw(this, &FTechArtToolboxModule::OnSpawnTabWindow))
	.SetDisplayName(LOCTEXT("Toolbox", "TechArt Toolbox"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);
}

/** Undoes StartupModule: menus, style, commands and the tab spawner. */
void FTechArtToolboxModule::ShutdownModule()
{
	// Unregister the startup function
	UToolMenus::UnRegisterStartupCallback(this);
	
	// Unregister all our menu extensions
	UToolMenus::UnregisterOwner(this);

	// Unregister style refs
	FTechArtToolboxStyle::Shutdown();

	// Unregister command lists
	FTechArtToolboxCommands::Unregister();

	// Unregister nomad tab window
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TechArtToolboxTabName);
}

/** Adds every menu and toolbar entry (called once the ToolMenus system is ready). */
void FTechArtToolboxModule::RegisterMenus()
{
	// Owner will be used for cleanup in call to UToolMenus::UnregisterOwner
	FToolMenuOwnerScoped OwnerScoped(this);
	CreateTechArtMainMenu();
	ExtendTechArtMenu(); // create entries in our new main menu "TechArt"
	ExtendWindowMenu();  // create an entry in the "Window" Menu
	ExtendPlayToolBar(); // create a new icon entry next to the play button (PIE) toolbar
}



// MENUS
/** Adds the "TechArt" menu to the Level Editor main menu bar. */
void FTechArtToolboxModule::CreateTechArtMainMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
	
	FToolMenuSection& MenuSection = Menu->FindOrAddSection(NAME_None);
	
	FToolMenuEntry& MenuEntry =
		MenuSection.AddSubMenu(
			"TechArtToolbox",
			LOCTEXT("Label", "TechArt"),
			LOCTEXT("ToolTip", "TechArt Toolbox tools"),
			FNewToolMenuChoice()
		);

	MenuEntry.InsertPosition = FToolMenuInsert(NAME_None, EToolMenuInsertType::Last);
}

/** Fills the TechArt menu with the entry that opens the Toolbox. */
void FTechArtToolboxModule::ExtendTechArtMenu()
{
	UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.TechArtToolbox");
	FToolMenuSection& Section = MainMenu->FindOrAddSection("TechArtToolbox", LOCTEXT("ToolboxSection", "Toolbox"));
	Section.AddMenuEntryWithCommandList(FTechArtToolboxCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList);
}


// OTHER MENU ENTRIES
/** Window menu entry that opens Toolbox. */
void FTechArtToolboxModule::ExtendWindowMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
	Section.AddMenuEntryWithCommandList(FTechArtToolboxCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList);
}

/** Toolbox button next to the Play button. */
void FTechArtToolboxModule::ExtendPlayToolBar()
{
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("TechArtToolbox");
	
	FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FTechArtToolboxCommands::Get().CMD_OpenPopUpWindow));
	Entry.SetCommandList(EditorToolsCommandList);
}


// POPUP WINDOW
/** Every page with its sidebar label, header title/description and icon, in sidebar order. */
const TArray<FTechArtToolboxModule::FPageInfo>& FTechArtToolboxModule::GetPages()
{
	static const TArray<FPageInfo> Pages =
	{
		{ EToolPage::AssetValidation, LOCTEXT("ValidateTab", "Validate"), LOCTEXT("ValidateTitle", "Asset Validation"),
			LOCTEXT("ValidateDesc", "Check textures and meshes against the project's naming conventions and asset rules."), "VrrTools.Page.Validate" },
		{ EToolPage::TexturePacker, LOCTEXT("PackerTab", "Packer"), LOCTEXT("PackerTitle", "Texture Packer"),
			LOCTEXT("PackerDesc", "Combine channels from several textures into one packed mask."), "VrrTools.Page.Packer" },
		{ EToolPage::BakeNormals, LOCTEXT("NormalsTab", "Normals"), LOCTEXT("NormalsTitle", "Bake Smooth Normals"),
			LOCTEXT("NormalsDesc", "Bake fully smooth normals into vertex colors or a texture, for inverted-hull toon outlines."), "VrrTools.Page.Normals" },
		{ EToolPage::UVLayout, LOCTEXT("UVLayoutTab", "UV Layouts"), LOCTEXT("UVLayoutTitle", "UV Layouts"),
			LOCTEXT("UVLayoutDesc", "Build a unique UV channel (usually UV1) from a mesh's islands, with even texel density and one region per material."), "VrrTools.Page.UVLayout" },
		{ EToolPage::RebakeTexture, LOCTEXT("RebakeTab", "Rebake"), LOCTEXT("RebakeTitle", "Rebake Texture Map to UV Set"),
			LOCTEXT("RebakeDesc", "Move a texture from one UV channel of a mesh to another: sampled through the source UVs, written through the target UVs."), "VrrTools.Page.Rebake" },
		{ EToolPage::ConsolidateMaterials, LOCTEXT("ConsolidateTab", "Consolidate"), LOCTEXT("ConsolidateTitle", "Consolidate Materials Texture Sets"),
			LOCTEXT("ConsolidateDesc", "Bake every material on a multi-material mesh (Base Color, Metallic, Roughness, Normal...) through one UV channel into a single texture set."), "VrrTools.Page.Consolidate" },
		{ EToolPage::BakeMaps, LOCTEXT("MapsTab", "Mesh Maps"), LOCTEXT("MapsTitle", "Bake Mesh Maps"),
			LOCTEXT("MapsDesc", "Bake Cavity, Edge, Curvature and AO masks from a mesh's own geometry."), "VrrTools.Page.MeshMaps" },
		{ EToolPage::EdgeMesh, LOCTEXT("EdgeMeshTab", "Edges"), LOCTEXT("EdgeMeshTitle", "Edge Mesh"),
			LOCTEXT("EdgeMeshDesc", "Build strips along a static mesh's sharp outer edges, as their own mesh for edge-wear decal materials."), "VrrTools.Page.EdgeMesh" },
		{ EToolPage::ControlRig, LOCTEXT("RigTab", "Rig"), LOCTEXT("RigTitle", "Control Rig Generator"),
			LOCTEXT("RigDesc", "Generate a Modular Control Rig for a character's skeletal mesh."), "VrrTools.Page.Rig" },
		{ EToolPage::VATBake, LOCTEXT("VATTab", "VAT"), LOCTEXT("VATTitle", "Vertex Animation Textures"),
			LOCTEXT("VATDesc", "Set up an AnimToTexture Data Asset: pick a skeletal mesh, its animations, and Bone or Vertex baking."), "VrrTools.Page.VAT" },
	};
	return Pages;
}

const FTechArtToolboxModule::FPageInfo& FTechArtToolboxModule::GetPageInfo(EToolPage Page)
{
	const FPageInfo* Info = GetPages().FindByPredicate([Page](const FPageInfo& P) { return P.Page == Page; });
	return Info ? *Info : GetPages()[0];
}

/** Builds the Toolbox tab: header bar on top, sidebar on the left, and the switcher with one Menu_<Page> widget per tool. */
TSharedRef<SDockTab> FTechArtToolboxModule::OnSpawnTabWindow(const FSpawnTabArgs& SpawnTabArgs)
{
	// One scrolling page per tool. Slot order must match EToolPage.
	auto PageSlot = [](const TSharedRef<SWidget>& Page) -> TSharedRef<SWidget>
	{
		return SNew(SScrollBox)
			+ SScrollBox::Slot().Padding(16.0f, 16.0f, 16.0f, 4.0f)
			[
				Page
			];
	};

	return SNew(SDockTab)
	.TabRole(ETabRole::NomadTab)
	[
		SNew(SVerticalBox)

		// 1. Header bar: logo, then the active page's title and description
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			CreateCustomHeader()
		]

		// 2. Sidebar and the active page
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				CreateLeftToolBar()
			]

			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SWidgetSwitcher)
				.WidgetIndex_Lambda([this]() { return static_cast<int32>(ActivePage); })
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_TexturePacker)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_AssetValidation)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_BakeNormals)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_UVLayout)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_RebakeTexture)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_BakeMaps)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_ControlRig)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_VATBake)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_ConsolidateMaterials)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_EdgeMesh)) ]
			]
		]
	];
}

/** Header bar: the TechArt Toolbox logo, a divider, and the active page's title and description. */
TSharedRef<SWidget> FTechArtToolboxModule::CreateCustomHeader()
{
	return SNew(SBorder)
	.BorderImage(FTechArtToolboxStyle::Get().GetBrush("VrrTools.Frame"))
	.Padding(FMargin(12.0f, 6.0f))
	[
		SNew(SHorizontalBox)

		// Logo
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FTechArtToolboxStyle::Get().GetBrush("VrrTools.Logo"))
		]

		// Divider
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(16.0f, 10.0f)
		[
			SNew(SSeparator)
			.Orientation(Orient_Vertical)
			.Thickness(1.0f)
		]

		// Active page
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetPageInfo(ActivePage).Title; })
				.Font(VrrMenu::TitleFont())
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 3.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetPageInfo(ActivePage).Description; })
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.AutoWrapText(true)
			]
		]
	];
}

/** Sidebar: one tab per page, top to bottom. */
TSharedRef<SWidget> FTechArtToolboxModule::CreateLeftToolBar()
{
	TSharedRef<SVerticalBox> Tabs = SNew(SVerticalBox);
	for (const FPageInfo& Info : GetPages())
	{
		Tabs->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ CreatePageTab(Info) ];
	}

	return SNew(SBorder)
	.BorderImage(FTechArtToolboxStyle::Get().GetBrush("VrrTools.Frame"))
	.Padding(FMargin(6.0f, 10.0f))
	[
		SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			Tabs
		]
	];
}

/** Flat button showing the page's icon over its label. Active: filled background, accent bar, full-brightness icon. */
TSharedRef<SWidget> FTechArtToolboxModule::CreatePageTab(const FPageInfo& Info)
{
	const EToolPage Page = Info.Page;
	auto IsActive = [this, Page]() { return ActivePage == Page; };
	auto Tint = [IsActive]() { return IsActive() ? FSlateColor::UseForeground() : FSlateColor::UseSubduedForeground(); };

	return SNew(SBox)
	.WidthOverride(80.0f)
	.HeightOverride(60.0f)
	.ToolTipText(Info.Description)
	[
		SNew(SOverlay)

		// Active background
		+ SOverlay::Slot()
		[
			SNew(SImage)
			.Image(FTechArtToolboxStyle::Get().GetBrush("VrrTools.Tab.Active"))
			.Visibility_Lambda([IsActive]() { return IsActive() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
		]

		// Accent bar on the left edge
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Fill)
		.Padding(0.0f, 12.0f)
		[
			SNew(SBox)
			.WidthOverride(3.0f)
			.Visibility_Lambda([IsActive]() { return IsActive() ? EVisibility::HitTestInvisible : EVisibility::Hidden; })
			[
				SNew(SImage).Image(FTechArtToolboxStyle::Get().GetBrush("VrrTools.Tab.Accent"))
			]
		]

		// Button: icon over label
		+ SOverlay::Slot()
		[
			SNew(SButton)
			.ButtonStyle(&FTechArtToolboxStyle::Get().GetWidgetStyle<FButtonStyle>("VrrTools.Tab"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this, Page]() { ActivePage = Page; return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(FTechArtToolboxStyle::Get().GetBrush(Info.Icon))
					.ColorAndOpacity_Lambda(Tint)
				]
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.0f, 5.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(Info.TabLabel)
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
					.ColorAndOpacity_Lambda(Tint)
					.Justification(ETextJustify::Center)
				]
			]
		]
	];
}

// INITIALIZE BUTTON/COMMAND INTERACTIONS
/** Binds the plugin's command (open the Toolbox) to its handler. */
void FTechArtToolboxModule::MapCommandListActions()
{
	// Register our command lists
	FTechArtToolboxCommands::Register();
	
	// Initialize our command list
	EditorToolsCommandList = MakeShareable(new FUICommandList);
	
	// map our custom function behavior - PopUp Window
	EditorToolsCommandList->MapAction
	(
		FTechArtToolboxCommands::Get().CMD_OpenPopUpWindow,
		FExecuteAction::CreateRaw(this, &FTechArtToolboxModule::onClicked_OpenMenu),
		FCanExecuteAction()
	);

}


// BEHAVIORS
/** Opens (or focuses) the Toolbox tab. */
void FTechArtToolboxModule::onClicked_OpenMenu()
{
	FGlobalTabmanager::Get()->TryInvokeTab(TechArtToolboxTabName);
}


#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FTechArtToolboxModule, TechArtToolbox)