// Fill out your copyright notice in the Description page of Project Settings.

/*
 * The plugin's Slate style set ("VRR_EditorToolsStyle"): every brush and button style the Toolbox window uses.
 *
 *  - Initialize/Shutdown register and unregister the set once, from the module's startup and shutdown.
 *  - Create builds it: the toolbar icon, the logo, the frame/card/badge backgrounds, the sidebar tab button and
 *    its active/accent brushes, and one icon per page ("VrrTools.Page.<Page>", taken from the editor's own
 *    Starship SVGs so they match the rest of the editor).
 *  - A new Toolbox page adds its icon here and names it in its page info (VRR_EditorTools.cpp).
 */
#include "VRR_EditorToolsStyle.h"
#include "Styling/SlateStyleRegistry.h"
#include "Framework/Application/SlateApplication.h"
#include "Slate/SlateGameResources.h"
#include "Interfaces/IPluginManager.h"
#include "Styling/SlateStyleMacros.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Misc/Paths.h"
#include "Styling/StyleColors.h"

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

TSharedRef< FSlateStyleSet > FVRR_EditorToolsStyle::Create()
{
	TSharedRef< FSlateStyleSet > Style = MakeShareable(new FSlateStyleSet("VRR_EditorToolsStyle"));
	Style->SetContentRoot(IPluginManager::Get().FindPlugin("VRR_EditorTools")->GetBaseDir() / TEXT("Resources"));

	Style->Set("OpenPluginWindow", new IMAGE_BRUSH(TEXT("Icon128.png"), Icon20x20));

	// GOD TOOLS WINDOW
	// Header logo: LOGO.png is 1024 x 256, shown at a quarter size
	Style->Set("VrrTools.Logo", new IMAGE_BRUSH(TEXT("LOGO"), FVector2D(256.0f, 64.0f)));

	// Frame (header bar and sidebar) sits one shade above the window background
	Style->Set("VrrTools.Frame", new FSlateColorBrush(FStyleColors::Recessed));

	// Cards group each section of a page
	Style->Set("VrrTools.Card", new FSlateRoundedBoxBrush(FStyleColors::Panel, 6.0f, FStyleColors::Header, 1.0f));

	// Pill behind badges (counts, states); white so each badge tints it
	Style->Set("VrrTools.Badge", new FSlateRoundedBoxBrush(FLinearColor::White, 9.0f));

	// Sidebar tabs: flat until hovered; the active tab gets a filled background and an accent bar
	Style->Set("VrrTools.Tab", FButtonStyle()
		.SetNormal(FSlateNoResource())
		.SetHovered(FSlateRoundedBoxBrush(FStyleColors::Panel, 4.0f))
		.SetPressed(FSlateRoundedBoxBrush(FStyleColors::Header, 4.0f))
		.SetNormalPadding(FMargin(0.0f))
		.SetPressedPadding(FMargin(0.0f)));
	Style->Set("VrrTools.Tab.Active", new FSlateRoundedBoxBrush(FStyleColors::Header, 4.0f));
	Style->Set("VrrTools.Tab.Accent", new FSlateRoundedBoxBrush(FStyleColors::Primary, 1.5f));

	// Page icons: the editor's own monochrome icons, so they can be tinted for the active/inactive state
	const FString StarshipDir = FPaths::EngineContentDir() / TEXT("Editor/Slate/Starship");
	auto PageIcon = [&StarshipDir](const TCHAR* Path) { return new FSlateVectorImageBrush(StarshipDir / Path, FVector2D(20.0f, 20.0f)); };
	Style->Set("VrrTools.Page.Validate", PageIcon(TEXT("Common/validate.svg")));
	Style->Set("VrrTools.Page.Packer", PageIcon(TEXT("AssetIcons/Texture2DArray_16.svg")));
	Style->Set("VrrTools.Page.Normals", PageIcon(TEXT("AssetIcons/StaticMesh_16.svg")));
	Style->Set("VrrTools.Page.UVLayout", PageIcon(TEXT("Common/SetDrawUVs.svg")));
	Style->Set("VrrTools.Page.Rebake", PageIcon(TEXT("AssetIcons/Texture2D_16.svg")));
	Style->Set("VrrTools.Page.Consolidate", PageIcon(TEXT("Common/Layers.svg")));
	Style->Set("VrrTools.Page.MeshMaps", PageIcon(TEXT("AssetIcons/Material_16.svg")));
	Style->Set("VrrTools.Page.Rig", PageIcon(TEXT("AssetIcons/Skeleton_16.svg")));
	Style->Set("VrrTools.Page.VAT", PageIcon(TEXT("AssetIcons/AnimSequence_16.svg")));

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
