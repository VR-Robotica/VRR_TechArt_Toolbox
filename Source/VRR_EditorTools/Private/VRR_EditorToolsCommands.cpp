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
