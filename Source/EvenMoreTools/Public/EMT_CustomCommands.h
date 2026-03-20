#pragma once

#include "Framework/Commands/Commands.h"
#include "Styling/AppStyle.h"

class FEMT_CustomCommands : public TCommands<FEMT_CustomCommands>
{
public:
	FEMT_CustomCommands()
	:TCommands<FEMT_CustomCommands>
	(
		TEXT("MyCustomCommands"), // Unique Name of Command Set
		NSLOCTEXT("CustomCommands", "EvenMoreTools", "Even more tools Plugin"),// Label
		NAME_None, // Name of Parent command set this is extending
		FAppStyle::GetAppStyleSetName() 		// name of the style set from which command icons should be loaded
	){}
	
	// Commands should be declared as members
	TSharedPtr<FUICommandInfo> CMD_MyCustomButton;
	TSharedPtr<FUICommandInfo> CMD_MyCustomEntry;
	
	TSharedPtr<FUICommandInfo> CMD_MyCustomToggle;
	TSharedPtr<FUICommandInfo> CMD_MyCustomCheckBox;
	
	virtual void RegisterCommands() override;
};
