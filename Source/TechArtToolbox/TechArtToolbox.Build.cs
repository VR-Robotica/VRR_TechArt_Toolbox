// Copyright VR Robotica. All Rights Reserved.

using UnrealBuildTool;

/** TechArt Toolbox: editor-only tools window (validation, texture packing, baking, UV layouts, Control Rig, VAT). Never ships with the game. */
public class TechArtToolbox : ModuleRules
{
	public TechArtToolbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"AnimToTexture",			// VAT page
			"AnimToTextureEditor",
			"AssetRegistry",
			"AssetTools",
			"ContentBrowser",
			"ContentBrowserData",
			"ControlRig",				// Rig page
			"ControlRigDeveloper",
			"ControlRigEditor",
			"CoreUObject",
			"DesktopPlatform",
			"DeveloperSettings",		// Project Settings page
			"EditorFramework",
			"EditorWidgets",
			"Engine",
			"ImageCore",
			"InputCore",
			"Json",
			"MaterialBaking",
			"MaterialEditor",
			"MeshDescription",
			"PhysicsCore",
			"Projects",
			"PropertyEditor",
			"RenderCore",
			"RigVM",
			"RigVMDeveloper",
			"SkeletalMeshDescription",
			"Slate",
			"SlateCore",
			"StaticMeshDescription",
			"ToolMenus",
			"ToolWidgets",
			"UnrealEd"
		});
	}
}
