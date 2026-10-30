// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "Framework/Commands/Commands.h"
#include "TechArtToolboxStyle.h"

/** TechArt Toolbox's UI commands. */
class FTechArtToolboxCommands : public TCommands<FTechArtToolboxCommands>
{
public:
	FTechArtToolboxCommands()
		: TCommands<FTechArtToolboxCommands>(
			TEXT("TechArtToolbox"),													// command set name (also the icon's style key prefix)
			NSLOCTEXT("TechArtToolbox", "CommandsLabel", "TechArt Toolbox"),
			NAME_None,
			FTechArtToolboxStyle::GetStyleSetName())
	{}

	/** Opens (or focuses) the TechArt Toolbox tab. */
	TSharedPtr<FUICommandInfo> CMD_OpenPopUpWindow;

	virtual void RegisterCommands() override;
};
