// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_BakeMaps.h"
#include "Core_ConsolidateMaterials.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

/**
 * Toolbox "Mesh Maps" page: bakes Cavity, Edge, Curvature and Ambient Occlusion maps from a mesh into textures.
 * The baking itself is in MeshMapBaker (Core_BakeMaps.h).
 */
class SMenu_BakeMaps : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_BakeMaps) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	// Mesh (a UStaticMesh or USkeletalMesh)
	TWeakObjectPtr<UObject> Mesh;
	int32 NumUVChannels = 0;

	FMeshMapBakeSettings Settings;
	/** The mesh's material slots grouped by shading model, for the per-shading-model texture names. */
	TArray<FConsolidateGroup> ShadingModelGroups;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateMapsSection();
	TSharedRef<SWidget> CreateOutputSection();
	TSharedRef<SWidget> CreateBakeSection();

	// Behavior
	void SetMesh(UObject* NewMesh);
	/** Why Bake can't run yet, or empty when it can. */
	FText GetBakeBlocker() const;
	FText GetMeshInfoText() const;
	FReply OnBakeClicked();
	void SetStatus(const FText& Text, bool bError);
};
