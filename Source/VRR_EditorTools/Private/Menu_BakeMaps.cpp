// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Mesh Maps page (UI only).
 *
 * The page edits an FMeshMapBakeSettings and hands it to MeshMapBaker::Bake (Core_BakeMaps.cpp), which analyzes
 * the mesh and writes the textures. This file builds the widgets and shows the result.
 */

#include "Menu_BakeMaps.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "SMenu_BakeMaps"

/** Builds the page: mesh, one card per map, output and bake. */
void SMenu_BakeMaps::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		VrrMenu::Stack({
			CreateMeshSection(),
			CreateMapsSection(),
			CreateOutputSection(),
			CreateBakeSection(),
		})
	];
}

// LAYOUT
/** Mesh picker, "Use Selected" button, the mesh info line and the weld distance. */
TSharedRef<SWidget> SMenu_BakeMaps::CreateMeshSection()
{
	return VrrMenu::Card(LOCTEXT("MeshHeader", "Source Mesh"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("MeshLabel", "Mesh"),
			VrrMenu::MeshPicker(VrrMenu::EMeshTypes::StaticOrSkeletal,
				[this]() { return Mesh.Get(); },
				[this](UObject* NewMesh) { SetMesh(NewMesh); },
				[this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_BakeMaps::GetMeshInfoText)),
		VrrMenu::Row(LOCTEXT("WeldDistance", "Weld distance (cm)"),
			VrrMenu::FloatField(&Settings.WeldDistance, 0.0f, 10.0f, 0.001f),
			LOCTEXT("WeldDistanceTip", "Vertices closer than this count as the same point, so hard edges and UV seams don't split the surface")),
	}));
}

/** One card per map (Cavity, Edge, Curvature, Ambient Occlusion): the header checkbox turns it on, the rows tune it. */
TSharedRef<SWidget> SMenu_BakeMaps::CreateMapsSection()
{
	auto MapCard = [](const FText& Title, bool* bEnabled, const FText& ToolTip, const TArray<TSharedRef<SWidget>>& Rows) -> TSharedRef<SWidget>
	{
		TSharedRef<SWidget> Content = VrrMenu::Rows(Rows);
		Content->SetEnabled(TAttribute<bool>::CreateLambda([bEnabled]() { return *bEnabled; }));
		return VrrMenu::Card(Title, Content, VrrMenu::CheckBox(bEnabled, LOCTEXT("BakeThisMap", "Bake"), ToolTip));
	};

	return VrrMenu::Stack({
		MapCard(LOCTEXT("Cavity", "Cavity"), &Settings.bBakeCavity,
			LOCTEXT("CavityTip", "White on flat or raised surfaces, dark in pockets and crevices that sit below their surroundings"), {
			VrrMenu::HelpText(LOCTEXT("CavityInfo", "White on flat or raised surfaces, dark in pockets and crevices.")),
			VrrMenu::Row(LOCTEXT("CavitySpread", "Spread"),
				VrrMenu::IntField(&Settings.CavitySpread, 1, 200),
				LOCTEXT("CavitySpreadTip", "How wide an area each point is compared against (smoothing iterations). Higher finds broader pockets; lower only tight creases")),
			VrrMenu::Row(LOCTEXT("CavityDepth", "Depth for black (cm)"),
				VrrMenu::FloatField(&Settings.CavityDepth, 0.001f, 50.0f, 0.01f),
				LOCTEXT("CavityDepthTip", "How far below the surroundings a point must sit to be fully black. Lower makes cavities darker")),
		}),

		MapCard(LOCTEXT("Edge", "Edge"), &Settings.bBakeEdge,
			LOCTEXT("EdgeTip", "White lines along sharp creases between faces (wear masks, highlights), black elsewhere"), {
			VrrMenu::HelpText(LOCTEXT("EdgeInfo", "White lines along sharp creases (wear masks, highlights), black elsewhere.")),
			VrrMenu::Row(LOCTEXT("EdgeMinAngle", "Start angle (deg)"),
				VrrMenu::FloatField(&Settings.EdgeMinAngle, 0.0f, 180.0f, 0.5f),
				LOCTEXT("EdgeMinAngleTip", "Creases where faces meet at less than this angle are ignored. Keeps smooth curved surfaces black")),
			VrrMenu::Row(LOCTEXT("EdgeFullAngle", "Full angle (deg)"),
				VrrMenu::FloatField(&Settings.EdgeFullAngle, 0.0f, 180.0f, 0.5f),
				LOCTEXT("EdgeFullAngleTip", "Creases at or above this angle are fully white; in between they fade")),
			VrrMenu::Row(LOCTEXT("EdgeWidth", "Width (cm)"),
				VrrMenu::FloatField(&Settings.EdgeWidth, 0.001f, 50.0f, 0.01f),
				LOCTEXT("EdgeWidthTip", "Distance from the crease where the line fades to black. Lines spread into the triangles touching the crease, so very wide lines get cut off on large triangles")),
			VrrMenu::Row(LOCTEXT("EdgeCreases", "Creases"),
				VrrMenu::CheckBox(&Settings.bEdgeConcave, LOCTEXT("EdgeConcave", "Include inward creases"),
					LOCTEXT("EdgeConcaveTip", "Also mark valley creases. Off marks only outward edges (where wear would show)"))),
		}),

		MapCard(LOCTEXT("Curvature", "Curvature"), &Settings.bBakeCurvature,
			LOCTEXT("CurvatureTip", "Mid gray on flat surfaces, brighter where the surface bends outward, darker where it bends inward"), {
			VrrMenu::HelpText(LOCTEXT("CurvatureInfo", "Mid gray on flat surfaces, brighter where the surface bends outward, darker where it bends inward.")),
			VrrMenu::Row(LOCTEXT("CurvatureRadius", "Full radius (cm)"),
				VrrMenu::FloatField(&Settings.CurvatureRadius, 0.01f, 500.0f, 0.05f),
				LOCTEXT("CurvatureRadiusTip", "A surface curved this tightly (or tighter) reaches full white or black. Raise it to pick up gentler curves")),
			VrrMenu::Row(LOCTEXT("CurvatureBlur", "Blur"),
				VrrMenu::IntField(&Settings.CurvatureBlur, 0, 50),
				LOCTEXT("CurvatureBlurTip", "Neighbor averaging passes. Softens noise on dense meshes")),
		}),

		MapCard(LOCTEXT("AO", "Ambient Occlusion"), &Settings.bBakeAO,
			LOCTEXT("AOTip", "White where the surface sees open sky, darker where nearby parts of the mesh block it. Ray traced, so it is the slowest map"), {
			VrrMenu::HelpText(LOCTEXT("AOInfo", "White where the surface is open, darker where nearby geometry blocks it. Ray traced per texel: the slowest map, and the bake can be cancelled.")),
			VrrMenu::Row(LOCTEXT("AORays", "Rays per texel"),
				VrrMenu::IntField(&Settings.AORays, 4, 1024),
				LOCTEXT("AORaysTip", "More rays give smoother shading but bake time grows with it. 32-64 for previews, 128-256 for final")),
			VrrMenu::Row(LOCTEXT("AODistance", "Max distance (cm)"),
				VrrMenu::FloatField(&Settings.AODistance, 0.01f, 1000.0f, 0.1f),
				LOCTEXT("AODistanceTip", "Geometry farther than this doesn't occlude. Small values give tight contact shadows; large values darken whole recessed areas")),
			VrrMenu::Row(LOCTEXT("AOStrength", "Strength"),
				VrrMenu::FloatField(&Settings.AOStrength, 0.0f, 8.0f, 0.05f),
				LOCTEXT("AOStrengthTip", "Contrast curve applied to the result. 1 is unchanged, above 1 darkens the occlusion, below 1 lightens it")),
			VrrMenu::Row(LOCTEXT("AOBias", "Bias (cm)"),
				VrrMenu::FloatField(&Settings.AOBias, 0.0f, 5.0f, 0.001f),
				LOCTEXT("AOBiasTip", "How far above the surface rays start. Raise it if flat areas show speckles or dark acne; too high misses tight contact shadows")),
			VrrMenu::Row(LOCTEXT("AOBlur", "Blur"),
				VrrMenu::IntField(&Settings.AOBlur, 0, 16),
				LOCTEXT("AOBlurTip", "3x3 smoothing passes on the baked texels. Hides ray noise; stays within connected surface so UV islands don't bleed")),
			VrrMenu::Row(LOCTEXT("AOFalloff", "Falloff"),
				VrrMenu::CheckBox(&Settings.bAODistanceFalloff, LOCTEXT("AODistanceFalloff", "Fade with distance"),
					LOCTEXT("AODistanceFalloffTip", "Close hits occlude fully and hits near the max distance barely, for a softer gradient. Off: any hit within the distance counts fully"))),
			VrrMenu::Row(LOCTEXT("AOBackFaces", "Back faces"),
				VrrMenu::CheckBox(&Settings.bAOIgnoreBackFaces, LOCTEXT("AOIgnoreBackFaces", "Ignore back faces"),
					LOCTEXT("AOIgnoreBackFacesTip", "Rays that hit the inside of a surface don't count. Keeps open meshes and overlapping shells (cloth over a body) from going black"))),
		}),
	});
}

/** UV channel, resolution, padding, folder, base name, packed or split textures, and overwrite. */
TSharedRef<SWidget> SMenu_BakeMaps::CreateOutputSection()
{
	return VrrMenu::HelpCard(LOCTEXT("OutputHeader", "Output"), LOCTEXT("MapsInfo", "Maps are computed from the mesh's own LOD 0 geometry and saved as linear textures. Distances are in mesh units (cm)."), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("UVChannel", "UV channel"),
			VrrMenu::UVChannelCombo([this]() { return NumUVChannels; }, &Settings.UVChannel)),
		VrrMenu::Row(LOCTEXT("Resolution", "Resolution"),
			VrrMenu::ResolutionCombo(&Settings.Resolution, { 256, 512, 1024, 2048, 4096 })),
		VrrMenu::Row(LOCTEXT("Padding", "Padding (px)"),
			VrrMenu::IntField(&Settings.Padding, 0, 64),
			LOCTEXT("PaddingTip", "Grow each UV island by this many pixels so filtering and mips at the edges don't pick up empty texels")),
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("BaseName", "Base name"),
			VrrMenu::NameField(&Settings.OutputBaseName),
			LOCTEXT("BaseNameTip", "The packed mask is named <Base name>_CCE, or <Base name>_CCEA when it includes Ambient Occlusion; split textures are <Base name>_Curvature, _Cavity, _Edge and _AO")),
		VrrMenu::Row(LOCTEXT("Textures", "Textures"),
			VrrMenu::CheckBox(&Settings.bSplitTextures, LOCTEXT("SplitTextures", "Split into separate textures"),
				LOCTEXT("SplitTexturesTip", "Off: one packed mask with R = Curvature, G = Cavity, B = Edge. Without Ambient Occlusion it is RGB only (_CCE, smaller in memory); "
					"with it, A = Ambient Occlusion and the name is _CCEA. Unchecked maps get their neutral value (0.5, white, black).\n"
					"On: one linear grayscale texture per checked map (_Curvature, _Cavity, _Edge, _AO)"))),
		VrrMenu::Row(LOCTEXT("TextureSets", "Texture sets"),
			VrrMenu::CheckBox(&Settings.bSplitByShadingModel, LOCTEXT("SplitByShadingModel", "One per shading model"),
				LOCTEXT("SplitByShadingModelTip", "For materials whose UVs overlap each other: group the material slots by shading model (Default Lit, Preintegrated Skin, Subsurface Profile...) "
					"and bake one texture set per group, drawing only that group's triangles, named <Base name>_<shading model>_CCE...\n"
					"The maps are still computed from the whole mesh, so other parts still occlude and curvature doesn't break at material borders"))),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			if (Settings.bSplitByShadingModel && !ShadingModelGroups.IsEmpty())
			{
				TArray<FString> Sets;
				for (const FConsolidateGroup& Group : ShadingModelGroups)
				{
					Sets.Add(FString::Printf(TEXT("%s_%s (%s, %d slots)"), *Settings.OutputBaseName, *Group.Tag, *Group.Label.ToString(), Group.Slots.Num()));
				}
				return FText::Format(LOCTEXT("SetNames", "One set per shading model: {0}. Each gets the textures below."), FText::FromString(FString::Join(Sets, TEXT(", "))));
			}
			return FText::GetEmpty();
		})),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			return Settings.bSplitTextures
				? FText::Format(LOCTEXT("SplitNames", "Creates {0}_Curvature, {0}_Cavity, {0}_Edge and {0}_AO (G8, linear)"), FText::FromString(Settings.OutputBaseName))
				: Settings.bBakeAO
					? FText::Format(LOCTEXT("PackedNamesAO", "Creates {0}{1} (RGBA, Masks compression, linear): R Curvature, G Cavity, B Edge, A Ambient Occlusion"),
						FText::FromString(Settings.OutputBaseName), FText::FromString(MeshMapBaker::PackedSuffix(true)))
					: FText::Format(LOCTEXT("PackedNames", "Creates {0}{1} (RGB, Masks compression, linear): R Curvature, G Cavity, B Edge"),
						FText::FromString(Settings.OutputBaseName), FText::FromString(MeshMapBaker::PackedSuffix(false)));
		})),
		VrrMenu::Row(LOCTEXT("Existing", "Existing textures"),
			VrrMenu::CheckBox(&Settings.bOverwrite, LOCTEXT("Overwrite", "Overwrite"),
				LOCTEXT("OverwriteTip", "Re-baking updates textures with the same name in place, so materials using them pick up the change. Off creates numbered copies"))),
	}));
}

/** Bake button and the status line. */
TSharedRef<SWidget> SMenu_BakeMaps::CreateBakeSection()
{
	return VrrMenu::Card(LOCTEXT("BakeHeader", "Bake"), VrrMenu::Rows({
		VrrMenu::ActionButton(LOCTEXT("Bake", "Bake Maps"),
			LOCTEXT("BakeTip", "Bake the checked maps into textures"),
			[this]() { return GetBakeBlocker(); },
			FOnClicked::CreateSP(this, &SMenu_BakeMaps::OnBakeClicked)),
		VrrMenu::StatusLine(&StatusText, &bStatusIsError),
	}));
}

// BEHAVIOR
/** Switches to a new mesh: resets the per-mesh state and suggests output folder, name and UV channel. */
void SMenu_BakeMaps::SetMesh(UObject* NewMesh)
{
	Mesh = VrrCore::IsStaticOrSkeletalMesh(NewMesh) ? NewMesh : nullptr;
	NumUVChannels = 0;
	ShadingModelGroups.Reset();
	StatusText = FText::GetEmpty();

	if (Mesh.IsValid())
	{
		NumUVChannels = VrrCore::GetNumUVChannels(Mesh.Get());
		Settings.OutputFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		Settings.OutputBaseName = MeshMapBaker::SuggestBaseName(Mesh.Get());
		ShadingModelGroups = MaterialConsolidator::GroupByShadingModel(MaterialConsolidator::GetSlots(Mesh.Get()));
	}
	Settings.UVChannel = 0;
}

/** Why Bake can't run yet (shown next to the button), or empty when it can. */
FText SMenu_BakeMaps::GetBakeBlocker() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NeedMesh", "Pick a mesh first.");
	}
	if (!VrrCore::GetMeshDescription(Mesh.Get(), 0))
	{
		return LOCTEXT("NeedMeshData", "This mesh has no LOD 0 source data.");
	}
	if (NumUVChannels == 0)
	{
		return LOCTEXT("NeedUVs", "This mesh has no UVs to bake through.");
	}
	if (!Settings.HasAnyMap())
	{
		return LOCTEXT("NeedMap", "Turn on at least one map.");
	}
	return FText::GetEmpty();
}

/** "Static Mesh | N vertices, N triangles | N UV channels" or why the mesh can't be baked. */
FText SMenu_BakeMaps::GetMeshInfoText() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NoMesh", "Pick a Static Mesh or Skeletal Mesh.");
	}
	const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh.Get(), 0);
	if (!MeshDescription)
	{
		return LOCTEXT("NoMeshData", "This mesh has no source mesh data for LOD 0.");
	}
	return FText::Format(LOCTEXT("MeshInfo", "{0}  |  {1} vertices, {2} triangles  |  {3} UV channels"),
		VrrCore::IsSkeletalMesh(Mesh.Get()) ? LOCTEXT("Skeletal", "Skeletal Mesh") : LOCTEXT("Static", "Static Mesh"),
		MeshDescription->Vertices().Num(),
		MeshDescription->Triangles().Num(),
		NumUVChannels);
}

/** Bakes, then shows the result and selects the new textures in the Content Browser. */
FReply SMenu_BakeMaps::OnBakeClicked()
{
	if (!GetBakeBlocker().IsEmpty())
	{
		return FReply::Handled();
	}

	const FMeshMapBakeResult Result = MeshMapBaker::Bake(Mesh.Get(), Settings);

	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("BakeDone", "Mesh maps baked") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);
	VrrCore::ShowInContentBrowser(TArray<UObject*>(Result.Textures));
	return FReply::Handled();
}

/** Text (and error color) for the status line under the Bake button. */
void SMenu_BakeMaps::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

#undef LOCTEXT_NAMESPACE
