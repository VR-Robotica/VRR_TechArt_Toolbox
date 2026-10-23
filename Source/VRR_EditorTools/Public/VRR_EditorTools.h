#pragma once

#include "Modules/ModuleManager.h"
#include "TAT_Menu.h"

class FToolBarBuilder;
class FMenuBuilder;

UENUM()
enum class ETonemapperType : uint8
{
	Value1 UMETA(DisplayName = "ACES (Default)"),
	Value2 UMETA(DisplayName = "AgX"),
	Value3 UMETA(DisplayName = "Reinhard"),
	Value4 UMETA(DisplayName = "Durrand")
};

enum class EAgXStyles : uint8
{
	Value1 UMETA(DisplayName = "Default"),
	Value2 UMETA(DisplayName = "Golden"),
	Value3 UMETA(DisplayName = "Vibrant")
};

class FVRR_EditorTools : public IModuleInterface
{
public:
	FTechArtTools_Menu* PopupWindow;
	
	//UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tonemapping")
	ETonemapperType TonemapperType = ETonemapperType::Value1;
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
	
	/** This function will be bound to Command (by default it will bring up plugin window) */
	void onClicked_OpenMenu(); // this function will 'invoke' the window
	void onClicked_AnotherCoolFunction();

protected:

	void RegisterMenus();
	void MapCommandListActions();

	// create sub menus
	void CreateTechArtMainMenu();
	void ExtendTechArtMenu();
	void ExtendWindowMenu();
	void ExtendPlayToolBar();
	
	void CreateMenuDropdown();

	TSharedRef<class SDockTab> OnSpawnTabWindow(const class FSpawnTabArgs& SpawnTabArgs);
	
	TSharedRef<SWidget> CreateCustomHeader();
	TSharedRef<SWidget> CreateLeftToolBar();

private:
	TSharedPtr<class FUICommandList> EditorToolsCommandList;

	// Pages shown in the Toolbox main area, picked from the left toolbar (value = page switcher index)
	enum class EToolPage : int32 { TexturePacker, AssetValidation, BakeNormals, UVLayout, RebakeTexture, BakeMaps, ControlRig };
	EToolPage ActivePage = EToolPage::AssetValidation;

	/** What the sidebar tab and the header show for a page. */
	struct FPageInfo
	{
		EToolPage Page;
		FText TabLabel;		// short, under the sidebar icon
		FText Title;		// header title
		FText Description;	// header subtitle
		FName Icon;			// brush in FVRR_EditorToolsStyle
	};

	/** Every page, in sidebar order. */
	static const TArray<FPageInfo>& GetPages();
	static const FPageInfo& GetPageInfo(EToolPage Page);

	/** One sidebar tab: icon over label, highlighted with an accent bar while its page is shown. */
	TSharedRef<SWidget> CreatePageTab(const FPageInfo& Info);
};
