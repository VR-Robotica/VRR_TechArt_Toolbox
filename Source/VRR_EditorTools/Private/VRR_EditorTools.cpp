#include "VRR_EditorTools.h"
#include "VRR_EditorToolsStyle.h"
#include "VRR_EditorToolsCommands.h"
#include "Menu_TexturePacker.h"

#include "ToolMenus.h"
#include "LevelEditor.h"

#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

static const FName VRR_EditorToolsTabName("GodTools");

class FVRR_EditorToolsStyle;

#define LOCTEXT_NAMESPACE "FVRR_EditorTools"


// MODULE BEHAVIORS

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
	.SetDisplayName(LOCTEXT("GodTools", "God Tools"))
	.SetMenuType(ETabSpawnerMenuType::Hidden);
}

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
	
	FToolMenuSection& GodToolsSection = MainMenu->FindOrAddSection("GodTools", INVTEXT("God Tools Section"));
	
	GodToolsSection.AddMenuEntryWithCommandList(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList)
		.InsertPosition = FToolMenuInsert(NAME_None, EToolMenuInsertType::Last);
}


// TEST MENU ENTRIES
void FVRR_EditorTools::ExtendWindowMenu()
{
	UToolMenu* Menu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Window");
	
	FToolMenuSection& Section = Menu->FindOrAddSection("WindowLayout");
	Section.AddMenuEntryWithCommandList(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow, EditorToolsCommandList);
}

void FVRR_EditorTools::ExtendPlayToolBar()
{
	UToolMenu* ToolbarMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.LevelEditorToolBar.PlayToolBar");
	
	FToolMenuSection& Section = ToolbarMenu->FindOrAddSection("VRR Plugin");
	
	FToolMenuEntry& Entry = Section.AddEntry(FToolMenuEntry::InitToolBarButton(FVRR_EditorToolsCommands::Get().CMD_OpenPopUpWindow));
	Entry.SetCommandList(EditorToolsCommandList);
}


// POPUP WINDOW
TSharedRef<SDockTab> FVRR_EditorTools::OnSpawnTabWindow(const FSpawnTabArgs& SpawnTabArgs)
{	
	// create our large widget pieces before integrating them into the final window format
	TSharedRef<SWidget> Header = CreateCustomHeader();
	TSharedRef<SWidget> SidebarWidget = CreateLeftToolBar();
	
	// Main Window Formatting (A dockable 'tab' window:
	return SNew(SDockTab)
	.TabRole(ETabRole::NomadTab)
	[
		SNew(SVerticalBox)
		// --- 1. HEADER IMAGE SLOT ---
		+ SVerticalBox::Slot()
		.MaxHeight(128) // Keeps the header at its specified height
		.MinHeight(128)
		[
			Header
		]

		// --- 2. THE MAIN AREA ---
		+ SVerticalBox::Slot()
		.FillHeight(1.0f) // This slot takes up the remaining vertical space
		[
			// --- 3. Sidebar
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SScrollBox) // Good practice in case you add many buttons
				+ SScrollBox::Slot()
				[
					SidebarWidget // The TSharedRef<SWidget> we created earlier
				]
			]
			// --- 4. main window
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(10)
			[
				SNew(SScrollBox) // Good practice in case you add many buttons
				+ SScrollBox::Slot()
				[
					 SNew(STextBlock)
					 .Text(FText::FromString("This is the main content area for dark incantations!"))
					//TexturePacker
				]
			]
		]
	];
}

TSharedRef<SWidget> FVRR_EditorTools::CreateCustomHeader()
{	
	return SNew(SBox)
	.HeightOverride(100.0f) // Fixed height for your header
	[
		SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
        .VAlign(VAlign_Bottom)
        [
            SNew(SSeparator)
            .Thickness(2.0f)
            .SeparatorImage(FAppStyle::GetBrush("Menu.Separator"))
        ]

// ***** The actual Image layer
		+ SHorizontalBox::Slot()
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Brushes.Panel")) // Or your custom Slate Brush
			//.Image(FVRR_EditorToolsStyle::Get()->GetBrush("VRR_EditorToolsStyle.Header"))
			.ColorAndOpacity(FLinearColor(0.05f, 0.05f, 0.05f, 1.0f))
		]
// *****
		
		+ SHorizontalBox::Slot()
		.VAlign(VAlign_Bottom)
		[
			SNew(SSeparator)
			.Thickness(2.0f)
			.SeparatorImage(FAppStyle::GetBrush("Menu.Separator"))
		]
	];
}

TSharedRef<SWidget> FVRR_EditorTools::CreateLeftToolBar()
{
	// 1. Create a Command List to handle the clicks
	TSharedPtr<FUICommandList> MyCommands = MakeShareable(new FUICommandList());

	// 2. Create the Builder
	// Orientation: Orient_Vertical creates the column layout
	FToolBarBuilder ToolbarBuilder(MyCommands, FMultiBoxCustomization::None, TSharedPtr<FExtender>(), Orient_Vertical);

	// 3. Add your buttons
	ToolbarBuilder.BeginSection("MyPluginSection");
	{
		ToolbarBuilder.AddToolBarButton(
			FVRR_EditorToolsCommands::Get().CMD_CustomFunction_01,
			NAME_None,
			FText::FromString("Refresh"),
			FText::FromString("Refresh Window"),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), "Graph.Node.Loop")
		);
	}
	ToolbarBuilder.EndSection();
	
	// 4. Generate the widget
	return ToolbarBuilder.MakeWidget();
}

void FVRR_EditorTools::CreateMenuDropdown()
{
	// create	
}


// INITIALIZE BUTTON/COMMAND INTERACTIONS
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
void FVRR_EditorTools::onClicked_OpenMenu()
{
	UE_LOG(LogTemp, Log, TEXT("Open Menu Button Clicked"));
	FGlobalTabmanager::Get()->TryInvokeTab(VRR_EditorToolsTabName); // invokes (creates) the dock tab window
	//FGlobalTabmanager::Get()->TryInvokeTab(PopupWindow->GetName());
}

void FVRR_EditorTools::onClicked_AnotherCoolFunction()
{
	UE_LOG(LogTemp, Log, TEXT("A Cool Function was clicked"));
}



#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FVRR_EditorTools, VRR_EditorTools)