// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_ControlRig.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class USkeletalMesh;

/**
 * Toolbox "Rig" page: generates a Modular Control Rig for a humanoid Skeletal Mesh, optionally following a
 * Blender rig description. The generator itself is in ControlRigGenerator (Core_ControlRig.h).
 */
class SMenu_ControlRig : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMenu_ControlRig) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<USkeletalMesh> Mesh;
	FRigBoneMap Bones;

	/** Optional .rig.json from vrr_rig_exporter.py (Blender). When set, the bone map follows the Blender rig. */
	FString RigDescriptionPath;
	bool bRigDescriptionAuto = false;	// found next to the mesh, not picked by hand

	FControlRigGenSettings Settings;
	bool bOpenWhenDone = true;

	FText StatusText;
	bool bStatusIsError = false;

	// Layout
	TSharedRef<SWidget> CreateMeshSection();
	TSharedRef<SWidget> CreateBonesSection();
	TSharedRef<SWidget> CreateOptionsSection();
	TSharedRef<SWidget> CreateOutputSection();
	TSharedRef<SWidget> CreateGenerateSection();

	// Behavior
	void SetMesh(USkeletalMesh* NewMesh);
	void SetRigDescription(const FString& Path, bool bAuto);
	void RefreshBones();
	/** How many body parts of the bone map are missing bones (the warning rows of Detected Bones). */
	int32 CountToCheck() const;
	FReply OnBrowseRigDescriptionClicked();
	FReply OnGenerateClicked();
	void SetStatus(const FText& Text, bool bError);
};
