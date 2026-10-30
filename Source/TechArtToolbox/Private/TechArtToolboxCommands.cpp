// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#include "TechArtToolboxCommands.h"

#define LOCTEXT_NAMESPACE "TechArtToolbox"

void FTechArtToolboxCommands::RegisterCommands()
{
	UI_COMMAND(CMD_OpenPopUpWindow, "TechArt Toolbox", "Open the TechArt Toolbox", EUserInterfaceActionType::Button, FInputChord());
}

#undef LOCTEXT_NAMESPACE
