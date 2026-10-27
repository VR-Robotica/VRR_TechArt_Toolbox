// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Edge Mesh page (UI only).
 *
 * The page edits an FEdgeMeshSettings, previews which edges those settings pick (kept edges bright, dropped ones
 * dim, over a faint wireframe of the mesh), and hands the settings to EdgeMeshBuilder::Build (Core_EdgeMesh.cpp),
 * which writes the edge mesh asset.
 *
 * Cards: [Edges (which edges, strip shape)] [Preview], then Output (material, where it goes, the button).
 */

#include "Menu_EdgeMesh.h"

#include "Core_Common.h"
#include "Engine/StaticMesh.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "MenuWidgets.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SLeafWidget.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_EdgeMesh"

/**
 * Draws the edges a preview found, projected from one of four views: a faint wireframe of the whole mesh, the
 * dropped edges dim red, the kept edges bright. Lines are thin quads in one custom-vertex draw per kind, so dense
 * meshes stay fast.
 */
class SEdgeMeshPreview : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SEdgeMeshPreview) {}
	SLATE_END_ARGS()

	void Construct(const FArguments&) {}

	void SetPreview(const TSharedPtr<FEdgeMeshPreview>& InPreview, int32 InView)
	{
		Preview = InPreview;
		View = InView;
		Invalidate(EInvalidateWidgetReason::Paint);
	}

	virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(320.0f, 320.0f); }

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry, const FSlateRect& CullingRect, FSlateWindowElementList& OutDrawElements,
		int32 LayerId, const FWidgetStyle& WidgetStyle, bool bParentEnabled) const override
	{
		const FVector2f Size = FVector2f(Geometry.GetLocalSize());
		const float Side = FMath::Min(Size.X, Size.Y);
		const FSlateBrush* White = FAppStyle::GetBrush("WhiteBrush");

		// 1. Backdrop
		FSlateDrawElement::MakeBox(OutDrawElements, LayerId, Geometry.ToPaintGeometry(FVector2f(Side), FSlateLayoutTransform()),
			White, ESlateDrawEffect::None, FLinearColor(0.012f, 0.012f, 0.014f, 1.0f));
		if (!Preview.IsValid() || Preview->IsEmpty())
		{
			return LayerId + 1;
		}

		// 2. Fit the projected bounds into the square (8 corners of the 3D box), with a margin
		FVector3f Right, Up;
		GetViewAxes(View, Right, Up);
		auto Project = [&Right, &Up](const FVector3f& P) { return FVector2f(FVector3f::DotProduct(P, Right), -FVector3f::DotProduct(P, Up)); };
		FBox2f Fit(ForceInit);
		for (int32 Corner = 0; Corner < 8; ++Corner)
		{
			const FVector3f P((Corner & 1) ? Preview->Bounds.Max.X : Preview->Bounds.Min.X,
				(Corner & 2) ? Preview->Bounds.Max.Y : Preview->Bounds.Min.Y,
				(Corner & 4) ? Preview->Bounds.Max.Z : Preview->Bounds.Min.Z);
			Fit += Project(P);
		}
		const float Margin = 12.0f;
		const FVector2f Extent = Fit.GetSize();
		const float Zoom = (Side - 2.0f * Margin) / FMath::Max3(Extent.X, Extent.Y, 0.001f);
		const FVector2f Center = Fit.GetCenter();
		auto ToLocal = [&](const FVector3f& P) { return (Project(P) - Center) * Zoom + FVector2f(Side * 0.5f); };

		// 3. Line batches: wireframe, dropped, kept (drawn in that order so kept edges sit on top)
		DrawLines(OutDrawElements, LayerId + 1, Geometry, Preview->Wireframe, ToLocal, FLinearColor(1.0f, 1.0f, 1.0f, 0.10f), 1.0f);
		DrawLines(OutDrawElements, LayerId + 2, Geometry, Preview->Dropped, ToLocal, FLinearColor(0.85f, 0.25f, 0.2f, 0.6f), 1.0f);
		DrawLines(OutDrawElements, LayerId + 3, Geometry, Preview->Kept, ToLocal, FLinearColor(1.0f, 0.65f, 0.15f, 1.0f), 2.0f);
		return LayerId + 3;
	}

	/** Screen axes of each view: 0 three-quarter (from front-right, above), 1 front, 2 side, 3 top. */
	static void GetViewAxes(int32 InView, FVector3f& OutRight, FVector3f& OutUp)
	{
		switch (InView)
		{
		case 1: OutRight = FVector3f(0.0f, 1.0f, 0.0f); OutUp = FVector3f(0.0f, 0.0f, 1.0f); break;	// front: looking along +X
		case 2: OutRight = FVector3f(-1.0f, 0.0f, 0.0f); OutUp = FVector3f(0.0f, 0.0f, 1.0f); break;	// side: looking along +Y
		case 3: OutRight = FVector3f(0.0f, 1.0f, 0.0f); OutUp = FVector3f(1.0f, 0.0f, 0.0f); break;	// top: looking down
		default:
			// Yaw 45 degrees, pitch 30 degrees down: Right is level, Up = Right x ViewDirection
			OutRight = FVector3f(0.7071f, 0.7071f, 0.0f);
			OutUp = FVector3f(-0.3536f, 0.3536f, 0.8660f);
			break;
		}
	}

private:
	TSharedPtr<FEdgeMeshPreview> Preview;
	int32 View = 0;

	/** Segments (2 points each) as quads Thickness pixels wide, in one draw. */
	template <typename FToLocal>
	static void DrawLines(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const TArray<FVector3f>& Points,
		const FToLocal& ToLocal, const FLinearColor& Color, float Thickness)
	{
		if (Points.Num() < 2)
		{
			return;
		}
		const FSlateResourceHandle Handle = FSlateApplication::Get().GetRenderer()->GetResourceHandle(*FAppStyle::GetBrush("WhiteBrush"));
		const FSlateRenderTransform& Transform = Geometry.GetAccumulatedRenderTransform();
		const FColor VertexColor = Color.ToFColor(true);
		TArray<FSlateVertex> Vertices;
		TArray<SlateIndex> Indices;
		Vertices.Reserve(Points.Num() * 2);
		Indices.Reserve(Points.Num() * 3);
		for (int32 i = 0; i + 1 < Points.Num(); i += 2)
		{
			const FVector2f A = ToLocal(Points[i]);
			const FVector2f B = ToLocal(Points[i + 1]);
			const FVector2f Along = (B - A).GetSafeNormal();
			const FVector2f Offset = FVector2f(-Along.Y, Along.X) * (Thickness * 0.5f);
			const SlateIndex First = static_cast<SlateIndex>(Vertices.Num());
			for (const FVector2f& P : { A - Offset, A + Offset, B + Offset, B - Offset })
			{
				Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform, P, FVector2f::ZeroVector, VertexColor));
			}
			Indices.Append({ First, First + 1, First + 2, First, First + 2, First + 3 });
		}
		FSlateDrawElement::MakeCustomVerts(OutDrawElements, Layer, Handle, Vertices, Indices, nullptr, 0, 0);
	}
};

/** Builds the page: [Edges] [Preview] side by side, then Output. */
void SMenu_EdgeMesh::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		VrrMenu::Stack({
			VrrMenu::CardRow(CreateEdgesCard(), CreatePreviewCard()),
			CreateOutputCard(),
		})
	];
}

// LAYOUT
/** The mesh, which edges count (angle, outer only, length, corners) and the strip shape (width, offset, tiling). */
TSharedRef<SWidget> SMenu_EdgeMesh::CreateEdgesCard()
{
	return VrrMenu::HelpCard(LOCTEXT("EdgesHeader", "Edges"),
		LOCTEXT("EdgesHelp", "Finds the mesh's creases (where two faces meet at Min Angle or more, the same edges Bake Mesh Maps puts in its Edge mask), joins them into chains, "
			"and drops chains shorter than Min Length, so small details go and the long outer edges stay. Each kept chain becomes a thin strip folded over the edge, "
			"Strip Width onto each face. Give the result a masked or translucent edge-wear material and place it at the same transform as the mesh."),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("MeshLabel", "Static Mesh"),
				VrrMenu::MeshPicker(VrrMenu::EMeshTypes::StaticOnly,
					[this]() { return Mesh.Get(); },
					[this](UObject* NewMesh) { SetMesh(NewMesh); },
					[this](const FText& Error) { StatusText = Error; bStatusIsError = true; })),
			VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
			{
				const FMeshDescription* Description = Mesh.IsValid() ? VrrCore::GetMeshDescription(Mesh.Get(), 0) : nullptr;
				return !Mesh.IsValid() ? LOCTEXT("PickMesh", "Pick a Static Mesh.")
					: !Description ? LOCTEXT("NoMeshData", "This mesh has no source mesh data for LOD 0.")
					: FText::Format(LOCTEXT("MeshInfo", "{0} triangles"), Description->Triangles().Num());
			})),
			VrrMenu::Row(LOCTEXT("MinAngle", "Min angle"),
				VrrMenu::FloatField(&Settings.MinAngle, 1.0f, 179.0f, 1.0f),
				LOCTEXT("MinAngleTip", "Degrees between two faces for their edge to count. Higher keeps only hard corners; lower also takes soft bevels")),
			VrrMenu::Row(LOCTEXT("Outer", "Edges"),
				VrrMenu::CheckBox(&Settings.bConvexOnly, LOCTEXT("ConvexOnly", "Outer edges only"),
					LOCTEXT("ConvexOnlyTip", "Only convex (outward) edges, where wear happens. Off also takes inner (valley) creases"))),
			VrrMenu::Row(LOCTEXT("MinLength", "Min length (cm)"),
				VrrMenu::FloatField(&Settings.MinLength, 0.0f, 10000.0f, 1.0f),
				LOCTEXT("MinLengthTip", "Edge chains shorter than this are dropped: raise it to keep only the big outer edges")),
			VrrMenu::Row(LOCTEXT("MaxTurn", "Corner break"),
				VrrMenu::FloatField(&Settings.MaxTurnAngle, 5.0f, 180.0f, 1.0f),
				LOCTEXT("MaxTurnTip", "Degrees: an edge chain ends where it turns sharper than this (e.g. a box corner), so strips don't twist around corners. "
					"A chain's length (for Min Length) is measured between these breaks")),
			VrrMenu::Row(LOCTEXT("Width", "Strip width (cm)"),
				VrrMenu::FloatField(&Settings.Width, 0.05f, 100.0f, 0.1f),
				LOCTEXT("WidthTip", "How far the strip reaches onto each face from the edge (the strip is twice this across)")),
			VrrMenu::Row(LOCTEXT("Offset", "Surface offset (cm)"),
				VrrMenu::FloatField(&Settings.SurfaceOffset, 0.0f, 5.0f, 0.01f),
				LOCTEXT("OffsetTip", "How far the strip floats above the surface, so it never flickers through it. Raise it if it z-fights at a distance")),
			VrrMenu::Row(LOCTEXT("UTile", "U tile length (cm)"),
				VrrMenu::FloatField(&Settings.UTileLength, 0.0f, 1000.0f, 0.5f),
				LOCTEXT("UTileTip", "Centimeters of edge per repeat of the decal texture along the edge. 0 = twice the strip width, so texels stay square")),
		}));
}

/** The preview square, its info line, the view picker, Update and the "Out of date" badge. */
TSharedRef<SWidget> SMenu_EdgeMesh::CreatePreviewCard()
{
	return VrrMenu::Card(LOCTEXT("PreviewHeader", "Preview"),
		SNew(SBox)
		.WidthOverride(PreviewSize)
		[
			VrrMenu::Rows({
				SNew(SBox)
				.WidthOverride(PreviewSize)
				.HeightOverride(PreviewSize)
				[
					SAssignNew(PreviewWidget, SEdgeMeshPreview)
				],
				VrrMenu::HelpText(TAttribute<FText>::CreateSP(this, &SMenu_EdgeMesh::GetPreviewInfoText)),
				VrrMenu::IntCombo(
					[]() { return TArray<int32>{ 0, 1, 2, 3 }; },
					&View,
					[](int32 Option)
					{
						return Option == 1 ? LOCTEXT("ViewFront", "Front") : Option == 2 ? LOCTEXT("ViewSide", "Side")
							: Option == 3 ? LOCTEXT("ViewTop", "Top") : LOCTEXT("View34", "Three-quarter");
					},
					[this](int32) { PreviewWidget->SetPreview(Preview, View); }),
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()
				[
					SNew(SButton)
					.Text(LOCTEXT("UpdatePreview", "Update Preview"))
					.ToolTipText(LOCTEXT("UpdatePreviewTip", "Find the edges with the current settings, without building anything"))
					.IsEnabled_Lambda([this]() { return Mesh.IsValid(); })
					.OnClicked_Lambda([this]() { UpdatePreview(); return FReply::Handled(); })
				]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					VrrMenu::Badge(TAttribute<FText>::CreateLambda([this]()
					{
						return IsPreviewStale() ? LOCTEXT("Stale", "Out of date") : FText::GetEmpty();
					}), VrrMenu::WarningColor())
				],
			})
		]);
}

/** The decal material, where the mesh goes, and the Build button. */
TSharedRef<SWidget> SMenu_EdgeMesh::CreateOutputCard()
{
	return VrrMenu::Card(LOCTEXT("OutputHeader", "Output"),
		VrrMenu::Rows({
			VrrMenu::Row(LOCTEXT("Material", "Material"),
				VrrMenu::AssetPicker(UMaterialInterface::StaticClass(),
					[](const FAssetData& Asset) { return Asset.IsInstanceOf(UMaterialInterface::StaticClass()); },
					[this]() -> UObject* { return Settings.Material.Get(); },
					[this](UObject* Picked) { Settings.Material = Cast<UMaterialInterface>(Picked); },
					LOCTEXT("UseSelectedMaterialTip", "Use the material selected in the Content Browser"),
					LOCTEXT("NoMaterialSelected", "Select a Material or Material Instance in the Content Browser first."),
					[this](const FText& Error) { StatusText = Error; bStatusIsError = true; }),
				LOCTEXT("MaterialTip", "Put on the edge mesh's only slot. Usually a masked or translucent edge-wear decal; empty leaves the engine default")),
			VrrMenu::Row(LOCTEXT("Folder", "Folder"),
				VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { StatusText = Error; bStatusIsError = true; })),
			VrrMenu::Row(LOCTEXT("Name", "Name"),
				VrrMenu::NameField(&Settings.OutputName)),
			VrrMenu::Row(LOCTEXT("Overwrite", "Existing"),
				VrrMenu::CheckBox(&Settings.bOverwrite, LOCTEXT("OverwriteLabel", "Overwrite"),
					LOCTEXT("OverwriteTip", "Rebuild a mesh that already has this name in place, so actors and Blueprints using it update. Off: a new name (_01, _02...)"))),
			VrrMenu::ActionButton(LOCTEXT("Build", "Build Edge Mesh"),
				LOCTEXT("BuildTip", "Build the strips into a Static Mesh with the source's pivot (not saved)"),
				[this]() { return GetBuildBlocker(); },
				FOnClicked::CreateSP(this, &SMenu_EdgeMesh::OnBuildClicked)),
			VrrMenu::StatusLine(&StatusText, &bStatusIsError),
		}));
}

// BEHAVIOR
/** Switches to a new mesh: output next to it as SM_<Base>_Edges, and a fresh preview. */
void SMenu_EdgeMesh::SetMesh(UObject* NewMesh)
{
	Mesh = Cast<UStaticMesh>(NewMesh);
	StatusText = FText::GetEmpty();
	if (Mesh.IsValid())
	{
		Settings.OutputFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		Settings.OutputName = EdgeMeshBuilder::SuggestName(Mesh.Get());
	}
	UpdatePreview();
}

/** Finds the edges with the current settings and shows them; clears the preview when it can't. */
void SMenu_EdgeMesh::UpdatePreview()
{
	TSharedPtr<FEdgeMeshPreview> NewPreview = MakeShared<FEdgeMeshPreview>();
	FText Error;
	if (Mesh.IsValid() && EdgeMeshBuilder::ComputePreview(Mesh.Get(), Settings, *NewPreview, Error))
	{
		Preview = NewPreview;
		PreviewMadeWith = Settings;
	}
	else
	{
		Preview.Reset();
		if (!Error.IsEmpty())
		{
			StatusText = Error;
			bStatusIsError = true;
		}
	}
	if (PreviewWidget.IsValid())
	{
		PreviewWidget->SetPreview(Preview, View);
	}
}

/** Only the settings that change which edges are found (strip shape doesn't show in the preview). */
bool SMenu_EdgeMesh::IsPreviewStale() const
{
	if (!Preview.IsValid())
	{
		return false;
	}
	const FEdgeMeshSettings& A = Settings;
	const FEdgeMeshSettings& B = PreviewMadeWith;
	return A.MinAngle != B.MinAngle || A.bConvexOnly != B.bConvexOnly || A.MinLength != B.MinLength || A.MaxTurnAngle != B.MaxTurnAngle;
}

/** "12 of 40 edge chains kept (3.2 m), 1,280 strip triangles." */
FText SMenu_EdgeMesh::GetPreviewInfoText() const
{
	if (!Preview.IsValid())
	{
		return Mesh.IsValid() ? LOCTEXT("NoPreview", "No preview yet.") : LOCTEXT("PreviewNeedsMesh", "Pick a mesh to see its edges.");
	}
	if (Preview->NumChains == 0)
	{
		return LOCTEXT("PreviewNoEdges", "No edges are this sharp. Lower Min Angle.");
	}
	FNumberFormattingOptions OneDecimal;
	OneDecimal.SetMaximumFractionalDigits(1);
	return FText::Format(LOCTEXT("PreviewInfo", "{0} of {1} edge chains kept (orange, {2} m), {3} strip triangles. Dropped chains are red."),
		Preview->NumKeptChains, Preview->NumChains, FText::AsNumber(Preview->KeptLength / 100.0f, &OneDecimal), Preview->NumTriangles);
}

/** Why Build can't run yet, or empty when it can. */
FText SMenu_EdgeMesh::GetBuildBlocker() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NeedMesh", "Pick a Static Mesh first.");
	}
	if (!VrrCore::GetMeshDescription(Mesh.Get(), 0))
	{
		return LOCTEXT("NeedMeshData", "This mesh has no LOD 0 source data.");
	}
	if (Settings.OutputName.IsEmpty())
	{
		return LOCTEXT("NeedName", "Enter a name for the edge mesh.");
	}
	return FText::GetEmpty();
}

/** Builds the mesh, reports, and shows it in the Content Browser. */
FReply SMenu_EdgeMesh::OnBuildClicked()
{
	const FEdgeMeshResult Result = EdgeMeshBuilder::Build(Mesh.Get(), Settings);
	StatusText = Result.Message;
	bStatusIsError = !Result.bSuccess;
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("BuildDone", "Edge mesh built") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);
	if (Result.bSuccess && Result.Mesh)
	{
		VrrCore::ShowInContentBrowser({ Result.Mesh });
	}
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
