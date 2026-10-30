// Copyright VR Robotica. All Rights Reserved.

#include "TechArtToolboxCommands.h"

#define LOCTEXT_NAMESPACE "TechArtToolbox"

void FTechArtToolboxCommands::RegisterCommands()
{
	UI_COMMAND(CMD_OpenPopUpWindow, "TechArt Toolbox", "Open the TechArt Toolbox", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
