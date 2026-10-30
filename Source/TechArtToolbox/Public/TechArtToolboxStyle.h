// Copyright VR Robotica. All Rights Reserved.

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