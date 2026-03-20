#include "VRR_EditorToolsStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"

#define RootToContentDir Style->RootToContentDir

TSharedPtr<FSlateStyleSet> FVRR_EditorToolsStyle::StyleSetInstance = nullptr;

void FVRR_EditorToolsStyle::Initialize()
{
	if (!StyleSetInstance.IsValid())
	{
		StyleSetInstance = Create();
		FSlateStyleRegistry::RegisterSlateStyle(*StyleSetInstance);
	}
}

void FVRR_EditorToolsStyle::Shutdown()
{
	FSlateStyleRegistry::UnRegisterSlateStyle(*StyleSetInstance);
	ensure(StyleSetInstance.IsUnique());
	StyleSetInstance.Reset();
}

FName FVRR_EditorToolsStyle::GetStyleSetName()
{
	static FName StyleSetName(TEXT("VRR_EditorToolsStyle"));
	return StyleSetName;
}

const FVector2D Icon16x16(16.0f, 16.0f);
const FVector2D Icon20x20(20.0f, 20.0f);
//const FVector2D Header512(512.0f, 256.0f);

TSharedRef< FSlateStyleSet > FVRR_EditorToolsStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("VRR_EditorToolsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("VRR_EditorTools")->GetBaseDir() / TEXT("Resources"));

	Style->Set("OpenPluginWindow", new IMAGE_BRUSH(TEXT("Icon128.png"), Icon20x20));
	//Style->Set("VRR_EditorTools.Header", new FSlateImageBrush((TEXT("LOGO_CDPR"), TEXT(".png")), FVector2D(512.0f, 128.0f)));

	return Style;
}

void FVRR_EditorToolsStyle::ReloadTextures()
{
	if (FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().GetRenderer()->ReloadTextureResources();
	}
}

const ISlateStyle& FVRR_EditorToolsStyle::Get()
{
	return *StyleSetInstance;
}
