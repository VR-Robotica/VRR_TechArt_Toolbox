// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "Styling/SlateStyle.h"

/**  */
class FTechArtToolboxStyle
{
public:

	static void Initialize();

	static void Shutdown();

	static void ReloadTextures();
	
	static const ISlateStyle& Get();

	static FName GetStyleSetName();

private:

	static TSharedRef< class FSlateStyleSet > Create();

private:

	static TSharedPtr< class FSlateStyleSet > StyleSetInstance;
};