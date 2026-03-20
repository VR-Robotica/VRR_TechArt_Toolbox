#include "EvenMoreTools.h"

#include "EMT_CustomCommands.h"
#include "ToolMenus.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FEvenMoreToolsModule"

void FEvenMoreToolsModule::StartupModule()
{
	// Bind Commands
	// const FLevelEditorModule& LevelEditor = FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
	// const TSharedRef<FUICommandList> Commands = LevelEditor.GetGlobalLevelEditorActions();
	MapCommandListActions();
	
	// Register a function to be called when menu system is initialized
	UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(this, &FEvenMoreToolsModule::RegisterMenus));
}

void FEvenMoreToolsModule::ShutdownModule()
{
	// Unregister the startup function
	UToolMenus::UnRegisterStartupCallback(this);
 
	// Unregister all our menu extensions
	UToolMenus::UnregisterOwner(this);
	
	FEMT_CustomCommands::Unregister();
}

void FEvenMoreToolsModule::RegisterMenus()
{
	// Use the current object as the owner of the menus
	// This allows us to remove all our custom menus when the
	// module is unloaded (see ShutdownModule above)
	FToolMenuOwnerScoped OwnerScoped(this);

	// ToolbarSection.AddEntry(FToolMenuEntry::InitToolBarButton(
	// 		TEXT("MyCustomButtonName"),
	// 		FExecuteAction::CreateLambda([]()
	// 	{
	// 		// Simply log for this example
	// 		UE_LOG(LogTemp, Log, TEXT("MyCustomButton triggered!!"));
	// 	}),
	// 	INVTEXT("My custom button"), // name of button
	// 	INVTEXT("Tooltip for my custom button"), // button hover tooltip
	// 	FSlateIcon(FName("EditorStyle"), "Icons.Comment") // button icon
	// ));
	
	// Extend the "Select" section of the main toolbar
	UToolMenu* SelectionMenu = UToolMenus::Get()->ExtendMenu("LevelEditor.MainMenu.Select");
	
	// add a titled section separator 
	FToolMenuSection& SelectionSection = SelectionMenu->AddSection(
		TEXT("MyCustomSection"), 
		INVTEXT("My Custom Section Name"), 
		FToolMenuInsert("ByTypeSection", EToolMenuInsertType::After)
		);
	
	// add command entry
	SelectionSection.AddEntry(FToolMenuEntry::InitMenuEntry(
		TEXT("MyCustomEntryName"),
		INVTEXT("My Custom Entry"),
		INVTEXT("Selection Entry Tooltip"),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "Icons.Comment"),
		FExecuteAction::CreateLambda([]()
		{
			UE_LOG(LogTemp, Log, TEXT("MyCustomEntry triggered!!"));
		})
		));
	
	// SelectionSection.AddMenuEntry(FEMT_CustomCommands::Get().CMD_MyCustomToggleButton);
	// SelectionSection.AddMenuEntry(FEMT_CustomCommands::Get().CMD_MyCustomCheckBox);
	
	// advanced mode
	SelectionSection.AddMenuEntryWithCommandList
	(
		FEMT_CustomCommands::Get().CMD_MyCustomToggle, 
		EMTCommands, 
FText::FromString("Custom Toggle"),
		FText::FromString("entry for toggling"),
		FSlateIcon()
	);
	
	SelectionSection.AddMenuEntryWithCommandList
	(
		FEMT_CustomCommands::Get().CMD_MyCustomCheckBox, 
		EMTCommands, 
FText::FromString("Custom Check"),
		FText::FromString("box for checking"),
		FSlateIcon()
	);
}

void FEvenMoreToolsModule::MapCommandListActions()
{
	// Register Commands
	FEMT_CustomCommands::Register();
	
	// Initialize our command list
	EMTCommands = MakeShareable(new FUICommandList);
	
	EMTCommands->MapAction(
		FEMT_CustomCommands::Get().CMD_MyCustomButton,
		FExecuteAction::CreateLambda([]()
		{
			// Simply log for this example
			UE_LOG(LogTemp, Log, TEXT("MyCustom BUTTON triggered!!"));
		})
	);
 
	EMTCommands->MapAction(
		FEMT_CustomCommands::Get().CMD_MyCustomEntry,
		FExecuteAction::CreateLambda([]()
		{
			// Simply log for this example
			UE_LOG(LogTemp, Log, TEXT("MyCustom ENTRY triggered!!"));
		})
	);
	
	EMTCommands->MapAction(
		FEMT_CustomCommands::Get().CMD_MyCustomToggle,
		FExecuteAction::CreateLambda([this](){bChecked = !bChecked;}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this](){return bChecked;})
		);
	
	EMTCommands->MapAction(
		FEMT_CustomCommands::Get().CMD_MyCustomCheckBox,
		FExecuteAction::CreateLambda([this]{bChecked = !bChecked;}),
		FCanExecuteAction(),
		FIsActionChecked::CreateLambda([this](){return bChecked;})
		);
}


#undef LOCTEXT_NAMESPACE
    
IMPLEMENT_MODULE(FEvenMoreToolsModule, EvenMoreTools)