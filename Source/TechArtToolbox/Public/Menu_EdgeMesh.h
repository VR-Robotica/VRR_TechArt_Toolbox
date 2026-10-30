// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core_EdgeMesh.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class SEdgeMeshPreview;
class UStaticMesh;

/**
 * Toolbox "Edge Mesh" page: builds strips along a Static Mesh's sharp outer edges, as their own mesh for decal
 * materials. The building itself is in EdgeMeshBuilder (Core_EdgeMesh.h).
 */
class SMenu_EdgeMesh : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_EdgeMesh) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<UStaticMesh> Mesh;
	FEdgeMeshSettings Settings;

	// Preview: the creases found, the settings they were found with, its widget and the view direction
	TSharedPtr<FEdgeMeshPreview> Preview;
	FEdgeMeshSettings PreviewMadeWith;
	TSharedPtr<SEdgeMeshPreview> PreviewWidget;
	int32 View = 0;

	FText StatusText;
	bool bStatusIsError = false;

	/** Side of the preview square. */
	static constexpr float PreviewSize = 320.0f;

	// Layout: [Edges card] [Preview card], then the Output card
	TSharedRef<SWidget> CreateEdgesCard();
	TSharedRef<SWidget> CreatePreviewCard();
	TSharedRef<SWidget> CreateOutputCard();

	// Behavior
	void SetMesh(UObject* NewMesh);
	void UpdatePreview();
	bool IsPreviewStale() const;
	FText GetPreviewInfoText() const;
	FText GetBuildBlocker() const;
	FReply OnBuildClicked();
};
