#pragma once

#include "Styling/SlateStyle.h"

/**  */
class FVRR_EditorToolsStyle
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