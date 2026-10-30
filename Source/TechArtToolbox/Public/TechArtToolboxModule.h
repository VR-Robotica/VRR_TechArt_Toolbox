// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "Modules/ModuleManager.h"

class FTechArtToolboxModule : public IModuleInterface
{
public:
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** Opens (or focuses) the Toolbox tab; bound to CMD_OpenPopUpWindow. */
	void onClicked_OpenMenu();

protected:

	void RegisterMenus();
	void MapCommandListActions();

	// create sub menus
	void CreateTechArtMainMenu();
	void ExtendTechArtMenu();
	void ExtendWindowMenu();
	void ExtendPlayToolBar();

	TSharedRef<class SDockTab> OnSpawnTabWindow(const class FSpawnTabArgs& SpawnTabArgs);
	
	TSharedRef<SWidget> CreateCustomHeader();
	TSharedRef<SWidget> CreateLeftToolBar();

private:
	TSharedPtr<class FUICommandList> EditorToolsCommandList;

	// Pages shown in the Toolbox main area, picked from the left toolbar (value = page switcher index)
	enum class EToolPage : int32 { TexturePacker, AssetValidation, BakeNormals, UVLayout, RebakeTexture, BakeMaps, ControlRig, VATBake, ConsolidateMaterials, EdgeMesh };
	EToolPage ActivePage = EToolPage::AssetValidation;

	/** What the sidebar tab and the header show for a page. */
	struct FPageInfo
	{
		EToolPage Page;
		FText TabLabel;		// short, under the sidebar icon
		FText Title;		// header title
		FText Description;	// header subtitle
		FName Icon;			// brush in FTechArtToolboxStyle
	};

	/** Every page, in sidebar order. */
	static const TArray<FPageInfo>& GetPages();
	static const FPageInfo& GetPageInfo(EToolPage Page);

	/** One sidebar tab: icon over label, highlighted with an accent bar while its page is shown. */
	TSharedRef<SWidget> CreatePageTab(const FPageInfo& Info);
};
