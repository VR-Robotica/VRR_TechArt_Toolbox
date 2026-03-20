using UnrealBuildTool;

public class EvenMoreTools : ModuleRules
{
    public EvenMoreTools(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
        //PrivateIncludePaths.Add("EvenMoreToolsModule");
        
        PublicDependencyModuleNames.AddRange(
            new string[]
            {
                "Core",
            }
        );

        PrivateDependencyModuleNames.AddRange(
            new string[]
            {
                "CoreUObject",
                "Engine",
                "InputCore",
                "LevelEditor",
                "Slate",
                "SlateCore",
                "ToolMenus"
            }
        );
    }
}