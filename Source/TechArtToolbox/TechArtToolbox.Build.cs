// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

using UnrealBuildTool;

/** TechArt Toolbox: editor-only tools window (validation, texture packing, baking, UV layouts, Control Rig, VAT). Never ships with the game. */
public class TechArtToolbox : ModuleRules
{
	public TechArtToolbox(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		// The Rig page uses Control Rig APIs added in UE 5.7 (ControlRigBlueprintLegacy.h, GetModularRigModel), so 5.6 builds without it
		bool bWithRig = Target.Version.MajorVersion > 5 || (Target.Version.MajorVersion == 5 && Target.Version.MinorVersion >= 7);
		PublicDefinitions.Add("TECHARTTOOLBOX_WITH_RIG=" + (bWithRig ? "1" : "0"));

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
			"Settings",					// opening the naming page in Project Settings
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
