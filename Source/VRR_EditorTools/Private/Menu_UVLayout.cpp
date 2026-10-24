// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > UV Layouts page (UI only).
 *
 * The page edits an FUVLayoutSettings, shows a preview of the LOD 0 layout, and hands the settings to
 * UVLayout::Apply (Core_UVLayout.cpp), which writes the channel into the mesh.
 */

#include "Menu_UVLayout.h"

#include "Core_Common.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "MenuWidgets.h"
#include "MeshDescription.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_UVLayout"

/**
 * Draws a UV layout: the 0-1 square, one tinted region per material slot with its name, the triangles filled
 * in the region's color, and island outlines. Fills and outlines are one custom-vertex draw each, so large meshes stay fast.
 */
class SUVLayoutPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SUVLayoutPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	void SetPreview(const TSharedPtr<FUVLayoutPreview>& InPreview)
	{
		Preview = InPreview;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(360.0f, 360.0f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(Geometry.GetLocalSize());
		const float Side = FMath::Min(Size.X, Size.Y);
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");

		// The area shown: the 0-1 square, or every UDIM tile (scaled down to fit, aspect kept)
		FBox2f View(FVector2f(0.0f), FVector2f(1.0f));
		const bool bHasPreview = Preview.IsValid() && !Preview->IsEmpty();
		if (bHasPreview && Preview->bUDIM)
		{
			for (const FBox2f& Region : Preview->Regions)
			{
				View += Region;
			}
		}
		const FVector2f ViewSize = View.GetSize();
		const float Zoom = Side / FMath::Max(ViewSize.X, ViewSize.Y);
		const FVector2f ViewMin = View.Min;
		auto ToLocal = [Zoom, ViewMin](const FVector2f& UV) { return (UV - ViewMin) * Zoom; };

		// The backdrop, with a faint quarter grid in every unit tile
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(ViewSize * Zoom, FSlateLayoutTransform()),
			White, ESlateDrawEffect::None, FLinearColor(0.012f, 0.012f, 0.014f, 1.0f));
		for (int32 i = 1; i < FMath::RoundToInt32(ViewSize.X * 4.0f); ++i)
		{
			const float T = Zoom * i / 4.0f;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(T, 0.0f), FVector2f(T, ViewSize.Y * Zoom) },
				ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, i % 4 == 0 ? 0.2f : 0.06f), false);
		}
		for (int32 i = 1; i < FMath::RoundToInt32(ViewSize.Y * 4.0f); ++i)
		{
			const float T = Zoom * i / 4.0f;
			FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(), TArray<FVector2f>{ FVector2f(0.0f, T), FVector2f(ViewSize.X * Zoom, T) },
				ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, i % 4 == 0 ? 0.2f : 0.06f), false);
		}

		if (!bHasPreview)
		{
			return LayerId + 1;
		}

		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		auto Vertex = [&Transform](const FVector2f& Position, const FColor& Color)
		{
			return FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, Position, FVector2f::ZeroVector, Color);
		};

		// Regions: faint fill and outline
		for (int32 Region = 0; Region < Preview->Regions.Num(); ++Region)
		{
			const FBox2f& Box = Preview->Regions[Region];
			const FVector2f Min = ToLocal(Box.Min);
			const FVector2f Max = ToLocal(Box.Max);
			FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1, Geometry.ToPaintGeometry(FVector2f(Max - Min), FSlateLayoutTransform(Min)),
				White, ESlateDrawEffect::None, RegionColor(Region).CopyWithNewOpacity(0.06f));
			if (Preview->Regions.Num() > 1 || Preview->bUDIM)
			{
				FSlateDrawElement::MakeLines(OutDrawElements, LayerId + 2, Geometry.ToPaintGeometry(),
					TArray<FVector2f>{ Min, FVector2f(Max.X, Min.Y), Max, FVector2f(Min.X, Max.Y), Min },
					ESlateDrawEffect::None, RegionColor(Region).CopyWithNewOpacity(0.5f), false);
			}
		}

		// Triangles, filled in their region's color
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(Preview->Triangles.Num());
		Indices.Reserve(Preview->Triangles.Num());
		for (int32 Triangle = 0; Triangle < Preview->TriangleGroups.Num(); ++Triangle)
		{
			const FColor Color = RegionColor(Preview->TriangleGroups[Triangle]).CopyWithNewOpacity(0.45f).ToFColor(true);
			for (int32 i = 0; i < 3; ++i)
			{
				Indices.Add(static_cast<SlateIndex>(Vertices.Num()));
				Vertices.Add(Vertex(ToLocal(Preview->Triangles[Triangle * 3 + i]), Color));
			}
		}
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId + 3, Handle, Vertices, Indices, nullptr, 0, 0);

		// Island outlines: each segment as a thin quad
		Vertices.Reset();
		Indices.Reset();
		const FColor OutlineColor = FLinearColor(1.0f, 1.0f, 1.0f, 0.8f).ToFColor(true);
		for (int32 i = 0; i + 1 < Preview->Outlines.Num(); i += 2)
		{
			const FVector2f A = ToLocal(Preview->Outlines[i]);
			const FVector2f B = ToLocal(Preview->Outlines[i + 1]);
			const FVector2f Along = (B - A).GetSafeNormal();
			const FVector2f Side2 = FVector2f(-Along.Y, Along.X) * 0.5f;
			const SlateIndex First = static_cast<SlateIndex>(Vertices.Num());
			Vertices.Add(Vertex(A - Side2, OutlineColor));
			Vertices.Add(Vertex(A + Side2, OutlineColor));
			Vertices.Add(Vertex(B + Side2, OutlineColor));
			Vertices.Add(Vertex(B - Side2, OutlineColor));
			Indices.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
		if (!Vertices.IsEmpty())
		{
			FSlateDrawElement::MakeCustomVerts(OutDrawElements, LayerId + 4, Handle, Vertices, Indices, nullptr, 0, 0);
		}

		// Region names (and UDIM tile numbers)
		if (Preview->Regions.Num() > 1 || Preview->bUDIM)
		{
			const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 8);
			for (int32 Region = 0; Region < Preview->Regions.Num(); ++Region)
			{
				const FVector2f Min = Preview->Regions[Region].Min;
				const FVector2f Corner = ToLocal(Min) + FVector2f(4.0f, 2.0f);
				const FString Name = Preview->RegionNames[Region].ToString();
				const FString Label = Preview->bUDIM
					? FString::Printf(TEXT("%d: %d %s"), Region, 1001 + FMath::RoundToInt32(Min.X) - 10 * FMath::RoundToInt32(Min.Y), *Name)
					: FString::Printf(TEXT("%d: %s"), Region, *Name);
				FSlateDrawElement::MakeText(OutDrawElements, LayerId + 5, Geometry.ToPaintGeometry(FVector2f(Side, 14.0f), FSlateLayoutTransform(Corner)),
					Label, Font, ESlateDrawEffect::None, RegionColor(Region));
			}
		}
		return LayerId + 5;
	}

private:
	TSharedPtr<FUVLayoutPreview> Preview;

	static FLinearColor RegionColor(int32 Region)
	{
		static const FLinearColor Colors[] = {
			FLinearColor(0.20f, 0.55f, 1.00f), FLinearColor(1.00f, 0.55f, 0.15f), FLinearColor(0.35f, 0.85f, 0.35f), FLinearColor(0.90f, 0.30f, 0.60f),
			FLinearColor(0.95f, 0.85f, 0.25f), FLinearColor(0.55f, 0.40f, 0.95f), FLinearColor(0.25f, 0.85f, 0.85f), FLinearColor(0.95f, 0.35f, 0.30f) };
		return Colors[FMath::Abs(Region) % UE_ARRAY_COUNT(Colors)];
	}
};

/** Builds the page: general settings (full width), then the balanced and grouped layouts (settings | preview). */
void SMenu_UVLayout::Construct(const FArguments& InArgs)
{
	PackMethodOptions.Add(MakeShared<EUVPackMethod>(EUVPackMethod::Shapes));
	PackMethodOptions.Add(MakeShared<EUVPackMethod>(EUVPackMethod::Boxes));

	ChildSlot
	[
		VrrMenu::Stack({
			CreateGeneralSection(),
			CreateBalancedSection(),
			CreateGroupedSection(),
		})
	];
}

// LAYOUT
/**
 * A UV channel combo writing into *Value (VrrMenu::IntCombo). The options are re-read each time the list opens,
 * so channels added by Apply show up without a refresh. Target combos label the lightmap and new channels.
 */
TSharedRef<SWidget> SMenu_UVLayout::CreateChannelCombo(int32* Value, bool bTarget)
{
	return VrrMenu::IntCombo(
		[this, bTarget]() { return GetChannelOptions(bTarget); },
		Value,
		[this, bTarget](int32 Channel) { return GetChannelLabel(Channel, bTarget); });
}

static FText GetPackMethodLabel(EUVPackMethod Method)
{
	return Method == EUVPackMethod::Shapes ? LOCTEXT("PackShapes", "Shapes (tight)") : LOCTEXT("PackBoxes", "Boxes (fast)");
}

static FText GetPackMethodTip(EUVPackMethod Method)
{
	return Method == EUVPackMethod::Shapes
		? LOCTEXT("PackShapesTip", "Islands nest into each other's gaps and holes, trying all four quarter turns")
		: LOCTEXT("PackBoxesTip", "Bounding boxes on shelves");
}

/** Full width: the mesh, the source channel, and the options both layouts share (rotation, LODs). */
TSharedRef<SWidget> SMenu_UVLayout::CreateGeneralSection()
{
	return VrrMenu::HelpCard(LOCTEXT("GeneralHeader", "General Settings"),
		LOCTEXT("GeneralHelp", "Islands are read from the source channel, which is never changed. Every island is scaled by its 3D surface area so all islands get the same texel density, "
			"then packed without overlaps. Mirrored or stacked islands in the source get their own space, so the result is safe to bake into. "
			"These settings are used by both layouts below."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("MeshLabel", "Mesh"),
				VrrMenu::MeshPicker(VrrMenu::EMeshTypes::StaticOrSkeletal,
					[this]() { return Mesh.Get(); },
					[this](UObject* NewMesh) { SetMesh(NewMesh); },
					[this](const FText& Error) { SetStatus(Error, true); })),
			VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_UVLayout::GetMeshInfoText)),
			VrrMenu::Row(LOCTEXT("Source", "Source UVs"),
				CreateChannelCombo(&Settings.SourceChannel, false),
				LOCTEXT("SourceTip", "The channel whose islands (UV seams) are kept. Usually UV0, the texturing channel")),
			VrrMenu::Row(LOCTEXT("Rotate", "Rotation"),
				VrrMenu::CheckBox(&Settings.bRotateIslands, LOCTEXT("RotateIslands", "Rotate islands to fit"),
					LOCTEXT("RotateIslandsTip", "Turn each island to its tightest bounding box, lying flat, so they pack closer. Off keeps the source orientation"))),
			VrrMenu::Row(LOCTEXT("LODs", "LODs"),
				VrrMenu::CheckBox(&Settings.bAllLODs, LOCTEXT("AllLODs", "All LODs with source data"),
					LOCTEXT("AllLODsTip", "Also lay out imported LODs (each from its own islands). Generated LODs are rebuilt from LOD 0 and keep its UVs either way"))),
		}));
}

/**
 * Balanced UV layout: one region, even density. "As Lightmap UVs" (static meshes) replaces Unreal's generated
 * lightmap; otherwise it goes into its own target channel. Settings and button on the left, preview on the right.
 */
TSharedRef<SWidget> SMenu_UVLayout::CreateBalancedSection()
{
	auto IsLightmap = [this]() { return UVLayout::IsBalancedLightmap(Mesh.Get(), Settings); };

	TSharedRef<SWidget> SettingsColumn = VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("BalancedUse", "Use"),
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("BalancedAsLightmapTip", "Static meshes: write the layout as the lightmap UVs, replacing the ones Unreal generates on import (usually the last channel). "
				"Generate Lightmap UVs is turned off so later builds keep them, and the mesh's Light Map Resolution is set to the resolution below.\n"
				"Off (and on skeletal meshes, which have no lightmaps): write it into the target channel below, for custom materials"))
			.IsEnabled_Lambda([this]() { return UVLayout::SupportsLightmaps(Mesh.Get()); })
			.IsChecked_Lambda([this]() { return Settings.bBalancedAsLightmap && (!Mesh.IsValid() || UVLayout::SupportsLightmaps(Mesh.Get())) ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState State)
			{
				Settings.bBalancedAsLightmap = State == ECheckBoxState::Checked;
				ResetBalancedDefaults();
			})
			[
				SNew(STextBlock).Text(LOCTEXT("BalancedAsLightmap", "As Lightmap UVs"))
			]),
		VrrMenu::Row(LOCTEXT("BalancedTarget", "Target UVs"),
			CreateChannelCombo(&Settings.BalancedTargetChannel, true),
			LOCTEXT("BalancedTargetTip", "Where the balanced layout goes when it isn't the lightmap. A lightmap already there moves up one channel"),
			TAttribute<bool>::CreateLambda([IsLightmap]() { return !IsLightmap(); })),
		VrrMenu::Row(LOCTEXT("BalancedPadding", "Padding (px)"),
			VrrMenu::IntField(&Settings.BalancedPadding, 0, 64),
			LOCTEXT("BalancedPaddingTip", "Pixels between islands at the resolution below (lightmap texels when used as the lightmap; 2 is usually enough there)")),
		VrrMenu::Row(LOCTEXT("BalancedResolution", "Resolution"),
			VrrMenu::ResolutionCombo(&Settings.BalancedResolution, { 16, 32, 64, 128, 256, 512, 1024, 2048, 4096 }),
			LOCTEXT("BalancedResolutionTip", "The texture size the padding is measured in. As Lightmap UVs it is the mesh's Light Map Resolution (set on the mesh, starting from its current value)")),
		VrrMenu::Row(LOCTEXT("BalancedPacking", "Packing"),
			SNew(SComboBox<TSharedPtr<EUVPackMethod>>)
			.OptionsSource(&PackMethodOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<EUVPackMethod> Option)
			{
				return SNew(STextBlock).Text(GetPackMethodLabel(*Option)).ToolTipText(GetPackMethodTip(*Option));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<EUVPackMethod> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Settings.BalancedPackMethod = *Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetPackMethodLabel(Settings.BalancedPackMethod); })
			],
			LOCTEXT("BalancedPackingTip", "Shapes (tight): islands are drawn onto a grid, so they nest into each other's gaps and holes, and each tries all four quarter turns (with Rotation on). Uses the most space; slower on meshes with many islands.\n"
				"Boxes (fast): each island's bounding box is packed on shelves. Quick, but the space around irregular islands stays empty")),
		VrrMenu::Row(LOCTEXT("BalancedDetail", "Packing detail"),
			VrrMenu::ResolutionCombo(&Settings.BalancedPackDetail, { 128, 256, 512, 1024 }),
			LOCTEXT("BalancedDetailTip", "Shapes: grid cells across the square. Finer packs tighter but takes longer. Never finer than the resolution above, so lightmaps pack texel by texel"),
			TAttribute<bool>::CreateLambda([this]() { return Settings.BalancedPackMethod == EUVPackMethod::Shapes; })),
		VrrMenu::Row(LOCTEXT("BalancedFill", "Fill"),
			VrrMenu::CheckBox(&Settings.bBalancedStretchToFill, LOCTEXT("BalancedStretch", "Stretch to fill the square"),
				LOCTEXT("BalancedStretchTip", "After packing, scale U and V separately so the islands reach the edges, using the leftover strip along one side. "
					"Texel density is then a little higher along one axis; padding only grows"))),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			if (!Mesh.IsValid())
			{
				return FText::GetEmpty();
			}
			const FUVLayoutBalancedPlan Plan = UVLayout::PlanBalanced(Mesh.Get(), Settings);
			return Plan.Blocker.IsEmpty() ? Plan.Summary : Plan.Blocker;
		})),
		VrrMenu::ActionButton(LOCTEXT("CreateBalanced", "Create Balanced UV Layout"),
			LOCTEXT("CreateBalancedTip", "Write the balanced layout: as the lightmap UVs, or into the target channel. Undoable"),
			[this]() { return GetBalancedBlocker(); },
			FOnClicked::CreateSP(this, &SMenu_UVLayout::OnCreateBalancedClicked)),
		VrrMenu::StatusLine(&BalancedStatusText, &bBalancedStatusIsError),
	});

	return VrrMenu::HelpCard(LOCTEXT("BalancedHeader", "Balanced UV Layout"),
		LOCTEXT("BalancedHelp", "Every island from the source channel is scaled to its 3D size and packed evenly over the whole square, ignoring material slots. "
			"Use it as the lightmap on static meshes (replacing Unreal's generated lightmap UVs), or as a balanced channel for skinned meshes and custom materials."),
		VrrMenu::SettingsAndPreview(SettingsColumn, CreatePreviewPanel(true)));
}

/** Padding and resolution defaults for the current use: 2 texels at the mesh's Light Map Resolution as a lightmap, else 4 px at 1024. */
void SMenu_UVLayout::ResetBalancedDefaults()
{
	if (UVLayout::IsBalancedLightmap(Mesh.Get(), Settings))
	{
		const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh.Get());
		Settings.BalancedResolution = FMath::Max(StaticMesh->GetLightMapResolution(), 4);
		Settings.BalancedPadding = 2;
	}
	else
	{
		Settings.BalancedResolution = 1024;
		Settings.BalancedPadding = 4;
	}
}

/**
 * Grouped material layout: one region per material slot (columns first), for the Vrr UV Region material node.
 * Target channel, density and padding, and its button on the left; preview on the right.
 */
TSharedRef<SWidget> SMenu_UVLayout::CreateGroupedSection()
{
	TSharedRef<SWidget> SettingsColumn = VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Target", "Target UVs"),
			CreateChannelCombo(&Settings.TargetChannel, true),
			LOCTEXT("TargetTip", "Where the new layout goes. Usually UV1; a lightmap there moves to UV2")),
		VrrMenu::Row(LOCTEXT("GroupLayout", "Layout"),
			VrrMenu::CheckBox(&Settings.bGroupAsUDIMs, LOCTEXT("GroupAsUDIMs", "UDIM tiles (one per material)"),
				LOCTEXT("GroupAsUDIMsTip", "Give each material slot a whole 0-1 tile instead of a share of one square: slot 0 in 1001, slot 1 in 1002, ... up to 1010, then 1011 one row down. "
					"Every material gets the full resolution of its own texture set (UDIM textures, or one texture per material).\n"
					"Off: the atlas layout, with all materials sharing the 0-1 square in columns")),
			LOCTEXT("GroupAsUDIMsRowTip", "Atlas (shared square) or UDIM tiles")),
		VrrMenu::Row(LOCTEXT("Density", "Texel density"),
			VrrMenu::CheckBox(&Settings.bMatchDensityAcrossMaterials, LOCTEXT("MatchDensity", "Match across regions"),
				LOCTEXT("MatchDensityTip", "Use the same texel density in every region (the smallest one), so materials look equally sharp. Off: each region fills its space, so small materials get more texels")),
			FText::GetEmpty(), TAttribute<bool>::CreateLambda([this]() { return NumMaterialSlots > 1; })),
		VrrMenu::Row(LOCTEXT("Padding", "Padding (px)"),
			VrrMenu::IntField(&Settings.Padding, 0, 64),
			LOCTEXT("PaddingTip", "Pixels between islands at the resolution below, so baked textures can pad their islands without bleeding")),
		VrrMenu::Row(LOCTEXT("PaddingRes", "Resolution"),
			VrrMenu::ResolutionCombo(&Settings.PaddingResolution, { 256, 512, 1024, 2048, 4096 }),
			LOCTEXT("PaddingResTip", "The texture size you plan to bake at. Padding is measured in its pixels")),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			if (!Mesh.IsValid())
			{
				return FText::GetEmpty();
			}
			const FUVLayoutChannelPlan Plan = UVLayout::PlanChannels(Mesh.Get(), Settings);
			return Plan.Blocker.IsEmpty() ? Plan.Summary : Plan.Blocker;
		})),
		VrrMenu::ActionButton(LOCTEXT("Apply", "Create Grouped Material UV Layout"),
			LOCTEXT("ApplyTip", "Write the layout into the target channel. Undoable"),
			[this]() { return GetApplyBlocker(); },
			FOnClicked::CreateSP(this, &SMenu_UVLayout::OnApplyClicked)),
		VrrMenu::StatusLine(&StatusText, &bStatusIsError),
	});

	return VrrMenu::HelpCard(LOCTEXT("GroupedHeader", "Grouped Material UV Layout"),
		LOCTEXT("GroupedHelp", "Each material slot's islands get their own region, laid out in columns first: 2 slots two columns, 3 slots three columns, more add rows (4 = 2 + 2, 5 = 3 + 2). "
			"Regions fill the whole square in material slot order, so the Vrr UV Region material node can pick one by its number. "
			"As UDIM tiles, each slot gets a whole tile of its own instead (1001, 1002, ...). "
			"If the target is the lightmap channel, the lightmap moves up one channel first."),
		VrrMenu::SettingsAndPreview(SettingsColumn, CreatePreviewPanel(false)));
}

// BEHAVIOR
/** Switches to a new mesh: reads its channels and slots, picks UV0 -> UV1, and previews. */
void SMenu_UVLayout::SetMesh(UObject* NewMesh)
{
	Mesh = VrrCore::IsStaticOrSkeletalMesh(NewMesh) ? NewMesh : nullptr;
	NumUVChannels = Mesh.IsValid() ? VrrCore::GetNumUVChannels(Mesh.Get()) : 0;
	NumMaterialSlots = Mesh.IsValid() ? UVLayout::GetNumMaterialSlots(Mesh.Get()) : 0;
	StatusText = FText::GetEmpty();
	BalancedStatusText = FText::GetEmpty();

	Settings.SourceChannel = 0;
	Settings.TargetChannel = Mesh.IsValid() ? FMath::Min(1, NumUVChannels) : 1;
	Settings.BalancedTargetChannel = Settings.TargetChannel;
	ResetBalancedDefaults();

	// Both previews: laid out for a usable mesh, cleared otherwise
	UpdatePreviews();
}

/** Source: every existing channel. Target: every existing channel plus the next new one (within the mesh type's limit). */
TArray<int32> SMenu_UVLayout::GetChannelOptions(bool bTarget) const
{
	TArray<int32> Channels;
	const int32 Last = !Mesh.IsValid() ? -1
		: bTarget ? FMath::Min(NumUVChannels, UVLayout::GetMaxUVChannels(Mesh.Get()) - 1)
		: NumUVChannels - 1;
	for (int32 Channel = 0; Channel <= Last; ++Channel)
	{
		Channels.Add(Channel);
	}
	return Channels;
}

/** "UV1", "UV1 (lightmap)", "UV2 (new)". */
FText SMenu_UVLayout::GetChannelLabel(int32 Channel, bool bTarget) const
{
	const FText Name = FText::Format(LOCTEXT("ChannelName", "UV{0}"), Channel);
	if (Channel >= NumUVChannels)
	{
		return FText::Format(LOCTEXT("ChannelNew", "{0} (new)"), Name);
	}
	if (UVLayout::GetLightmapChannel(Mesh.Get()) == Channel)
	{
		return bTarget ? FText::Format(LOCTEXT("ChannelLightmapMoves", "{0} (lightmap, moves to UV{1})"), Name, Channel + 1)
			: FText::Format(LOCTEXT("ChannelLightmap", "{0} (lightmap)"), Name);
	}
	return Name;
}

/** Why Apply can't run yet (shown next to the button), or empty when it can. */
FText SMenu_UVLayout::GetApplyBlocker() const
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
		return LOCTEXT("NeedUVs", "This mesh has no UVs to take islands from.");
	}
	return UVLayout::PlanChannels(Mesh.Get(), Settings).Blocker;
}

/** "Static Mesh | N triangles | N UV channels | N material slots | lightmap UVn" or why the mesh can't be used. */
FText SMenu_UVLayout::GetMeshInfoText() const
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
	const int32 Lightmap = UVLayout::GetLightmapChannel(Mesh.Get());
	return FText::Format(LOCTEXT("MeshInfo", "{0}  |  {1} triangles  |  {2} UV channels  |  {3} material {3}|plural(one=slot,other=slots){4}"),
		VrrCore::IsSkeletalMesh(Mesh.Get()) ? LOCTEXT("Skeletal", "Skeletal Mesh") : LOCTEXT("Static", "Static Mesh"),
		MeshDescription->Triangles().Num(), NumUVChannels, NumMaterialSlots,
		Lightmap >= 0 ? FText::Format(LOCTEXT("MeshInfoLightmap", "  |  lightmap UV{0}"), Lightmap) : FText::GetEmpty());
}

/** Island count, regions and coverage of one preview. */
FText SMenu_UVLayout::GetPreviewInfoText(bool bBalanced) const
{
	const TSharedPtr<FUVLayoutPreview>& Preview = GetPreviewSlot(bBalanced).Data;
	if (!Preview.IsValid())
	{
		return Mesh.IsValid() ? LOCTEXT("NoPreview", "No preview yet.") : LOCTEXT("PreviewNeedsMesh", "Pick a mesh to see its layout.");
	}
	FText Info = bBalanced
		? FText::Format(LOCTEXT("BalancedPreviewInfo", "LOD 0: {0} islands, {1} of the square covered."), Preview->NumIslands, FText::AsPercent(Preview->Coverage))
		: Preview->bUDIM
		? FText::Format(LOCTEXT("PreviewInfoUDIM", "LOD 0: {0} islands in {1} UDIM {1}|plural(one=tile,other=tiles), {2} of each tile covered on average."),
			Preview->NumIslands, Preview->Regions.Num(), FText::AsPercent(Preview->Coverage))
		: FText::Format(LOCTEXT("PreviewInfo", "LOD 0: {0} islands in {1} {1}|plural(one=region,other=regions), {2} of the square covered."),
			Preview->NumIslands, Preview->Regions.Num(), FText::AsPercent(Preview->Coverage));
	if (!bBalanced && Preview->bUDIM)
	{
		Info = FText::Format(LOCTEXT("PreviewRegionNodeUDIM", "{0} In materials, the Vrr UV Region node (UDIM on) picks one: Region = the first number on the preview."), Info);
	}
	else if (!bBalanced && Preview->Regions.Num() > 1)
	{
		Info = FText::Format(LOCTEXT("PreviewRegionNode", "{0} In materials, the Vrr UV Region node picks one: Region Count {1}, Region = the number on the preview."),
			Info, Preview->Regions.Num());
	}
	if (Preview->NumProjected > 0)
	{
		Info = FText::Format(LOCTEXT("PreviewProjected", "{0} {1} islands had no UV area in the source and were laid flat from their 3D shape."), Info, Preview->NumProjected);
	}
	return Info;
}

/** Has any setting that shapes this preview changed since it was made? */
bool SMenu_UVLayout::IsPreviewStale(bool bBalanced) const
{
	const FPreviewSlot& Slot = GetPreviewSlot(bBalanced);
	if (!Slot.Data.IsValid())
	{
		return false;
	}
	const FUVLayoutSettings A = GetPreviewLayoutSettings(bBalanced);
	const FUVLayoutSettings& B = Slot.MadeWith;
	return A.SourceChannel != B.SourceChannel || A.bRotateIslands != B.bRotateIslands || A.bGroupByMaterial != B.bGroupByMaterial
		|| A.bMatchDensityAcrossMaterials != B.bMatchDensityAcrossMaterials || A.Padding != B.Padding || A.PaddingResolution != B.PaddingResolution
		|| A.bGroupAsUDIMs != B.bGroupAsUDIMs || A.PackMethod != B.PackMethod || A.PackDetail != B.PackDetail || A.bStretchToFill != B.bStretchToFill;
}

/** The settings a preview lays out with: the material-region layout, or the balanced one. */
FUVLayoutSettings SMenu_UVLayout::GetPreviewLayoutSettings(bool bBalanced) const
{
	return bBalanced ? UVLayout::MakeBalancedSettings(Settings) : Settings;
}

/** Lays out LOD 0 with the current settings and shows it in one preview. */
void SMenu_UVLayout::UpdatePreview(bool bBalanced)
{
	FPreviewSlot& Slot = GetPreviewSlot(bBalanced);
	TSharedPtr<FUVLayoutPreview> NewPreview = MakeShared<FUVLayoutPreview>();
	FText Error;
	const FUVLayoutSettings LayoutSettings = GetPreviewLayoutSettings(bBalanced);
	if (Mesh.IsValid() && NumUVChannels > 0 && UVLayout::ComputePreview(Mesh.Get(), LayoutSettings, *NewPreview, Error))
	{
		Slot.Data = NewPreview;
		Slot.MadeWith = LayoutSettings;
	}
	else
	{
		Slot.Data.Reset();
		if (!Error.IsEmpty())
		{
			SetStatus(Error, true);
		}
	}
	if (Slot.Widget.IsValid())
	{
		Slot.Widget->SetPreview(Slot.Data);
	}
}

/** Refreshes both previews. */
void SMenu_UVLayout::UpdatePreviews()
{
	UpdatePreview(false);
	UpdatePreview(true);
}

/** The preview widget, its info line, the Update button and the "Out of date" badge. */
TSharedRef<SWidget> SMenu_UVLayout::CreatePreviewPanel(bool bBalanced)
{
	// Fixed width so the info line wraps under the square instead of widening the column
	return SNew(SBox)
	.WidthOverride(PreviewSize)
	[
		VrrMenu::Rows({
			SNew(SBox)
			.WidthOverride(PreviewSize)
			.HeightOverride(PreviewSize)
			[
				SAssignNew(GetPreviewSlot(bBalanced).Widget, SUVLayoutPreview)
			],
			VrrMenu::HelpText(TAttribute<FText>::CreateLambda([this, bBalanced]() { return GetPreviewInfoText(bBalanced); })),
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("UpdatePreview", "Update Preview"))
				.ToolTipText(LOCTEXT("UpdatePreviewTip", "Lay out LOD 0 with the current settings, without changing the mesh"))
				.IsEnabled_Lambda([this]() { return Mesh.IsValid() && NumUVChannels > 0; })
				.OnClicked_Lambda([this, bBalanced]() { UpdatePreview(bBalanced); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				VrrMenu::Badge(TAttribute<FText>::CreateLambda([this, bBalanced]()
				{
					return IsPreviewStale(bBalanced) ? LOCTEXT("Stale", "Out of date") : FText::GetEmpty();
				}), VrrMenu::WarningColor())
			],
		})
	];
}

/** Picks the first Static or Skeletal Mesh selected in the Content Browser. */
/** Writes the layout, then re-reads the channel count (it may have grown) and refreshes the preview. */
FReply SMenu_UVLayout::OnApplyClicked()
{
	if (!GetApplyBlocker().IsEmpty())
	{
		return FReply::Handled();
	}

	const FUVLayoutResult Result = UVLayout::Apply(Mesh.Get(), Settings);
	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("ApplyDone", "UV layout created") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);

	if (Result.bSuccess)
	{
		const FText Status = StatusText;
		NumUVChannels = VrrCore::GetNumUVChannels(Mesh.Get());
		UpdatePreviews();
		SetStatus(Status, false);
	}
	return FReply::Handled();
}

/** Why Create Balanced UV Layout can't run yet, or empty when it can. */
FText SMenu_UVLayout::GetBalancedBlocker() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NeedMesh", "Pick a mesh first.");
	}
	if (NumUVChannels == 0)
	{
		return LOCTEXT("NeedUVs", "This mesh has no UVs to take islands from.");
	}
	return UVLayout::PlanBalanced(Mesh.Get(), Settings).Blocker;
}

/** Writes the balanced layout (as the lightmap or into its channel), then refreshes the channel list and the preview. */
FReply SMenu_UVLayout::OnCreateBalancedClicked()
{
	if (!GetBalancedBlocker().IsEmpty())
	{
		return FReply::Handled();
	}

	const bool bAsLightmap = UVLayout::IsBalancedLightmap(Mesh.Get(), Settings);
	const FUVLayoutResult Result = UVLayout::ApplyBalanced(Mesh.Get(), Settings);
	BalancedStatusText = Result.Message;
	bBalancedStatusIsError = !Result.bSuccess;
	VrrMenu::Notify(Result.bSuccess ? (bAsLightmap ? LOCTEXT("BalancedLightmapDone", "Balanced lightmap UVs created") : LOCTEXT("BalancedDone", "Balanced UV layout created")) : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);

	if (Result.bSuccess)
	{
		NumUVChannels = VrrCore::GetNumUVChannels(Mesh.Get());
		UpdatePreviews();
	}
	return FReply::Handled();
}

/** Text (and error color) for the status line under the Apply button. */
void SMenu_UVLayout::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

#undef LOCTEXT_NAMESPACE
