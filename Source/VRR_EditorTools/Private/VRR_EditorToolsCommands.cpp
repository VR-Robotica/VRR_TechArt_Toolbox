// Fill out your copyright notice in the Description page of Project Settings.

/*
 * The plugin's UI commands (FVRR_EditorToolsCommands).
 *
 *  - CMD_OpenPopUpWindow is the one in use: the "Toolbox" toolbar/menu entry that opens the Toolbox tab
 *    (mapped in FVRR_EditorTools::MapCommandListActions).
 *  - CMD_CustomFunction_01-04 are template placeholders with no real behavior (01 is mapped to a test function,
 *    02-04 only by FTAT_MenuBehaviors). Kept for now; remove them together with TAT_MenuBehaviors when unused.
 */
#include "VRR_EditorToolsCommands.h"


#define LOCTEXT_NAMESPACE "FVRR_EditorTools"

void FVRR_EditorToolsCommands::RegisterCommands()
{
	UI_COMMAND
	(
		CMD_OpenPopUpWindow,				// Unique Name of Commands
		"Toolbox",						// Label
		"Open the TechArt Toolbox",			// Tooltip
		EUserInterfaceActionType::Button,	// UI Representation
		FInputChord()						// Keyboard Shortcut
	);
		
	UI_COMMAND
	(
		CMD_CustomFunction_01, 
		"My custom function 01", 
		"Tooltip for my custom entry", 
		EUserInterfaceActionType::Button, 
		FInputChord()
	);
	
	UI_COMMAND
	(
		CMD_CustomFunction_02, 
		"My custom function 02", 
		"Tooltip for my custom entry", 
		EUserInterfaceActionType::Button, 
		FInputChord()
	);
	
	UI_COMMAND
	(
		CMD_CustomFunction_03, 
		"My custom function 03",  
		"Tooltip for my custom entry", 
		EUserInterfaceActionType::Button, 
		FInputChord()
	);
	
	UI_COMMAND
	(
		CMD_CustomFunction_04, 
		"My custom function 04",  
		"Tooltip for my custom entry", 
		EUserInterfaceActionType::Button, 
		FInputChord()
	);
}




#undef LOCTEXT_NAMESPACE
