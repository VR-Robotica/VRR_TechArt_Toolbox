#include "TAT_MenuBehaviors.h"
#include "VRR_EditorToolsCommands.h"


void FTAT_MenuBehaviors::MapCommandListActions()
{
	// Register our command lists
	FVRR_EditorToolsCommands::Register();
	
	// Initialize our command list
	MenuCommandList = MakeShareable(new FUICommandList);
	
	// map our custom function behavior - PopUp Window
	MenuCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_CustomFunction_01,
		FExecuteAction::CreateRaw(this, &FTAT_MenuBehaviors::onClicked_button01),
		FCanExecuteAction()
	);
	
	MenuCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_CustomFunction_02,
		FExecuteAction::CreateRaw(this, &FTAT_MenuBehaviors::onClicked_button02),
		FCanExecuteAction()
	);
	
	MenuCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_CustomFunction_03,
		FExecuteAction::CreateRaw(this, &FTAT_MenuBehaviors::onClicked_button03),
		FCanExecuteAction()
	);
	
	MenuCommandList->MapAction
	(
		FVRR_EditorToolsCommands::Get().CMD_CustomFunction_04,
		FExecuteAction::CreateRaw(this, &FTAT_MenuBehaviors::onClicked_button04),
		FCanExecuteAction()
	);
}


void FTAT_MenuBehaviors::onClicked_button01()
{
	UE_LOG(LogTemp, Log, TEXT("Button 01 Clicked"));
}

void FTAT_MenuBehaviors::onClicked_button02()
{
	UE_LOG(LogTemp, Log, TEXT("Button 02 Clicked"));
}

void FTAT_MenuBehaviors::onClicked_button03()
{
	UE_LOG(LogTemp, Log, TEXT("Button 03 Clicked"));
}

void FTAT_MenuBehaviors::onClicked_button04()
{
	UE_LOG(LogTemp, Log, TEXT("Button 04 Clicked"));
}
