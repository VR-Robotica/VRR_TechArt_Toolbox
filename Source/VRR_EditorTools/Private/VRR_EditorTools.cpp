/*
 * VRR_EditorTools module: registers the Toolbox window and the menu/toolbar entries that open it.
 *
 * The window is a dockable tab: a header image on top, a page toolbar on the left, and a widget switcher
 * holding one page per tool (Validate, Texture Packer, Bake Normals, Mesh Maps, Control Rig).
 * Each page lives in its own Menu_<Page> widget (UI) backed by Core_<Page> (the actual work), so this file
 * only wires things together.
 */

#include "VRR_EditorTools.h"
#include "VRR_EditorToolsStyle.h"
#include "VRR_EditorToolsCommands.h"
#include "Menu_TexturePacker.h"
#include "Menu_TextureValidator.h"
#include "Menu_BakeNormals.h"
#include "Menu_BakeMaps.h"
#include "Menu_ControlRig.h"

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

static const FName VRR_EditorToolsTabName("GodTools");

class FVRR_EditorToolsStyle;

#define LOCTEXT_NAMESPACE "FVRR_EditorTools"


// MODULE BEHAVIORS

/** Loads the style, maps the commands, registers the Toolbox tab spawner and the menus. */
void FVRR_EditorTools::StartupModule()
{
	// This code will execute after your module is loaded into memory; 
	// the exact timing is specified in the .uplugin file per-module
	
	// This plugin startup will setup up all the in-editor menu locations
	// actual menu behavior will be local to the menus themselves
	
	// Setup our UI Styles
	FVRR_EditorToolsStyle::Initialize();
	FVRR_EditorToolsStyle::ReloadTextures();
	
	MapCommandListActions();
	
	// Register a function to be called when menu system is initialized
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FVRR_EditorTools::RegisterMenus));
	
	FGlobalTabmanager::Get()->RegisterNomadTabSpawner(VRR_EditorToolsTabName, FOnSpawnTab::CreateRaw(this, &FVRR_EditorTools::OnSpawnTabWindow))
	.SetDisplayName(LOCTEXT("Toolbox", "Toolbox"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);
}

/** Undoes StartupModule: menus, style, commands and the tab spawner. */
void FVRR_EditorTools::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.

	// Unregister the startup function
	UToolMenus::UnRegisterStartupCallback(this);
	
	// Unregister all our menu extensions
	UToolMenus::UnregisterOwner(this);

	// Unregister style refs
	FVRR_EditorToolsStyle::Shutdown();

	// Unregister command lists
	FVRR_EditorToolsCommands::Unregister();

	// Unregister nomad tab window
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(VRR_EditorToolsTabName);
}

/** Adds every menu and toolbar entry (called once the ToolMenus system is ready). */
void FVRR_EditorTools::RegisterMenus()
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
void FVRR_EditorTools::CreateTechArtMainMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu");
	
	FToolMenuSection& MenuSection = Menu->FindOrAddSection(NAME_None);
	
	FToolMenuEntry& VRRToolEntry =
		MenuSection.AddSubMenu(
			"VRRTools",
			LOCTEXT("Label", "TechArt"),
			LOCTEXT("ToolTip", "Oracle of Occult Knowledge"),
			FNewToolMenuChoice()
		);

	VRRToolEntry.InsertPosition = FToolMenuInsert(NAME_None, EToolMenuInsertType::Last);
}

/** Fills the TechArt menu, including the entry that opens Toolbox. */
void FVRR_EditorTools::ExtendTechArtMenu()
{
	UToolMenu* MainMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.VRRTools");
	
	FToolMenuSection& LayoutSection = MainMenu->FindOrAddSection("TechArt", INVTEXT("Tech Art Section"));
	LayoutSection.AddEntry
	(
		FToolMenuEntry::InitMenuEntry
		(
			"TecgArtTools",
			INVTEXT("TechArt Tools"),
			INVTEXT("Tools Entry Tooltip"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
			FExecuteAction::CreateLambda([]()
				{
					UE_LOG(LogTemp, Log, TEXT("MyCustomEntry triggered!!") );
				}
			)
		)
	);
	
	FToolMenuSection& GodToolsSection = MainMenu->FindOrAddSection("GodTools", INVTEXT("Toolbox Section"));
	
	GodToolsSection.AddMenuEntryWithCommandList(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList)
		.InsertPosition = FToolMenuInsert(NAME_None, EToolMenuInsertType::Last);
}


// TEST MENU ENTRIES
/** Window menu entry that opens Toolbox. */
void FVRR_EditorTools::ExtendWindowMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
	Section.AddMenuEntryWithCommandList(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList);
}

/** Toolbox button next to the Play button. */
void FVRR_EditorTools::ExtendPlayToolBar()
{
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("VRR Plugin");
	
	FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow));
	Entry.SetCommandList(EditorToolsCommandList);
}


// POPUP WINDOW
/** Every page with its sidebar label, header title/description and icon, in sidebar order. */
const TArray<FVRR_EditorTools::FPageInfo>& FVRR_EditorTools::GetPages()
{
	static const TArray<FPageInfo> Pages =
	{
		{ EToolPage::TextureValidator, LOCTEXT("ValidateTab", "Validate"), LOCTEXT("ValidateTitle", "Texture Validator"),
			LOCTEXT("ValidateDesc", "Check texture assets against the project's naming, compression and size rules."), "VrrTools.Page.Validate" },
		{ EToolPage::TexturePacker, LOCTEXT("PackerTab", "Packer"), LOCTEXT("PackerTitle", "Texture Packer"),
			LOCTEXT("PackerDesc", "Combine channels from several textures into one packed mask."), "VrrTools.Page.Packer" },
		{ EToolPage::BakeNormals, LOCTEXT("NormalsTab", "Normals"), LOCTEXT("NormalsTitle", "Bake Smooth Normals"),
			LOCTEXT("NormalsDesc", "Bake fully smooth normals into vertex colors or a texture, for inverted-hull toon outlines."), "VrrTools.Page.Normals" },
		{ EToolPage::BakeMaps, LOCTEXT("MapsTab", "Mesh Maps"), LOCTEXT("MapsTitle", "Bake Mesh Maps"),
			LOCTEXT("MapsDesc", "Bake Cavity, Edge and Curvature masks from a mesh's own geometry."), "VrrTools.Page.MeshMaps" },
		{ EToolPage::ControlRig, LOCTEXT("RigTab", "Rig"), LOCTEXT("RigTitle", "Control Rig Generator"),
			LOCTEXT("RigDesc", "Generate a Modular Control Rig for a character's skeletal mesh."), "VrrTools.Page.Rig" },
	};
	return Pages;
}

const FVRR_EditorTools::FPageInfo& FVRR_EditorTools::GetPageInfo(EToolPage Page)
{
	const FPageInfo* Info = GetPages().FindByPredicate([Page](const FPageInfo& P) { return P.Page == Page; });
	return Info ? *Info : GetPages()[0];
}

/** Builds the Toolbox tab: header bar on top, sidebar on the left, and the switcher with one Menu_<Page> widget per tool. */
TSharedRef<SDockTab> FVRR_EditorTools::OnSpawnTabWindow(const FSpawnTabArgs& SpawnTabArgs)
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
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_TextureValidator)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_BakeNormals)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_BakeMaps)) ]
				+ SWidgetSwitcher::Slot()[ PageSlot(SNew(SMenu_ControlRig)) ]
			]
		]
	];
}

/** Header bar: the TechArt Toolbox logo, a divider, and the active page's title and description. */
TSharedRef<SWidget> FVRR_EditorTools::CreateCustomHeader()
{
	return SNew(SBorder)
	.BorderImage(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Frame"))
	.Padding(FMargin(12.0f, 6.0f))
	[
		SNew(SHorizontalBox)

		// Logo
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SImage)
			.Image(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Logo"))
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
TSharedRef<SWidget> FVRR_EditorTools::CreateLeftToolBar()
{
	TSharedRef<SVerticalBox> Tabs = SNew(SVerticalBox);
	for (const FPageInfo& Info : GetPages())
	{
		Tabs->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 4.0f)[ CreatePageTab(Info) ];
	}

	return SNew(SBorder)
	.BorderImage(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Frame"))
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
TSharedRef<SWidget> FVRR_EditorTools::CreatePageTab(const FPageInfo& Info)
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
			.Image(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Tab.Active"))
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
				SNew(SImage).Image(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Tab.Accent"))
			]
		]

		// Button: icon over label
		+ SOverlay::Slot()
		[
			SNew(SButton)
			.ButtonStyle(&FVRR_EditorToolsStyle::Get().GetWidgetStyle<FButtonStyle>("VrrTools.Tab"))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.OnClicked_Lambda([this, Page]() { ActivePage = Page; return FReply::Handled(); })
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
				[
					SNew(SImage)
					.Image(FVRR_EditorToolsStyle::Get().GetBrush(Info.Icon))
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

void FVRR_EditorTools::CreateMenuDropdown()
{
	// create	
}


// INITIALIZE BUTTON/COMMAND INTERACTIONS
/** Binds the plugin commands (open window, test function) to their handlers. */
void FVRR_EditorTools::MapCommandListActions()
{
	// Register our command lists
	FVRR_EditorToolsCommands::Register();
	
	// Initialize our command list
	EditorToolsCommandList = MakeShareable(new FUICommandList);
	
	// map our custom function behavior - PopUp Window
	EditorToolsCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow,
		FExecuteAction::CreateRaw(this, &FVRR_EditorTools::onClicked_OpenMenu),
		FCanExecuteAction()
	);
	
	EditorToolsCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_CustomFunction_01,
		FExecuteAction::CreateRaw(this, &FVRR_EditorTools::onClicked_AnotherCoolFunction),
		FCanExecuteAction()
	);

}



// BEHAVIORS
/** Opens (or focuses) the Toolbox tab. */
void FVRR_EditorTools::onClicked_OpenMenu()
{
	UE_LOG(LogTemp, Log, TEXT("Open Menu Button Clicked"));
	FGlobalTabmanager::Get()->TryInvokeTab(VRR_EditorToolsTabName); // invokes (creates) the dock tab window
	//FGlobalTabmanager::Get()->TryInvokeTab(PopupWindow->GetName());
}

/** Placeholder command: logs a message. */
void FVRR_EditorTools::onClicked_AnotherCoolFunction()
{
	UE_LOG(LogTemp, Log, TEXT("A Cool Function was clicked"));
}



#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVRR_EditorTools, VRR_EditorTools)