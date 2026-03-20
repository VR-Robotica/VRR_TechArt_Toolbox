#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"
#include "VRR_EditorToolsStyle.h"

#define LOCTEXT_NAMESPACE "FVRR_EditorTools"

class FVRR_EditorToolsCommands : public TCommands<FVRR_EditorToolsCommands>
{
public:
	// CONSTRUCTOR
	FVRR_EditorToolsCommands() 
	: TCommands<FVRR_EditorToolsCommands>
	(
		TEXT("VRR_EditorTools"), // Unique Name of Command Set
		NSLOCTEXT("FVRR_EditorTools", "EditorTools", "Editor Tools Plugin"),	// Label
		NAME_None, // Name of Parent command set this is extending
		FVRR_EditorToolsStyle::GetStyleSetName()// name of the style set from which command icons should be loaded
	)
	{}
	
	
	// Commands should be declared as members
	TSharedPtr<FUICommandInfo> CMD_OpenPopUpWindow;
	
	TSharedPtr<FUICommandInfo> CMD_CustomFunction_01;
	TSharedPtr<FUICommandInfo> CMD_CustomFunction_02;
	TSharedPtr<FUICommandInfo> CMD_CustomFunction_03;
	TSharedPtr<FUICommandInfo> CMD_CustomFunction_04;
	
	// TCommands<> interface
	virtual void RegisterCommands() override;
	
};

#undef LOCTEXT_NAMESPACE
