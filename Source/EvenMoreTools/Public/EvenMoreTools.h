#pragma once

#include "Modules/ModuleManager.h"

class FEvenMoreToolsModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
    
protected:
    void RegisterMenus();
    void MapCommandListActions();

    bool bChecked = true;
    
    //const auto MyCustomCommands = MakeShared<FUICommandList>();
    
    TSharedPtr<class FUICommandList> EMTCommands;
};
