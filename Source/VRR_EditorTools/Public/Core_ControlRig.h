// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"

class UControlRigBlueprint;
class USkeletalMesh;

enum class ERigSide : uint8 { Center, Left, Right };

/** Bones found for one side's limbs. NAME_None = not found. */
struct FRigLimbBones
{
	FName Clavicle, UpperArm, LowerArm, Hand;
	FName Thigh, Calf, Foot, Ball;
	TArray<FName> ArmTwist, LegTwist;
	TArray<TPair<FString, TArray<FName>>> Fingers;	// finger name -> chain (root to tip)
};

/** What the generator found in the skeleton. */
struct FRigBoneMap
{
	FName Root, Pelvis, Head;
	TArray<FName> Spine;	// pelvis (exclusive) to neck (exclusive)
	TArray<FName> Neck;
	FRigLimbBones Left, Right;
	TArray<FName> Face;		// below the head, in hierarchy order
	TArray<FName> Other;	// everything else worth a control, in hierarchy order
	TArray<FString> Info;	// notes from the rig description
	TArray<FText> Warnings;

	bool CanGenerate() const { return !Pelvis.IsNone(); }
};

struct FControlRigGenSettings
{
	bool bFingers = true;	// a Finger module per finger
	bool bFace = true;		// FK controls for the bones under the head
	bool bOther = true;		// FK controls for every remaining bone
	FString OutputFolder;
	FString OutputName;
};

struct FControlRigGenResult
{
	bool bSuccess = false;
	/** Summary (and anything that needs a look), or the error. */
	FText Message;
	/** Every module and connector, plus problems. Also written to the log (LogVrrRigGenerator). */
	FString Report;
	/** The new, saved rig. */
	UControlRigBlueprint* Blueprint = nullptr;
};

/**
 * Generates a standard Modular Control Rig for a humanoid Skeletal Mesh, built from Epic's rig modules
 * (Spine, Neck, Shoulder, Arm, Finger, Leg, Foot, Chain). Body parts are found from bone names and the hierarchy,
 * so UE-style (upperarm_l) and prefixed (L_upperarm) skeletons both work.
 * A rig description from Python/Blender/vrr_rig_exporter.py replaces the name guesses with the Blender rig's own
 * structure: limbs and fingers from its IK chains, extra controls only where the Blender rig has one.
 */
namespace ControlRigGenerator
{
	/** Finds the body parts. With a rig description path (.rig.json), the Blender rig decides; failures become warnings. */
	FRigBoneMap MapBones(const USkeletalMesh* Mesh, const FString& RigDescriptionPath = FString());

	/** The one .rig.json in the mesh's folder (or one folder up), as a full file path. Empty if none or several. */
	FString FindRigDescription(const USkeletalMesh* Mesh);

	/** CR_<mesh name without SKM_/SK_> */
	FString SuggestName(const USkeletalMesh* Mesh);

	/** One line per body part, then rig description notes and warnings. */
	FString DescribeBones(const FRigBoneMap& Bones);

	/** Creates, connects and saves the rig (a free name: existing assets are never overwritten). */
	FControlRigGenResult Generate(USkeletalMesh* Mesh, const FRigBoneMap& Bones, const FControlRigGenSettings& Settings);
}
