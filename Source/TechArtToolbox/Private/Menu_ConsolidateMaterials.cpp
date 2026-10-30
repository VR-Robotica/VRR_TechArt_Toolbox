// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Toolbox > Consolidate Materials page (UI only).
 *
 * The page edits an FConsolidateSettings and hands it to MaterialConsolidator::Consolidate (Core_ConsolidateMaterials.cpp),
 * which bakes every slot's material through the target UVs and combines them into one texture per output.
 */

#include "Menu_ConsolidateMaterials.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "MenuWidgets.h"
#include "Misc/PackageName.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_ConsolidateMaterials"

namespace
{
	FText GetMapToolTip(EConsolidateMap Map)
	{
		switch (Map)
		{
		case EConsolidateMap::BaseColor:		return LOCTEXT("BaseColorTip", "Base Color (sRGB) -> _D");
		case EConsolidateMap::Metallic:			return LOCTEXT("MetallicTip", "Metallic (linear, one channel) -> _M");
		case EConsolidateMap::Specular:			return LOCTEXT("SpecularTip", "Specular (linear, one channel) -> _S");
		case EConsolidateMap::Roughness:		return LOCTEXT("RoughnessTip", "Roughness (linear, one channel) -> _R");
		case EConsolidateMap::Normal:			return LOCTEXT("NormalTip", "Tangent-space normal (Normalmap compression) -> _N. Relative to this mesh's own tangents, so it shades the same on this mesh");
		case EConsolidateMap::Emissive:			return LOCTEXT("EmissiveTip", "Emissive color, kept in HDR (values above 1 survive) -> _E");
		case EConsolidateMap::Opacity:			return LOCTEXT("OpacityTip", "Opacity of translucent materials (linear, one channel) -> _O");
		case EConsolidateMap::OpacityMask:		return LOCTEXT("OpacityMaskTip", "Opacity Mask of masked materials (linear, one channel) -> _OM");
		case EConsolidateMap::AmbientOcclusion:	return LOCTEXT("AOTip", "The materials' Ambient Occlusion output (linear, one channel) -> _AO");
		default:								return FText::GetEmpty();
		}
	}
}

/** Builds the page: mesh and its slots, target UVs, outputs, then save and consolidate. */
void SMenu_ConsolidateMaterials::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		VrrMenu::Stack({
			CreateMeshSection(),
			CreateTargetSection(),
			CreateMapsSection(),
			CreateSaveSection(),
		})
	];
}

void SMenu_ConsolidateMaterials::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(ResultTextures);
}

// LAYOUT
/** Mesh picker, its info, and the material slots that will be combined. */
TSharedRef<SWidget> SMenu_ConsolidateMaterials::CreateMeshSection()
{
	return VrrMenu::HelpCard(LOCTEXT("MeshHeader", "Mesh"),
		LOCTEXT("MeshHelp", "Every material slot of the mesh (LOD 0) is rendered through the target UVs, drawing only that slot's triangles. "
			"Each material runs as it does on the mesh (its own UVs, vertex colors, parameters and math), so the textures hold its final outputs. "
			"The slots are then combined, so one texture per output covers the whole mesh, ready for a single material sampling the target UVs."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("MeshLabel", "Mesh"),
				VrrMenu::MeshPicker(VrrMenu::EMeshTypes::StaticOrSkeletal,
					[this]() { return Mesh.Get(); },
					[this](UObject* NewMesh) { SetMesh(NewMesh); },
					[this](const FText& Error) { SetStatus(Error, true); })),
			VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_ConsolidateMaterials::GetMeshInfoText)),
			VrrMenu::Row(LOCTEXT("SlotsLabel", "Material slots"), SAssignNew(SlotList, SVerticalBox)),
		}));
}

/** Target UVs, resolution and padding. */
TSharedRef<SWidget> SMenu_ConsolidateMaterials::CreateTargetSection()
{
	return VrrMenu::Card(LOCTEXT("TargetHeader", "Target"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("TargetUVs", "Target UVs"), VrrMenu::UVChannelCombo([this]() { return NumUVChannels; }, &Settings.TargetChannel),
			LOCTEXT("TargetUVsTip", "The channel the combined textures are laid out in. Every slot must have its own space in it (no overlapping islands), such as a channel from UV Layouts")),
		VrrMenu::Row(LOCTEXT("Resolution", "Resolution"), VrrMenu::ResolutionCombo(&Settings.Resolution, { 512, 1024, 2048, 4096, 8192 }),
			LOCTEXT("ResolutionTip", "Width and height of each texture")),
		VrrMenu::Row(LOCTEXT("Padding", "Padding (px)"), VrrMenu::IntField(&Settings.Padding, 0, 64),
			LOCTEXT("PaddingTip", "Grow each island by this many pixels so filtering and mips at the edges don't pick up empty texels")),
		VrrMenu::Row(LOCTEXT("TextureSets", "Texture sets"),
			VrrMenu::CheckBox(&Settings.bSplitByShadingModel, LOCTEXT("SplitByShadingModel", "One per shading model"),
				LOCTEXT("SplitByShadingModelTip", "Group the slots by their material's shading model (Default Lit, Preintegrated Skin, Subsurface Profile...) and write one texture set per group, "
					"named <base name>_<shading model>_<suffix>, for one material per shading model. "
					"Slots only need their own space in the target UVs within their group; different groups may overlap"))),
	}));
}

/** One checkbox per output, and the flat-output option. */
TSharedRef<SWidget> SMenu_ConsolidateMaterials::CreateMapsSection()
{
	constexpr int32 Columns = 3;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(0.0f, 2.0f, 24.0f, 2.0f));
	for (int32 MapIndex = 0; MapIndex < static_cast<int32>(EConsolidateMap::Count); ++MapIndex)
	{
		const EConsolidateMap Map = static_cast<EConsolidateMap>(MapIndex);
		Grid->AddSlot(MapIndex % Columns, MapIndex / Columns)
		[
			VrrMenu::CheckBox(&Settings.bMaps[MapIndex], MaterialConsolidator::GetMapLabel(Map), GetMapToolTip(Map))
		];
	}

	return VrrMenu::Card(LOCTEXT("MapsHeader", "Outputs"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("MapsLabel", "Bake"), Grid),
		VrrMenu::Row(LOCTEXT("FlatLabel", "Flat outputs"),
			VrrMenu::CheckBox(&Settings.bSkipFlatMaps, LOCTEXT("SkipFlat", "Skip outputs with one value everywhere"),
				LOCTEXT("SkipFlatTip", "An output that is the same in every material (Metallic 0, no Emissive...) isn't written; the result lists its value, to use as a constant in the material"))),
	}));
}

/** Folder, base name, overwrite and save to disk, then the Consolidate button, status and result previews. */
TSharedRef<SWidget> SMenu_ConsolidateMaterials::CreateSaveSection()
{
	return VrrMenu::Card(LOCTEXT("SaveHeader", "Save"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("Name", "Base name"),
			VrrMenu::NameField(&Settings.OutputBaseName),
			LOCTEXT("NameTip", "Each texture is <base name>_<suffix> (_D, _N, _R...). Starts as T_<mesh>")),
		VrrMenu::Row(LOCTEXT("Existing", "Existing textures"),
			VrrMenu::CheckBox(&Settings.bOverwrite, LOCTEXT("Overwrite", "Overwrite"),
				LOCTEXT("OverwriteTip", "Consolidating again updates textures with the same names in place, so materials using them pick up the change. Off creates numbered copies"))),
		VrrMenu::Row(LOCTEXT("SaveToDisk", "Save"),
			VrrMenu::CheckBox(&Settings.bSaveToDisk, LOCTEXT("SaveToDiskCheck", "Save to disk when done"),
				LOCTEXT("SaveToDiskTip", "Save the textures' packages right away. Off leaves them unsaved (marked dirty) in the Content Browser"))),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_ConsolidateMaterials::GetOutputNamesText)),
		VrrMenu::ActionButton(LOCTEXT("Consolidate", "Consolidate Materials"),
			LOCTEXT("ConsolidateTip", "Bake every slot's material outputs through the target UVs and combine them into one texture set"),
			[this]() { return GetConsolidateBlocker(); },
			FOnClicked::CreateSP(this, &SMenu_ConsolidateMaterials::OnConsolidateClicked)),
		VrrMenu::StatusLine(&StatusText, &bStatusIsError),
		SAssignNew(ResultPreviews, SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.0f, 8.0f)),
	}));
}

// BEHAVIOR
/** Switches to a new mesh: reads its slots and channels, picks UV1 (or UV0) and suggests a folder and name. */
void SMenu_ConsolidateMaterials::SetMesh(UObject* NewMesh)
{
	Mesh = VrrCore::IsStaticOrSkeletalMesh(NewMesh) ? NewMesh : nullptr;
	NumUVChannels = Mesh.IsValid() ? VrrCore::GetNumUVChannels(Mesh.Get()) : 0;
	Slots = Mesh.IsValid() ? MaterialConsolidator::GetSlots(Mesh.Get()) : TArray<FConsolidateSlot>();
	StatusText = FText::GetEmpty();

	Settings.TargetChannel = FMath::Min(1, FMath::Max(NumUVChannels - 1, 0));
	if (Mesh.IsValid())
	{
		Settings.OutputFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		Settings.OutputBaseName = MaterialConsolidator::SuggestBaseName(Mesh.Get());
	}
	RefreshSlotList();
}

/** One line per slot: index, slot name, material and triangle count. */
void SMenu_ConsolidateMaterials::RefreshSlotList()
{
	if (!SlotList.IsValid())
	{
		return;
	}
	SlotList->ClearChildren();
	if (Slots.IsEmpty())
	{
		SlotList->AddSlot().AutoHeight()
		[
			SNew(STextBlock)
			.Font(VrrMenu::HelpFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Text(Mesh.IsValid() ? LOCTEXT("NoSlots", "No slot has triangles on LOD 0.") : LOCTEXT("PickMeshForSlots", "Pick a mesh to list its slots."))
		];
		return;
	}
	for (const FConsolidateSlot& Slot : Slots)
	{
		SlotList->AddSlot().AutoHeight().Padding(0.0f, 1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(32.0f)
				[
					SNew(STextBlock).ColorAndOpacity(FSlateColor::UseSubduedForeground()).Text(FText::Format(LOCTEXT("SlotIndex", "#{0}"), Slot.SlotIndex))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(200.0f)
				[
					SNew(STextBlock).Text(FText::FromName(Slot.SlotName))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(240.0f)
				[
					SNew(STextBlock)
					.Text(Slot.Material ? FText::FromString(Slot.Material->GetName()) : LOCTEXT("NoMaterial", "(none: default material)"))
					.ColorAndOpacity(Slot.Material ? FSlateColor::UseForeground() : FSlateColor(VrrMenu::WarningColor()))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(160.0f)
				[
					SNew(STextBlock)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(Slot.ShadingModelLabel)
					.ToolTipText(LOCTEXT("SlotShadingModelTip", "The material's shading model. With one texture set per shading model, slots sharing it are combined"))
				]
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Text(FText::Format(LOCTEXT("SlotTriangles", "{0} triangles"), Slot.NumTriangles))
			]
		];
	}
}

/** Previews of the last run's textures, each with its name under it. */
void SMenu_ConsolidateMaterials::RefreshResultPreviews()
{
	if (!ResultPreviews.IsValid())
	{
		return;
	}
	ResultPreviews->ClearChildren();
	ResultBrushes.Reset();
	for (UTexture2D* Texture : ResultTextures)
	{
		if (!Texture)
		{
			continue;
		}
		// Each preview needs its own brush, kept alive (with its texture, see AddReferencedObjects) while it draws
		TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
		VrrMenu::SetBrushTexture(*Brush, Texture);
		ResultBrushes.Add(Brush);

		ResultPreviews->AddSlot()
		[
			SNew(SBox)
			.WidthOverride(PreviewSize)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					VrrMenu::TexturePreview(Brush.Get(), PreviewSize, true)
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(VrrMenu::HelpFont())
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text(FText::FromString(Texture->GetName()))
					.ToolTipText(FText::FromString(Texture->GetPathName()))
				]
			]
		];
	}
}

/** "Static Mesh | N slots | N triangles | N UV channels" or why the mesh can't be used. */
FText SMenu_ConsolidateMaterials::GetMeshInfoText() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NoMesh", "Pick a Static Mesh or Skeletal Mesh with more than one material slot.");
	}
	const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh.Get(), 0);
	if (!MeshDescription)
	{
		return LOCTEXT("NoMeshData", "This mesh has no source mesh data for LOD 0.");
	}
	return FText::Format(LOCTEXT("MeshInfo", "{0}  |  {1} slots with triangles  |  {2} triangles  |  {3} UV channels"),
		VrrCore::IsSkeletalMesh(Mesh.Get()) ? LOCTEXT("Skeletal", "Skeletal Mesh") : LOCTEXT("Static", "Static Mesh"),
		Slots.Num(), MeshDescription->Triangles().Num(), NumUVChannels);
}

/** "Writes /Game/Props/T_Crate_D, _M, _R, _N...", or one such line per shading model's texture set. */
FText SMenu_ConsolidateMaterials::GetOutputNamesText() const
{
	if (Settings.OutputBaseName.IsEmpty() || !Settings.HasAnyMap())
	{
		return FText::GetEmpty();
	}
	TArray<FString> Suffixes;
	for (int32 MapIndex = 0; MapIndex < static_cast<int32>(EConsolidateMap::Count); ++MapIndex)
	{
		if (Settings.bMaps[MapIndex])
		{
			Suffixes.Add(FString(TEXT("_")) + MaterialConsolidator::GetMapSuffix(static_cast<EConsolidateMap>(MapIndex)));
		}
	}
	const FString Folder = VrrCore::NormalizeContentFolder(Settings.OutputFolder);
	const FString SuffixList = FString::Join(Suffixes, TEXT(", "));
	if (!Settings.bSplitByShadingModel || Slots.IsEmpty())
	{
		return FText::Format(LOCTEXT("OutputPath", "Writes {0}/{1}{2}"),
			FText::FromString(Folder), FText::FromString(Settings.OutputBaseName), FText::FromString(SuffixList));
	}
	TArray<FString> Lines;
	for (const FConsolidateGroup& Group : MaterialConsolidator::GetGroups(Slots, Settings))
	{
		Lines.Add(FText::Format(LOCTEXT("OutputPathGroup", "{0} ({1} slots): writes {2}/{3}{4}"), Group.Label, Group.Slots.Num(),
			FText::FromString(Folder), FText::FromString(MaterialConsolidator::GetGroupBaseName(Group, Settings)), FText::FromString(SuffixList)).ToString());
	}
	return FText::FromString(FString::Join(Lines, TEXT("\n")));
}

FText SMenu_ConsolidateMaterials::GetConsolidateBlocker() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NeedMesh", "Pick a mesh first.");
	}
	if (!VrrCore::GetMeshDescription(Mesh.Get(), 0))
	{
		return LOCTEXT("NeedMeshData", "This mesh has no LOD 0 source data.");
	}
	if (Slots.Num() < 2)
	{
		return LOCTEXT("NeedSlots", "The mesh needs more than one material slot.");
	}
	if (NumUVChannels < 1)
	{
		return LOCTEXT("NeedUVs", "The mesh has no UV channels.");
	}
	if (!Settings.HasAnyMap())
	{
		return LOCTEXT("NeedMap", "Tick at least one output.");
	}
	if (Settings.OutputBaseName.IsEmpty())
	{
		return LOCTEXT("NeedName", "Enter a base name for the textures.");
	}
	return FText::GetEmpty();
}

/** Consolidates, then previews the textures and selects them in the Content Browser. */
FReply SMenu_ConsolidateMaterials::OnConsolidateClicked()
{
	if (!GetConsolidateBlocker().IsEmpty())
	{
		return FReply::Handled();
	}
	const FConsolidateResult Result = MaterialConsolidator::Consolidate(Mesh.Get(), Settings);
	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("ConsolidateDone", "Materials consolidated") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);

	ResultTextures.Reset();
	for (UTexture2D* Texture : Result.Textures)
	{
		ResultTextures.Add(Texture);
	}
	RefreshResultPreviews();
	if (!Result.Textures.IsEmpty())
	{
		TArray<UObject*> Assets(Result.Textures);
		VrrCore::ShowInContentBrowser(Assets);
	}
	return FReply::Handled();
}

void SMenu_ConsolidateMaterials::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

#undef LOCTEXT_NAMESPACE
