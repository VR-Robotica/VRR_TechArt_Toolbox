// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Mesh Maps page (UI only).
 *
 * The page edits an FMeshMapBakeSettings and hands it to MeshMapBaker::Bake (Core_BakeMaps.cpp), which analyzes
 * the mesh and writes the textures. This file builds the widgets and shows the result.
 */

#include "Menu_BakeMaps.h"

#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PropertyCustomizationHelpers.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_BakeMaps"

/** Builds the page: mesh, one card per map, output and bake. */
void SMenu_BakeMaps::Construct(const FArguments& InArgs)
{
	for (int32 Size : { 256, 512, 1024, 2048, 4096 })
	{
		ResolutionOptions.Add(MakeShared<int32>(Size));
	}
	Resolution = ResolutionOptions[3]; // 2048

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
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(VrrMenu::WideWidth)
				[
					SNew(SObjectPropertyEntryBox)
					.AllowedClass(UStreamableRenderAsset::StaticClass())
					.OnShouldFilterAsset_Lambda([](const FAssetData& AssetData)
					{
						return !AssetData.IsInstanceOf(UStaticMesh::StaticClass()) && !AssetData.IsInstanceOf(USkeletalMesh::StaticClass());
					})
					.ObjectPath_Lambda([this]() { return Mesh.IsValid() ? Mesh->GetPathName() : FString(); })
					.OnObjectChanged_Lambda([this](const FAssetData& AssetData) { SetMesh(AssetData.GetAsset()); })
					.DisplayThumbnail(true)
				]
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("UseSelection", "Use Selected"))
				.ToolTipText(LOCTEXT("UseSelectionTip", "Use the static or skeletal mesh selected in the Content Browser"))
				.OnClicked(this, &SMenu_BakeMaps::OnUseSelectionClicked)
			]),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_BakeMaps::GetMeshInfoText)),
		VrrMenu::Row(LOCTEXT("WeldDistance", "Weld distance (cm)"),
			VrrMenu::FloatField(&Settings.WeldDistance, 0.0f, 10.0f, 0.001f),
			LOCTEXT("WeldDistanceTip", "Vertices closer than this count as the same point, so hard edges and UV seams don't split the surface")),
	}));
}

/** One card per map (Cavity, Edge, Curvature): the header checkbox turns it on, the rows tune it. */
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
	});
}

/** UV channel, resolution, padding, folder, base name, packed or split textures, and overwrite. */
TSharedRef<SWidget> SMenu_BakeMaps::CreateOutputSection()
{
	return VrrMenu::HelpCard(LOCTEXT("OutputHeader", "Output"), LOCTEXT("MapsInfo", "Maps are computed from the mesh's own LOD 0 geometry and saved as linear textures. Distances are in mesh units (cm)."), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("UVChannel", "UV channel"),
			SAssignNew(UVChannelCombo, SComboBox<TSharedPtr<int32>>)
			.OptionsSource(&UVChannelOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
			{
				return SNew(STextBlock).Text(FText::AsNumber(*Option));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Settings.UVChannel = *Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::AsNumber(Settings.UVChannel); })
			]),
		VrrMenu::Row(LOCTEXT("Resolution", "Resolution"),
			SNew(SComboBox<TSharedPtr<int32>>)
			.OptionsSource(&ResolutionOptions)
			.InitiallySelectedItem(Resolution)
			.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
			{
				return SNew(STextBlock).Text(FText::AsNumber(*Option, &FNumberFormattingOptions::DefaultNoGrouping()));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<int32> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Resolution = Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this]() { return FText::AsNumber(*Resolution, &FNumberFormattingOptions::DefaultNoGrouping()); })
			]),
		VrrMenu::Row(LOCTEXT("Padding", "Padding (px)"),
			VrrMenu::IntField(&Settings.Padding, 0, 64),
			LOCTEXT("PaddingTip", "Grow each UV island by this many pixels so filtering and mips at the edges don't pick up empty texels")),
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("BaseName", "Base name"),
			VrrMenu::NameField(&Settings.OutputBaseName),
			LOCTEXT("BaseNameTip", "The packed mask is named <Base name>_CCE; split textures are <Base name>_Curvature, _Cavity and _Edge")),
		VrrMenu::Row(LOCTEXT("Textures", "Textures"),
			VrrMenu::CheckBox(&Settings.bSplitTextures, LOCTEXT("SplitTextures", "Split into separate textures"),
				LOCTEXT("SplitTexturesTip", "Off: one packed RGB mask (_CCE) with R = Curvature, G = Cavity, B = Edge; unchecked maps get their neutral value (0.5, white, black).\n"
					"On: one linear grayscale texture per checked map (_Curvature, _Cavity, _Edge)"))),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			return Settings.bSplitTextures
				? FText::Format(LOCTEXT("SplitNames", "Creates {0}_Curvature, {0}_Cavity and {0}_Edge (G8, linear)"), FText::FromString(Settings.OutputBaseName))
				: FText::Format(LOCTEXT("PackedNames", "Creates {0}_CCE (Masks compression, linear): R Curvature, G Cavity, B Edge"), FText::FromString(Settings.OutputBaseName));
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
	StatusText = FText::GetEmpty();

	if (Mesh.IsValid())
	{
		NumUVChannels = VrrCore::GetNumUVChannels(Mesh.Get());
		Settings.OutputFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		Settings.OutputBaseName = MeshMapBaker::SuggestBaseName(Mesh.Get());
	}

	UVChannelOptions.Reset();
	for (int32 Channel = 0; Channel < NumUVChannels; ++Channel)
	{
		UVChannelOptions.Add(MakeShared<int32>(Channel));
	}
	Settings.UVChannel = 0;
	if (UVChannelCombo.IsValid())
	{
		UVChannelCombo->RefreshOptions();
		UVChannelCombo->SetSelectedItem(UVChannelOptions.IsEmpty() ? nullptr : UVChannelOptions[0]);
	}
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

/** Picks the first Static or Skeletal Mesh selected in the Content Browser. */
FReply SMenu_BakeMaps::OnUseSelectionClicked()
{
	for (const FAssetData& Asset : VrrCore::GetSelectedAssets())
	{
		if (Asset.IsInstanceOf(UStaticMesh::StaticClass()) || Asset.IsInstanceOf(USkeletalMesh::StaticClass()))
		{
			SetMesh(Asset.GetAsset());
			return FReply::Handled();
		}
	}

	SetStatus(LOCTEXT("NoMeshSelected", "Select a Static Mesh or Skeletal Mesh in the Content Browser first."), true);
	return FReply::Handled();
}

/** Bakes, then shows the result and selects the new textures in the Content Browser. */
FReply SMenu_BakeMaps::OnBakeClicked()
{
	if (!GetBakeBlocker().IsEmpty())
	{
		return FReply::Handled();
	}

	Settings.Resolution = Resolution.IsValid() ? *Resolution : 2048;
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
