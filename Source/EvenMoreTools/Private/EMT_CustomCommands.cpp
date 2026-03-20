// Fill out your copyright notice in the Description page of Project Settings.

#include "EMT_CustomCommands.h"

#define LOCTEXT_NAMESPACE "EMT_CustomCommands"

void FEMT_CustomCommands::RegisterCommands()
{
	UI_COMMAND(
		CMD_MyCustomButton,					// Unique Name of Command 
		"My Button Label",					// Label
		"My Button Tooltip",				// Tooltip
		EUserInterfaceActionType::Button,	// UI representation (when used to dynamically build toolbars and menus)
		FInputChord(EKeys::C, EModifierKey::Shift | EModifierKey::Alt) // Keyboard Shortcut
	);
 
	UI_COMMAND(
		CMD_MyCustomEntry, 
		"My custom entry", 
		"Tooltip for my custom entry", 
		EUserInterfaceActionType::Button, 
		FInputChord()
	);
	
	UI_COMMAND(
		CMD_MyCustomToggle, 
		"Custom Toggle", 
		"Tooltip on how to toggle", 
		EUserInterfaceActionType::ToggleButton, 
		FInputChord()
	);
	
	UI_COMMAND(
		CMD_MyCustomCheckBox, 
		"Custom Check Box", 
		"Tooltip on how to check",
		EUserInterfaceActionType::Check, 
		FInputChord()
	);
}

#undef LOCTEXT_NAMESPACE
