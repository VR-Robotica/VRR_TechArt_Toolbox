// Copyright Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;

public class VRR_EditorTools : ModuleRules
{
	public VRR_EditorTools(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		
		PublicIncludePaths.AddRange(
			new string[] {
				// ... add public include paths required here ...
			}
			);
				
		
		PrivateIncludePaths.AddRange(
			new string[] {
				// ... add other private include paths required here ...
			}
			);
			
		
		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				// ... add other public dependencies that you statically link with here ...
				"RHI"
			}
			);
			
		if ((Target.IsInPlatformGroup(UnrealPlatformGroup.Windows)))
		{
			// Uses DXGI to query GPU hardware
			// This is what will allow us to get GPU usage statistics at runtime
			PublicSystemLibraries.Add("DXGI.lib");
		}
		
		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"AssetRegistry",
				"ContentBrowser",
				"CoreUObject",
				"EditorFramework",
				"Engine",
				"ImageCore",
				"InputCore",
				"Projects",
				"PropertyEditor",
				"RenderCore",
				"Slate",
				"SlateCore",
				"ToolMenus",
				"UnrealEd",
				// ... add private dependencies that you statically link with here ...	
			}
		);
		
		
		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				// ... add any modules that your module loads dynamically here ...
			}
			);
	}
}
