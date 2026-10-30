// Copyright VR Robotica. All Rights Reserved.

/*
 * Control Rig generator: builds a UE 5.8 Modular Control Rig for a skeletal mesh, without hand wiring.
 *
 * Strategy
 *  A. Map the bones (FRigBoneMap): which bones are the pelvis, spine, neck, head, and per side the arm,
 *     leg, twist and finger chains. Two sources:
 *       - MapBones: bone-name rules ("upperarm_l", "L_UpperArm", "LeftUpperArm"... plus the hierarchy).
 *       - ApplyRigDescription (optional): a .rig.json exported from the Blender rig (vrr_rig_exporter.py).
 *         The Blender IK chains say exactly which bones are limbs, and its controls say which extra bones
 *         an animator actually poses. It overrides the name rules wherever it knows better.
 *     The page shows the map (DescribeBones) so the user can check it before generating.
 *  B. Generate: plan one module per body part (Root > Spine > Neck / Shoulder > Arm > Finger / Leg > Foot,
 *     plus a one-bone Chain module for each face/other bone), then create the rig asset and for each module:
 *       - plug its primary connector into a socket of its parent module (decides where it lives),
 *       - connect the bone connectors we already know (Shoulder Bone, Elbow Bone, ...),
 *       - let the module's own rules auto-connect the rest (pole vectors, space parents).
 *     Face/other chains then get their Parent Control pointed at the closest body control (e.g. the head),
 *     and everything that is connected (or still open) is written to the report and the Output Log.
 */

#include "Core_ControlRig.h"

#include "Algo/AllOf.h"
#include "Algo/AnyOf.h"
#include "Animation/Skeleton.h"
#include "ControlRigBlueprintFactory.h"
#include "ControlRigBlueprintLegacy.h"
#include "Core_Common.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "FileHelpers.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "ModularRig.h"
#include "ModularRigController.h"
#include "ModularRigModel.h"
#include "ModularRigRuleManager.h"
#include "Rigs/RigHierarchy.h"
#include "Rigs/RigHierarchyController.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

#define LOCTEXT_NAMESPACE "ControlRigGenerator"

DEFINE_LOG_CATEGORY_STATIC(LogVrrRigGenerator, Log, All);

namespace ControlRigGen
{
	using ESide = ERigSide;

	/** One skeleton bone with what the name rules learned about it. */
	struct FBone
	{
		FName Name;
		int32 Parent = INDEX_NONE;
		FString Base;		// lower case, side marker removed
		ESide Side = ESide::Center;
		bool bEnd = false;		// leaf marker bones (..._end)
		bool bTwist = false;
		bool bHelper = false;	// IK_ helper bones: driven by animation, never controlled
	};

	/** Splits "upperarm_l", "L_upperarm", "upperarm.L" or "LeftUpperArm" into a side and a base name. */
	void ParseName(const FString& Name, FString& OutBase, ESide& OutSide)
	{
		FString Lower = Name.ToLower();
		OutSide = ESide::Center;

		if (Lower.StartsWith(TEXT("l_")))			{ OutSide = ESide::Left;  Lower.RightChopInline(2); }
		else if (Lower.StartsWith(TEXT("r_")))		{ OutSide = ESide::Right; Lower.RightChopInline(2); }
		else if (Lower.EndsWith(TEXT("_l")) || Lower.EndsWith(TEXT(".l")))	{ OutSide = ESide::Left;  Lower.LeftChopInline(2); }
		else if (Lower.EndsWith(TEXT("_r")) || Lower.EndsWith(TEXT(".r")))	{ OutSide = ESide::Right; Lower.LeftChopInline(2); }
		else if (Lower.StartsWith(TEXT("left")))	{ OutSide = ESide::Left;  Lower.RightChopInline(4); }
		else if (Lower.StartsWith(TEXT("right")))	{ OutSide = ESide::Right; Lower.RightChopInline(5); }

		Lower.RemoveFromStart(TEXT("_"));
		OutBase = Lower;
	}

	/** The reference skeleton parsed once into FBones, with name lookups and hierarchy queries. */
	struct FSkeletonInfo
	{
		TArray<FBone> Bones;
		TMap<FName, int32> IndexOf;

		explicit FSkeletonInfo(const FReferenceSkeleton& RefSkeleton)
		{
			for (int32 Index = 0; Index < RefSkeleton.GetRawBoneNum(); ++Index)
			{
				FBone& Bone = Bones.AddDefaulted_GetRef();
				Bone.Name = RefSkeleton.GetBoneName(Index);
				Bone.Parent = RefSkeleton.GetParentIndex(Index);
				ParseName(Bone.Name.ToString(), Bone.Base, Bone.Side);
				const FString Lower = Bone.Name.ToString().ToLower();
				Bone.bEnd = Lower.EndsWith(TEXT("_end")) || Lower.EndsWith(TEXT("end_l")) || Lower.EndsWith(TEXT("end_r"));
				Bone.bTwist = Lower.Contains(TEXT("twist"));
				Bone.bHelper = Lower.StartsWith(TEXT("ik_"));
				IndexOf.Add(Bone.Name, Index);
			}
		}

		/** Bone by name, or null. */
		const FBone* Get(FName Name) const
		{
			const int32* Index = IndexOf.Find(Name);
			return Index ? &Bones[*Index] : nullptr;
		}

		/** True when Ancestor is somewhere above Child (not Child itself). */
		bool IsDescendant(FName Child, FName Ancestor) const
		{
			const int32* ChildIndex = IndexOf.Find(Child);
			const int32* AncestorIndex = IndexOf.Find(Ancestor);
			if (!ChildIndex || !AncestorIndex)
			{
				return false;
			}
			for (int32 Index = Bones[*ChildIndex].Parent; Index != INDEX_NONE; Index = Bones[Index].Parent)
			{
				if (Index == *AncestorIndex)
				{
					return true;
				}
			}
			return false;
		}

		/** Number of bones from Name up to the root, for sorting chains top-down. */
		int32 Depth(FName Name) const
		{
			int32 Result = 0;
			for (int32 Index = IndexOf.FindRef(Name); Index != INDEX_NONE; Index = Bones[Index].Parent)
			{
				++Result;
			}
			return Result;
		}

		/** First regular bone on this side whose base name is one of the candidates. */
		FName Find(std::initializer_list<const TCHAR*> Candidates, ESide Side, FName Under = NAME_None) const
		{
			for (const TCHAR* Candidate : Candidates)
			{
				for (const FBone& Bone : Bones)
				{
					if (!Bone.bEnd && !Bone.bTwist && !Bone.bHelper && Bone.Side == Side && Bone.Base == Candidate
						&& (Under.IsNone() || IsDescendant(Bone.Name, Under)))
					{
						return Bone.Name;
					}
				}
			}
			return NAME_None;
		}
	};

	/** "_L", "_R" or "" for module names. */
	FString SideSuffix(ESide Side)
	{
		return Side == ESide::Left ? TEXT("_L") : Side == ESide::Right ? TEXT("_R") : TEXT("");
	}

	/**
	 * Finds one side's arm, leg, twist and finger bones by name. Each lower bone must sit under the one above it
	 * (lower arm under upper arm...), so a stray "hand" elsewhere in the skeleton isn't picked.
	 * A limb missing any required bone is dropped as a whole, with a warning.
	 */
	void FindLimbs(const FSkeletonInfo& Skeleton, ESide Side, FRigLimbBones& Out, TArray<FText>& Warnings)
	{
		const FText SideName = Side == ESide::Left ? LOCTEXT("Left", "Left") : LOCTEXT("Right", "Right");

		// Arm
		Out.Clavicle = Skeleton.Find({ TEXT("clavicle"), TEXT("collar"), TEXT("shoulder") }, Side);
		Out.UpperArm = Skeleton.Find({ TEXT("upperarm"), TEXT("upper_arm"), TEXT("arm") }, Side);
		Out.LowerArm = Skeleton.Find({ TEXT("lowerarm"), TEXT("lower_arm"), TEXT("forearm") }, Side, Out.UpperArm);
		Out.Hand = Skeleton.Find({ TEXT("hand"), TEXT("wrist") }, Side, Out.LowerArm);
		if (Out.UpperArm.IsNone() || Out.LowerArm.IsNone() || Out.Hand.IsNone())
		{
			Warnings.Add(FText::Format(LOCTEXT("NoArm", "{0} arm not found (needs upper arm > lower arm > hand). It gets no arm module."), SideName));
			Out.UpperArm = Out.LowerArm = Out.Hand = NAME_None;
		}
		if (!Out.Clavicle.IsNone() && !Out.UpperArm.IsNone() && !Skeleton.IsDescendant(Out.UpperArm, Out.Clavicle))
		{
			Out.Clavicle = NAME_None;
		}

		// Leg
		Out.Thigh = Skeleton.Find({ TEXT("thigh"), TEXT("upperleg"), TEXT("upper_leg"), TEXT("upleg") }, Side);
		Out.Calf = Skeleton.Find({ TEXT("calf"), TEXT("shin"), TEXT("lowerleg"), TEXT("lower_leg"), TEXT("knee"), TEXT("leg") }, Side, Out.Thigh);
		Out.Foot = Skeleton.Find({ TEXT("foot"), TEXT("ankle") }, Side, Out.Calf);
		Out.Ball = Skeleton.Find({ TEXT("ball"), TEXT("toe"), TEXT("toes"), TEXT("toebase") }, Side, Out.Foot);
		if (Out.Thigh.IsNone() || Out.Calf.IsNone() || Out.Foot.IsNone())
		{
			Warnings.Add(FText::Format(LOCTEXT("NoLeg", "{0} leg not found (needs thigh > calf > foot). It gets no leg module."), SideName));
			Out.Thigh = Out.Calf = Out.Foot = Out.Ball = NAME_None;
		}

		// Twist bones, upper segment first
		for (const FBone& Bone : Skeleton.Bones)
		{
			if (!Bone.bTwist || Bone.bEnd || Bone.Side != Side)
			{
				continue;
			}
			if (!Out.UpperArm.IsNone() && (Bone.Parent != INDEX_NONE) && (Skeleton.IsDescendant(Bone.Name, Out.UpperArm)) && !Skeleton.IsDescendant(Bone.Name, Out.Hand))
			{
				Out.ArmTwist.Add(Bone.Name);
			}
			else if (!Out.Thigh.IsNone() && Skeleton.IsDescendant(Bone.Name, Out.Thigh) && !Skeleton.IsDescendant(Bone.Name, Out.Foot))
			{
				Out.LegTwist.Add(Bone.Name);
			}
		}
		auto SortTwist = [&Skeleton](TArray<FName>& Twist, FName Lower)
		{
			Twist.Sort([&Skeleton, Lower](FName A, FName B)
			{
				const bool bLowerA = !Lower.IsNone() && Skeleton.IsDescendant(A, Lower);
				const bool bLowerB = !Lower.IsNone() && Skeleton.IsDescendant(B, Lower);
				return bLowerA != bLowerB ? !bLowerA : A.LexicalLess(B);
			});
		};
		SortTwist(Out.ArmTwist, Out.LowerArm);
		SortTwist(Out.LegTwist, Out.Calf);

		// Fingers: every regular bone under the hand whose name starts with the finger name
		if (!Out.Hand.IsNone())
		{
			for (const TCHAR* Finger : { TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky") })
			{
				TArray<FName> Chain;
				for (const FBone& Bone : Skeleton.Bones)
				{
					if (!Bone.bEnd && !Bone.bTwist && Bone.Side == Side && Bone.Base.StartsWith(Finger) && Skeleton.IsDescendant(Bone.Name, Out.Hand))
					{
						Chain.Add(Bone.Name);
					}
				}
				Chain.Sort([&Skeleton](FName A, FName B) { return Skeleton.Depth(A) < Skeleton.Depth(B); });
				if (!Chain.IsEmpty())
				{
					Out.Fingers.Emplace(Finger, Chain);
				}
			}
		}
	}

	/**
	 * Name-based bone map: 1. root, pelvis, head, 2. neck, 3. spine, 4. limbs per side,
	 * 5. collect the bones already used, 6. sort the rest into face and other.
	 */
	FRigBoneMap MapBones(const USkeletalMesh* Mesh)
	{
		FRigBoneMap Map;
		const FSkeletonInfo Skeleton(Mesh->GetRefSkeleton());
		if (Skeleton.Bones.IsEmpty())
		{
			return Map;
		}

		// 1. Root is the first bone; pelvis and head by name
		Map.Root = Skeleton.Bones[0].Name;
		Map.Pelvis = Skeleton.Find({ TEXT("pelvis"), TEXT("hips"), TEXT("hip") }, ESide::Center);
		Map.Head = Skeleton.Find({ TEXT("head") }, ESide::Center);

		// 2. Neck: center bones named "neck..." that the head hangs under, ordered from the spine up
		for (const FBone& Bone : Skeleton.Bones)
		{
			if (!Bone.bEnd && !Bone.bTwist && Bone.Side == ESide::Center && Bone.Base.StartsWith(TEXT("neck"))
				&& (Map.Head.IsNone() || Skeleton.IsDescendant(Map.Head, Bone.Name)))
			{
				Map.Neck.Add(Bone.Name);
			}
		}
		Map.Neck.Sort([&Skeleton](FName A, FName B) { return Skeleton.Depth(A) < Skeleton.Depth(B); });

		// 3. Spine: the bones between the pelvis and the neck (or head)
		const FName Top = !Map.Neck.IsEmpty() ? Map.Neck[0] : Map.Head;
		if (Map.Pelvis.IsNone())
		{
			Map.Warnings.Add(LOCTEXT("NoPelvis", "No pelvis/hips bone found. A rig can't be generated."));
		}
		else if (Top.IsNone() || !Skeleton.IsDescendant(Top, Map.Pelvis))
		{
			Map.Warnings.Add(LOCTEXT("NoSpine", "No neck or head found above the pelvis. The rig gets no spine, neck or head modules."));
		}
		else
		{
			for (int32 Index = Skeleton.Get(Top)->Parent; Index != INDEX_NONE && Skeleton.Bones[Index].Name != Map.Pelvis; Index = Skeleton.Bones[Index].Parent)
			{
				Map.Spine.Insert(Skeleton.Bones[Index].Name, 0);
			}
			if (Map.Spine.IsEmpty())
			{
				Map.Warnings.Add(LOCTEXT("ShortSpine", "No spine bones between pelvis and neck. The rig gets no spine module."));
			}
		}

		// 4. Arms, legs, twist bones and fingers, per side
		FindLimbs(Skeleton, ESide::Left, Map.Left, Map.Warnings);
		FindLimbs(Skeleton, ESide::Right, Map.Right, Map.Warnings);

		// 5. Everything that already has a job
		TSet<FName> Used = { Map.Root, Map.Pelvis, Map.Head };
		Used.Append(Map.Spine);
		Used.Append(Map.Neck);
		for (const FRigLimbBones* Limb : { &Map.Left, &Map.Right })
		{
			Used.Append(TArray<FName>{ Limb->Clavicle, Limb->UpperArm, Limb->LowerArm, Limb->Hand, Limb->Thigh, Limb->Calf, Limb->Foot, Limb->Ball });
			Used.Append(Limb->ArmTwist);
			Used.Append(Limb->LegTwist);
			for (const TPair<FString, TArray<FName>>& Finger : Limb->Fingers)
			{
				Used.Append(Finger.Value);
			}
		}

		// 6. The rest: bones under the head become face controls, anything else "other" (end, twist and IK helper bones skipped)
		for (const FBone& Bone : Skeleton.Bones)
		{
			if (Bone.bEnd || Bone.bTwist || Bone.bHelper || Used.Contains(Bone.Name))
			{
				continue;
			}
			if (!Map.Head.IsNone() && Skeleton.IsDescendant(Bone.Name, Map.Head))
			{
				Map.Face.Add(Bone.Name);
			}
			else if (!Bone.Base.StartsWith(TEXT("ik")))
			{
				Map.Other.Add(Bone.Name);
			}
		}
		return Map;
	}

	/** String array field of a JSON object (missing field or non-strings -> skipped). */
	TArray<FString> JsonStrings(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<FString> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				FString String;
				if (Value.IsValid() && Value->TryGetString(String))
				{
					Out.Add(String);
				}
			}
		}
		return Out;
	}

	/** Object array field of a JSON object (missing field or non-objects -> skipped). */
	TArray<TSharedPtr<FJsonObject>> JsonObjects(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		TArray<TSharedPtr<FJsonObject>> Out;
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Values)
			{
				const TSharedPtr<FJsonObject>* Child = nullptr;
				if (Value.IsValid() && Value->TryGetObject(Child))
				{
					Out.Add(*Child);
				}
			}
		}
		return Out;
	}

	/** "a, b, c (+5 more)" for long lists in the notes. */
	FString ShortList(const TArray<FString>& Items, int32 Max = 12)
	{
		if (Items.Num() <= Max)
		{
			return FString::Join(Items, TEXT(", "));
		}
		return FString::Join(TArray<FString>(Items.GetData(), Max), TEXT(", ")) + FString::Printf(TEXT(" (+%d more)"), Items.Num() - Max);
	}

	/**
	 * Rebuilds the bone map from a Blender rig description (.rig.json from vrr_rig_exporter.py):
	 *  - arms, legs and fingers come from the rig's IK chains (legs are the chains that start on the pelvis);
	 *  - the pelvis is where the limb chains meet, the spine runs from there to where the arms meet;
	 *  - face/other FK controls only for bones that have their own control in Blender.
	 * Bones the description doesn't know (newer/older export) keep the name-based rules.
	 */
	bool ApplyRigDescription(const USkeletalMesh* Mesh, const FString& Path, FRigBoneMap& Map)
	{
		// 1. Load the file and check it is a vrr_rig description with a summary
		const FString FileName = FPaths::GetCleanFilename(Path);
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *Path))
		{
			Map.Warnings.Add(FText::Format(LOCTEXT("DescUnreadable", "Couldn't read the rig description {0}. Using bone names instead."), FText::FromString(Path)));
			return false;
		}
		TSharedPtr<FJsonObject> Root;
		if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid() || Root->GetStringField(TEXT("format")) != TEXT("vrr_rig"))
		{
			Map.Warnings.Add(FText::Format(LOCTEXT("DescInvalid", "{0} isn't a rig description from vrr_rig_exporter.py. Using bone names instead."), FText::FromString(FileName)));
			return false;
		}
		const TSharedPtr<FJsonObject>* SummaryPtr = nullptr;
		if (!Root->TryGetObjectField(TEXT("summary"), SummaryPtr))
		{
			Map.Warnings.Add(FText::Format(LOCTEXT("DescNoSummary", "{0} has no summary section. Using bone names instead."), FText::FromString(FileName)));
			return false;
		}
		const TSharedPtr<FJsonObject> Summary = *SummaryPtr;

		const FSkeletonInfo Skeleton(Mesh->GetRefSkeleton());
		TMap<FString, FName> SkeletonByLower;
		for (const FBone& Bone : Skeleton.Bones)
		{
			SkeletonByLower.Add(Bone.Name.ToString().ToLower(), Bone.Name);
		}
		auto ParentOf = [&Skeleton](FName Bone) -> FName
		{
			const FBone* Info = Skeleton.Get(Bone);
			return Info && Info->Parent != INDEX_NONE ? Skeleton.Bones[Info->Parent].Name : NAME_None;
		};
		auto SideOf = [&Skeleton](FName Bone) { const FBone* Info = Skeleton.Get(Bone); return Info ? Info->Side : ESide::Center; };

		// 2. Deform bones: Blender name -> skeleton bone (FBX turns '.' into '_'; match ignoring case)
		TMap<FString, FName> FromBlender;
		TSet<FName> Known;				// skeleton bones the description knows
		TSet<FName> OwnControl;			// ...that have their own control in Blender (not just following a parent)
		TMap<FName, TArray<FString>> IkControlsOf;
		TMap<FName, TArray<FString>> ControlsOf;	// FK + IK controls that move the bone
		TMap<FString, TArray<FName>> BonesOf;		// control -> bones it moves
		TMap<FName, FVector> BlenderHead;
		TArray<FString> Missing;
		int32 NumDeform = 0;
		for (const TSharedPtr<FJsonObject>& Deform : JsonObjects(Summary, TEXT("deform_bones")))
		{
			++NumDeform;
			const FString BlenderName = Deform->GetStringField(TEXT("name"));
			const FName* Bone = SkeletonByLower.Find(Deform->GetStringField(TEXT("ue_name")).ToLower());
			if (!Bone)
			{
				Missing.Add(BlenderName);
				continue;
			}
			FromBlender.Add(BlenderName, *Bone);
			Known.Add(*Bone);
			const TArray<FString> IkControls = JsonStrings(Deform, TEXT("ik_controls"));
			TArray<FString> Controls = JsonStrings(Deform, TEXT("fk_controls"));
			Controls.Append(IkControls);
			for (const FString& Control : Controls)
			{
				BonesOf.FindOrAdd(Control).AddUnique(*Bone);
			}
			ControlsOf.Add(*Bone, MoveTemp(Controls));
			IkControlsOf.Add(*Bone, IkControls);
			const TArray<TSharedPtr<FJsonValue>>* HeadValues = nullptr;
			if (Deform->TryGetArrayField(TEXT("head"), HeadValues) && HeadValues->Num() == 3)
			{
				BlenderHead.Add(*Bone, FVector((*HeadValues)[0]->AsNumber(), (*HeadValues)[1]->AsNumber(), (*HeadValues)[2]->AsNumber()));
			}
		}
		// A bone has its own control when one of its controls moves only it and its children
		// (a twist bone copying the hand's rotation shares the hand's controls, so it doesn't)
		for (const TPair<FName, TArray<FString>>& Entry : ControlsOf)
		{
			const FName Bone = Entry.Key;
			if (Algo::AnyOf(Entry.Value, [&](const FString& Control)
				{
					return Algo::AllOf(BonesOf.FindRef(Control), [&](FName Other) { return Other == Bone || Skeleton.IsDescendant(Other, Bone); });
				}))
			{
				OwnControl.Add(Bone);
			}
		}

		if (FromBlender.IsEmpty())
		{
			Map.Warnings.Add(FText::Format(LOCTEXT("DescNoMatch", "None of the {0} bones in {1} are in this skeleton. Using bone names instead."), NumDeform, FText::FromString(FileName)));
			return false;
		}
		auto ToSkeleton = [&FromBlender](const TArray<FString>& Names, bool& bComplete)
		{
			TArray<FName> Out;
			bComplete = true;
			for (const FString& Name : Names)
			{
				if (const FName* Bone = FromBlender.Find(Name))
				{
					Out.Add(*Bone);
				}
				else
				{
					bComplete = false;
				}
			}
			return Out;
		};

		// 3. IK chains: two bones = a limb, three or more = a finger
		struct FChain
		{
			TArray<FName> Bones;
			TArray<FName> Tip;
			FString Target;
		};
		TArray<FChain> Limbs, Fingers;
		TArray<FString> Skipped;
		for (const TSharedPtr<FJsonObject>& Ik : JsonObjects(Summary, TEXT("ik_chains")))
		{
			FChain Chain;
			bool bComplete = false, bTipComplete = false;
			Chain.Bones = ToSkeleton(JsonStrings(Ik, TEXT("deform_chain")), bComplete);
			Chain.Tip = ToSkeleton(JsonStrings(Ik, TEXT("deform_tip")), bTipComplete);
			Chain.Target = Ik->GetStringField(TEXT("target"));
			if (!bComplete || Chain.Bones.Num() < 2)
			{
				Skipped.Add(Ik->GetStringField(TEXT("owner")));
				continue;
			}
			(Chain.Bones.Num() == 2 ? Limbs : Fingers).Add(MoveTemp(Chain));
		}

		// 4. Pelvis: where the limb chains meet. Legs start right on it.
		auto Ancestors = [&](FName Bone)
		{
			TArray<FName> Out;
			for (FName Parent = ParentOf(Bone); !Parent.IsNone(); Parent = ParentOf(Parent))
			{
				Out.Add(Parent);
			}
			return Out;
		};
		auto CommonAncestor = [&](const TArray<FName>& Bones) -> FName
		{
			if (Bones.IsEmpty())
			{
				return NAME_None;
			}
			for (const FName Candidate : Ancestors(Bones[0]))
			{
				if (Algo::AllOf(Bones, [&](FName Bone) { return Skeleton.IsDescendant(Bone, Candidate); }))
				{
					return Candidate;
				}
			}
			return NAME_None;
		};

		TArray<FName> LimbStarts;
		for (const FChain& Limb : Limbs)
		{
			LimbStarts.Add(Limb.Bones[0]);
		}
		const FName Meeting = LimbStarts.Num() >= 2 ? CommonAncestor(LimbStarts) : NAME_None;
		const bool bHasLegs = Algo::AnyOf(Limbs, [&](const FChain& Limb) { return ParentOf(Limb.Bones[0]) == Meeting; });
		if (bHasLegs && !Meeting.IsNone())
		{
			Map.Pelvis = Meeting;
		}

		// The child of a limb bone that the IK target moves (hand, foot), or the tip the chain carries
		auto EndBone = [&](const FChain& Limb, FName Current) -> FName
		{
			if (!Limb.Tip.IsEmpty())
			{
				return Limb.Tip[0];
			}
			FName Best;
			for (const FBone& Bone : Skeleton.Bones)
			{
				if (Bone.Parent != INDEX_NONE && Skeleton.Bones[Bone.Parent].Name == Limb.Bones[1] && IkControlsOf.FindRef(Bone.Name).Contains(Limb.Target))
				{
					return Bone.Name;
				}
				if (Best.IsNone() && Bone.Parent != INDEX_NONE && Skeleton.Bones[Bone.Parent].Name == Limb.Bones[1] && !Bone.bTwist && !Bone.bEnd)
				{
					Best = Bone.Name;
				}
			}
			return !Current.IsNone() && ParentOf(Current) == Limb.Bones[1] ? Current : Best;
		};

		// 5. Each limb chain becomes a leg (starts on the pelvis) or an arm, on the side its name says
		int32 NumArms = 0, NumLegs = 0, NumFingers = 0;
		for (const FChain& Limb : Limbs)
		{
			const ESide Side = SideOf(Limb.Bones[0]);
			if (Side == ESide::Center)
			{
				Skipped.Add(Limb.Bones[0].ToString() + TEXT(" (no side in its name)"));
				continue;
			}
			FRigLimbBones& Out = Side == ESide::Left ? Map.Left : Map.Right;
			if (bHasLegs && ParentOf(Limb.Bones[0]) == Meeting)
			{
				Out.Thigh = Limb.Bones[0];
				Out.Calf = Limb.Bones[1];
				Out.Foot = EndBone(Limb, Out.Foot);
				FName Ball;
				for (const FBone& Bone : Skeleton.Bones)
				{
					if (Bone.Parent != INDEX_NONE && Skeleton.Bones[Bone.Parent].Name == Out.Foot && !Bone.bEnd && !Bone.bTwist && (Ball.IsNone() || OwnControl.Contains(Bone.Name)))
					{
						Ball = Bone.Name;
					}
				}
				Out.Ball = Ball;
				++NumLegs;
			}
			else
			{
				Out.UpperArm = Limb.Bones[0];
				Out.LowerArm = Limb.Bones[1];
				Out.Hand = EndBone(Limb, Out.Hand);
				const FName Parent = ParentOf(Out.UpperArm);
				Out.Clavicle = (!Parent.IsNone() && SideOf(Parent) == Side) ? Parent : NAME_None;
				++NumArms;
			}
		}

		// 6. Fingers: chains under a hand, named after their first bone (index_01_l -> index)
		TSet<FName> HandsFromDescription;
		for (const FChain& Finger : Fingers)
		{
			FRigLimbBones* Owner = nullptr;
			for (FRigLimbBones* Limb : { &Map.Left, &Map.Right })
			{
				if (!Limb->Hand.IsNone() && Skeleton.IsDescendant(Finger.Bones[0], Limb->Hand))
				{
					Owner = Limb;
				}
			}
			if (!Owner)
			{
				Skipped.Add(Finger.Bones[0].ToString() + TEXT(" (not under a hand)"));
				continue;
			}
			if (!HandsFromDescription.Contains(Owner->Hand))
			{
				HandsFromDescription.Add(Owner->Hand);
				Owner->Fingers.Reset();
			}
			FString Name = Skeleton.Get(Finger.Bones[0])->Base;
			while (!Name.IsEmpty() && (FChar::IsDigit(Name[Name.Len() - 1]) || Name.EndsWith(TEXT("_")) || Name.EndsWith(TEXT("."))))
			{
				Name.LeftChopInline(1);
			}
			Owner->Fingers.Emplace(Name.IsEmpty() ? Finger.Bones[0].ToString() : Name, Finger.Bones);
			++NumFingers;
		}

		// 7. Spine: pelvis to where the arms meet; neck: from there to the head
		TArray<FName> ArmStarts;
		for (const FRigLimbBones* Limb : { &Map.Left, &Map.Right })
		{
			if (!Limb->UpperArm.IsNone())
			{
				ArmStarts.Add(Limb->Clavicle.IsNone() ? Limb->UpperArm : Limb->Clavicle);
			}
		}
		const FName Chest = ArmStarts.Num() == 2 ? CommonAncestor(ArmStarts) : NAME_None;
		if (!Chest.IsNone() && !Map.Pelvis.IsNone() && Skeleton.IsDescendant(Chest, Map.Pelvis))
		{
			Map.Spine.Reset();
			for (FName Bone = Chest; !Bone.IsNone() && Bone != Map.Pelvis; Bone = ParentOf(Bone))
			{
				Map.Spine.Insert(Bone, 0);
			}
			if (!Map.Head.IsNone() && Skeleton.IsDescendant(Map.Head, Chest))
			{
				Map.Neck.Reset();
				for (FName Bone = ParentOf(Map.Head); !Bone.IsNone() && Bone != Chest; Bone = ParentOf(Bone))
				{
					Map.Neck.Insert(Bone, 0);
				}
			}
		}

		// 8. Twist bones the animator controls by hand in Blender become their own FK controls instead
		for (FRigLimbBones* Limb : { &Map.Left, &Map.Right })
		{
			Limb->ArmTwist.RemoveAll([&OwnControl](FName Bone) { return OwnControl.Contains(Bone); });
			Limb->LegTwist.RemoveAll([&OwnControl](FName Bone) { return OwnControl.Contains(Bone); });
		}

		// 9. Face/other: known bones only when Blender gives them a control; unknown bones keep the name rules
		TSet<FName> Used = { Map.Root, Map.Pelvis, Map.Head };
		Used.Append(Map.Spine);
		Used.Append(Map.Neck);
		for (const FRigLimbBones* Limb : { &Map.Left, &Map.Right })
		{
			Used.Append(TArray<FName>{ Limb->Clavicle, Limb->UpperArm, Limb->LowerArm, Limb->Hand, Limb->Thigh, Limb->Calf, Limb->Foot, Limb->Ball });
			Used.Append(Limb->ArmTwist);
			Used.Append(Limb->LegTwist);
			for (const TPair<FString, TArray<FName>>& Finger : Limb->Fingers)
			{
				Used.Append(Finger.Value);
			}
		}
		Map.Face.Reset();
		Map.Other.Reset();
		TArray<FString> Unknown;
		for (const FBone& Bone : Skeleton.Bones)
		{
			if (Bone.bEnd || Used.Contains(Bone.Name))
			{
				continue;
			}
			bool bWanted;
			if (Known.Contains(Bone.Name))
			{
				bWanted = OwnControl.Contains(Bone.Name);
			}
			else
			{
				Unknown.Add(Bone.Name.ToString());
				bWanted = !Bone.bTwist && !Bone.bHelper && !Bone.Base.StartsWith(TEXT("ik"));
			}
			if (bWanted)
			{
				(!Map.Head.IsNone() && Skeleton.IsDescendant(Bone.Name, Map.Head) ? Map.Face : Map.Other).Add(Bone.Name);
			}
		}

		// 10. Notes for the page: what matched, what was missing, what was skipped
		Map.Info.Add(FString::Printf(TEXT("Rig description: %s - %d of %d Blender deform bones found; IK chains used: %d arms, %d legs, %d fingers"),
			*FileName, FromBlender.Num(), NumDeform, NumArms, NumLegs, NumFingers));
		if (!Missing.IsEmpty())
		{
			Map.Info.Add(FString::Printf(TEXT("In the Blender rig but not this skeleton (re-export the mesh?): %s"), *ShortList(Missing)));
		}
		if (!Unknown.IsEmpty())
		{
			Map.Info.Add(FString::Printf(TEXT("Not in the rig description (bone-name rules): %s"), *ShortList(Unknown)));
		}
		if (!Skipped.IsEmpty())
		{
			Map.Info.Add(FString::Printf(TEXT("IK chains not used: %s"), *ShortList(Skipped)));
		}

		// 11. Size check: the same two bones measured in Blender (meters) and in the skeleton (cm)
		{
			const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
			TArray<FTransform> Component;
			Component.SetNum(Ref.GetRawBoneNum());
			for (int32 Index = 0; Index < Ref.GetRawBoneNum(); ++Index)
			{
				const int32 Parent = Ref.GetParentIndex(Index);
				Component[Index] = Parent == INDEX_NONE ? Ref.GetRawRefBonePose()[Index] : Ref.GetRawRefBonePose()[Index] * Component[Parent];
			}
			const FName A = Map.Pelvis, B = Map.Head;
			if (BlenderHead.Contains(A) && BlenderHead.Contains(B))
			{
				const double BlenderCm = FVector::Dist(BlenderHead[A], BlenderHead[B]) * 100.0;
				const double SkeletonCm = FVector::Dist(Component[Skeleton.IndexOf[A]].GetLocation(), Component[Skeleton.IndexOf[B]].GetLocation());
				if (BlenderCm > KINDA_SMALL_NUMBER && FMath::Abs(SkeletonCm / BlenderCm - 1.0) > 0.05)
				{
					Map.Info.Add(FString::Printf(TEXT("Size: %s to %s is %.1f cm in Blender but %.1f in this skeleton (x%.2f). The bone transforms carry a scale."),
						*A.ToString(), *B.ToString(), BlenderCm, SkeletonCm, SkeletonCm / BlenderCm));
				}
			}
		}
		return true;
	}

	/** A module connector and the bone(s) it should take. */
	struct FBoneConnection
	{
		FString Connector;
		TArray<FName> Bones;
	};

	/** One module to add: its type, place in the module tree, and which bones its connectors take. */
	struct FModulePlan
	{
		FString Type;
		FName Name;
		FName Parent;
		TArray<FName> PrimaryCandidates;			// bones the primary connector may use (Root only)
		TArray<FString> Sockets;					// preferred parent sockets for the primary connector, best first
		FName ParentBone;							// Chain modules: 'Parent Control' goes to this bone's control...
		FName ParentBoneModule;						// ...made by this (non-Chain) module
		TArray<FBoneConnection> Bones;
		TArray<FName> Owns;							// bones this module controls (for parenting later modules)
	};

	/** The rig module class for a type ("Arm", "Leg"...). Tries the 5.8 module folder first, then the older one. */
	UClass* LoadModuleClass(const FString& Type)
	{
		TArray<FString> Paths;
		if (Type == TEXT("Root"))
		{
			Paths = { TEXT("/ControlRig/Modules/Modules58/Root.Root_C"), TEXT("/ControlRig/Modules/Root.Root_C") };
		}
		else
		{
			Paths = { FString::Printf(TEXT("/ControlRigModules/Modules58/%s.%s_C"), *Type, *Type), FString::Printf(TEXT("/ControlRigModules/Modules/%s.%s_C"), *Type, *Type) };
		}
		for (const FString& Path : Paths)
		{
			if (UClass* Class = LoadObject<UClass>(nullptr, *Path, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Class;
			}
		}
		return nullptr;
	}

	/** "a, b, c" */
	FString JoinNames(const TArray<FName>& Names)
	{
		TArray<FString> Strings;
		for (const FName Name : Names)
		{
			Strings.Add(Name.ToString());
		}
		return FString::Join(Strings, TEXT(", "));
	}

	/** "index" -> "Index" */
	FString TitleCase(const FString& Text)
	{
		return Text.IsEmpty() ? Text : Text.Left(1).ToUpper() + Text.Mid(1);
	}

	/**
	 * Builds and saves the Modular Rig:
	 *  1. plan the module tree from the bone map (nothing created yet),
	 *  2. check every module class loads (fail before creating an asset),
	 *  3. create the asset with the mesh's bones and curves,
	 *  4. add each module and connect it (primary socket, known bones, then auto-connect the rest),
	 *     4b. point face/other chains at the closest body control,
	 *  5. report every connector, 6. save.
	 */
	bool Generate(USkeletalMesh* SkeletalMesh, const FRigBoneMap& Bones, const FControlRigGenSettings& Settings,
		FString& OutReport, FText& OutMessage, UControlRigBlueprint*& OutBlueprint)
	{
		const bool bFingers = Settings.bFingers;
		const bool bFace = Settings.bFace;
		const bool bOther = Settings.bOther;
		const FString& OutputFolder = Settings.OutputFolder;
		const FString& OutputName = Settings.OutputName;

		if (!SkeletalMesh || Bones.Pelvis.IsNone())
		{
			OutMessage = LOCTEXT("CantGenerate", "Pick a skeletal mesh with a pelvis bone first.");
			return false;
		}

		const FString DesiredPath = OutputFolder / OutputName;
		if (OutputName.IsEmpty() || !FPackageName::IsValidLongPackageName(DesiredPath))
		{
			OutMessage = FText::Format(LOCTEXT("BadPath", "Invalid output path: {0}"), FText::FromString(DesiredPath));
			return false;
		}

		// 1. Plan the modules
		TArray<FModulePlan> Plans;
		TMap<FName, FName> ModuleOfBone;	// bone -> module that controls it

		// Adds a planned module and records which bones it controls
		auto AddPlan = [&](const FString& Type, const FString& Name, FName Parent, TArray<FName> Primary,
			TArray<FBoneConnection> Connections, TArray<FName> Owns) -> FName
		{
			FModulePlan& Plan = Plans.AddDefaulted_GetRef();
			Plan.Type = Type;
			Plan.Name = FName(*Name);
			Plan.Parent = Parent;
			Plan.PrimaryCandidates = MoveTemp(Primary);
			Plan.Bones = MoveTemp(Connections);
			Plan.Owns = MoveTemp(Owns);
			for (const FName Bone : Plan.Owns)
			{
				ModuleOfBone.Add(Bone, Plan.Name);
			}
			return Plan.Name;
		};

		using FBoneConnections = TArray<FBoneConnection>;

		// Module primaries plug into sockets the parent module creates (Root: pelvis_socket; Spine: neck/shoulder/thigh;
		// Arm: hand_socket; Leg: foot_socket). Modules without a hint use any socket of the parent, then of its ancestors.
		auto SetSockets = [&Plans](FName Module, TArray<FString> Sockets)
		{
			if (FModulePlan* Plan = Plans.FindByPredicate([Module](const FModulePlan& P) { return P.Name == Module; }))
			{
				Plan->Sockets = MoveTemp(Sockets);
			}
		};

		// Body: Root > Spine > Neck, Shoulder > Arm > Fingers, Leg > Foot
		const FName RootModule = AddPlan(TEXT("Root"), TEXT("Root"), NAME_None, { Bones.Root }, {}, { Bones.Root });

		FName SpineModule = RootModule;
		if (!Bones.Spine.IsEmpty())
		{
			TArray<FName> Owned = Bones.Spine;
			Owned.Add(Bones.Pelvis);
			SpineModule = AddPlan(TEXT("Spine"), TEXT("Spine"), RootModule, {},
				FBoneConnections{ { TEXT("Pelvis Bone"), { Bones.Pelvis } }, { TEXT("Spine Start Bone"), { Bones.Spine[0] } }, { TEXT("Spine End Bone"), { Bones.Spine.Last() } } },
				Owned);
			SetSockets(SpineModule, { TEXT("pelvis_socket"), TEXT("spine_socket") });
		}

		if (!Bones.Head.IsNone() && SpineModule != RootModule)
		{
			const FName NeckStart = Bones.Neck.IsEmpty() ? Bones.Head : Bones.Neck[0];
			TArray<FName> Owned = Bones.Neck;
			Owned.Add(Bones.Head);
			const FName NeckModule = AddPlan(TEXT("Neck"), TEXT("Neck"), SpineModule, {},
				FBoneConnections{ { TEXT("Neck Start Bone"), { NeckStart } }, { TEXT("Head Bone"), { Bones.Head } } },
				Owned);
			SetSockets(NeckModule, { TEXT("neck_socket") });
		}

		const FName Chest = Bones.Spine.IsEmpty() ? Bones.Pelvis : Bones.Spine.Last();
		for (const TPair<ESide, const FRigLimbBones*> Side : { TPair<ESide, const FRigLimbBones*>(ESide::Left, &Bones.Left), TPair<ESide, const FRigLimbBones*>(ESide::Right, &Bones.Right) })
		{
			const FRigLimbBones& Limb = *Side.Value;
			const FString Suffix = SideSuffix(Side.Key);
			const FString SocketSide = Suffix.ToLower();	// _l / _r

			if (!Limb.UpperArm.IsNone())
			{
				FName ArmParent = SpineModule;
				if (!Limb.Clavicle.IsNone())
				{
					ArmParent = AddPlan(TEXT("Shoulder"), TEXT("Shoulder") + Suffix, SpineModule, { Limb.Clavicle },
						FBoneConnections{ { TEXT("Chest Bone"), { Chest } } }, { Limb.Clavicle });
					SetSockets(ArmParent, { TEXT("shoulder") + SocketSide + TEXT("_socket") });
				}

				TArray<FName> Owned = { Limb.UpperArm, Limb.LowerArm, Limb.Hand };
				Owned.Append(Limb.ArmTwist);
				FBoneConnections Connections = { { TEXT("Shoulder Bone"), { Limb.UpperArm } }, { TEXT("Elbow Bone"), { Limb.LowerArm } }, { TEXT("Hand Bone"), { Limb.Hand } } };
				if (!Limb.ArmTwist.IsEmpty())
				{
					Connections.Add({ TEXT("Twist Bones"), Limb.ArmTwist });
				}
				const FName ArmModule = AddPlan(TEXT("Arm"), TEXT("Arm") + Suffix, ArmParent, {}, Connections, Owned);
				if (ArmParent == SpineModule)
				{
					SetSockets(ArmModule, { TEXT("shoulder") + SocketSide + TEXT("_socket") });
				}

				if (bFingers)
				{
					for (const TPair<FString, TArray<FName>>& Finger : Limb.Fingers)
					{
						const FName FingerModule = AddPlan(TEXT("Finger"), TitleCase(Finger.Key) + Suffix, ArmModule, {},
							FBoneConnections{ { TEXT("Start Bone"), { Finger.Value[0] } }, { TEXT("End Bone"), { Finger.Value.Last() } } },
							Finger.Value);
						SetSockets(FingerModule, { Finger.Value.Last().ToString() + TEXT("_end_socket"), TEXT("hand_socket") });
					}
				}
			}

			if (!Limb.Thigh.IsNone())
			{
				TArray<FName> Owned = { Limb.Thigh, Limb.Calf };
				Owned.Append(Limb.LegTwist);
				FBoneConnections Connections = { { TEXT("Pelvis Bone"), { Bones.Pelvis } }, { TEXT("Thigh Bone"), { Limb.Thigh } }, { TEXT("Knee Bone"), { Limb.Calf } }, { TEXT("Foot Bone"), { Limb.Foot } } };
				if (!Limb.LegTwist.IsEmpty())
				{
					Connections.Add({ TEXT("Twist Bones"), Limb.LegTwist });
				}
				const FName LegModule = AddPlan(TEXT("Leg"), TEXT("Leg") + Suffix, SpineModule, {}, Connections, Owned);
				SetSockets(LegModule, { TEXT("thigh") + SocketSide + TEXT("_socket"), TEXT("pelvis_socket") });

				if (!Limb.Ball.IsNone())
				{
					const FName FootModule = AddPlan(TEXT("Foot"), TEXT("Foot") + Suffix, LegModule, {},
						FBoneConnections{ { TEXT("Foot Bone"), { Limb.Foot } }, { TEXT("Ball Bone"), { Limb.Ball } } }, { Limb.Foot, Limb.Ball });
					SetSockets(FootModule, { TEXT("ball") + SocketSide + TEXT("_socket"), TEXT("foot") + SocketSide + TEXT("_socket"), TEXT("foot_socket") });
				}
				else
				{
					ModuleOfBone.Add(Limb.Foot, LegModule);
				}
			}
		}

		// Single-bone FK chains for face and other bones, parented to the module that controls the closest parent bone
		{
			const FSkeletonInfo Skeleton(SkeletalMesh->GetRefSkeleton());
			// Closest parent bone that a module controls, and that module
			auto ParentModuleOf = [&](FName Bone, FName& OutParentBone) -> FName
			{
				for (int32 Index = Skeleton.Get(Bone)->Parent; Index != INDEX_NONE; Index = Skeleton.Bones[Index].Parent)
				{
					if (const FName* Module = ModuleOfBone.Find(Skeleton.Bones[Index].Name))
					{
						OutParentBone = Skeleton.Bones[Index].Name;
						return *Module;
					}
				}
				OutParentBone = NAME_None;
				return RootModule;
			};

			// Chain controls can't be used as a Parent Control, so find the closest parent bone driven by a body module
			auto ControlParentOf = [&](FName Bone, FName& OutModule) -> FName
			{
				for (int32 Index = Skeleton.Get(Bone)->Parent; Index != INDEX_NONE; Index = Skeleton.Bones[Index].Parent)
				{
					const FName* Module = ModuleOfBone.Find(Skeleton.Bones[Index].Name);
					const FModulePlan* Owner = Module ? Plans.FindByPredicate([Module](const FModulePlan& P) { return P.Name == *Module; }) : nullptr;
					if (Owner && Owner->Type != TEXT("Chain"))
					{
						OutModule = Owner->Name;
						return Skeleton.Bones[Index].Name;
					}
				}
				OutModule = NAME_None;
				return NAME_None;
			};

			TArray<FName> Extra;
			if (bFace)
			{
				Extra.Append(Bones.Face);
			}
			if (bOther)
			{
				Extra.Append(Bones.Other);
			}
			Extra.Sort([&Skeleton](FName A, FName B) { return Skeleton.IndexOf.FindRef(A) < Skeleton.IndexOf.FindRef(B); }); // parents first

			for (const FName Bone : Extra)
			{
				FName ParentBone;
				const FName ParentModule = ParentModuleOf(Bone, ParentBone);
				AddPlan(TEXT("Chain"), Bone.ToString(), ParentModule, {},
					FBoneConnections{ { TEXT("Start Bone"), { Bone } }, { TEXT("End Bone"), { Bone } } }, { Bone });
				FName ControlModule;
				const FName ControlBone = ControlParentOf(Bone, ControlModule);
				Plans.Last().ParentBone = ControlBone;
				Plans.Last().ParentBoneModule = ControlModule;
			}
		}

		// 2. Check the module assets are available before creating anything
		TMap<FString, UClass*> ModuleClasses;
		for (const FModulePlan& Plan : Plans)
		{
			if (!ModuleClasses.Contains(Plan.Type))
			{
				UClass* Class = LoadModuleClass(Plan.Type);
				if (!Class)
				{
					OutMessage = FText::Format(LOCTEXT("MissingModule", "Couldn't load the '{0}' rig module. Enable the Control Rig Modules plugin (Edit > Plugins) and restart."), FText::FromString(Plan.Type));
					return false;
				}
				ModuleClasses.Add(Plan.Type, Class);
			}
		}

		FScopedSlowTask SlowTask(Plans.Num() + 2.0f, LOCTEXT("Generating", "Generating Control Rig..."));
		SlowTask.MakeDialog();

		// 3. Create the rig with the mesh's bones and curves (same steps as the Content Browser's "Create Modular Rig")
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("CreatingAsset", "Creating asset..."));
		UControlRigBlueprint* Blueprint = UControlRigBlueprintFactory::CreateNewControlRigAsset(DesiredPath, true);
		if (!Blueprint)
		{
			OutMessage = LOCTEXT("CreateFailed", "Couldn't create the Control Rig asset.");
			return false;
		}

		if (URigHierarchyController* HierarchyController = Blueprint->GetHierarchyController())
		{
			HierarchyController->ImportBones(SkeletalMesh->GetRefSkeleton(), NAME_None, false, false, false, false);
			HierarchyController->ImportCurvesFromSkeletalMesh(SkeletalMesh, NAME_None, false, false);
		}
		Blueprint->GetSourceHierarchyImport() = SkeletalMesh->GetSkeleton();
		Blueprint->GetSourceCurveImport() = SkeletalMesh->GetSkeleton();
		Blueprint->PropagateHierarchyFromBPToInstances();
		Blueprint->SetPreviewMesh(SkeletalMesh);
		Blueprint->RecompileModularRig();

		UModularRigController* Controller = Blueprint->GetModularRigController();
		if (!Controller)
		{
			OutMessage = LOCTEXT("NoController", "The new asset isn't a Modular Rig.");
			return false;
		}

		// A new Modular Rig starts with its own (unconnected) Root module; start clean so ours is the only one
		for (const FName& Existing : Controller->GetAllModules())
		{
			Controller->DeleteModule(Existing, false);
		}

		// The rig hierarchy of the compiled rig (recompiles once if there is no rig instance yet)
		auto GetHierarchy = [Blueprint]() -> URigHierarchy*
		{
			UModularRig* Rig = Cast<UModularRig>(Blueprint->GetDebuggedControlRig());
			if (!Rig)
			{
				Blueprint->RecompileModularRig();
				Rig = Cast<UModularRig>(Blueprint->GetDebuggedControlRig());
			}
			return Rig ? Rig->GetHierarchy() : nullptr;
		};

		// What we need to know about one connector of a module
		struct FConnectorInfo
		{
			FRigElementKey Key;
			FString ShortName;
			bool bPrimary = false;
			bool bOptional = false;
			bool bArray = false;
		};

		// A module's connectors, with their short names ("Hand Bone") as the module defines them
		auto GetConnectors = [&](FName ModuleName) -> TArray<FConnectorInfo>
		{
			TArray<FConnectorInfo> Result;
			const URigHierarchy* Hierarchy = GetHierarchy();
			const FRigModuleReference* Module = Controller->FindModule(ModuleName);
			if (!Hierarchy || !Module)
			{
				return Result;
			}
			for (const FRigConnectorElement* Connector : Module->FindConnectors(Hierarchy))
			{
				FConnectorInfo& Info = Result.AddDefaulted_GetRef();
				Info.Key = Connector->GetKey();
				Info.ShortName = FRigHierarchyModulePath(Info.Key.Name).GetElementNameString();
				Info.bPrimary = Connector->IsPrimary();
				Info.bOptional = Connector->IsOptional();
				Info.bArray = Connector->IsArrayConnector();
			}
			return Result;
		};

		// Every element the module's rules accept for this connector (bones, sockets, controls)
		auto FindValidTargets = [&](const FRigElementKey& ConnectorKey, FName ModuleName) -> FModularRigResolveResult
		{
			UModularRig* Rig = Cast<UModularRig>(Blueprint->GetDebuggedControlRig());
			URigHierarchy* Hierarchy = Rig ? Rig->GetHierarchy() : nullptr;
			const FRigConnectorElement* Connector = Hierarchy ? Hierarchy->Find<FRigConnectorElement>(ConnectorKey) : nullptr;
			if (!Connector)
			{
				return FModularRigResolveResult();
			}
			return Hierarchy->GetRuleManager()->FindMatches(Connector, Rig->FindModule(ModuleName), Rig->GetElementKeyRedirector());
		};

		// Connects a bone connector (single or array) after asking the controller whether it may
		auto TryConnect = [&](const FConnectorInfo& Connector, const TArray<FName>& Targets, FString& OutError) -> bool
		{
			TArray<FRigElementKey> Keys;
			for (const FName Bone : Targets)
			{
				if (!Bone.IsNone())
				{
					Keys.Emplace(Bone, ERigElementType::Bone);
				}
			}
			if (Keys.IsEmpty() || (!Connector.bArray && Keys.Num() > 1))
			{
				OutError = TEXT("no matching bone");
				return false;
			}

			FText Error;
			if (Connector.bArray)
			{
				if (!Controller->CanConnectConnectorToElements(Connector.Key, Keys, Error))
				{
					OutError = Error.ToString();
					return false;
				}
				return Controller->ConnectConnectorToElements(Connector.Key, Keys, false, false, false);
			}
			if (!Controller->CanConnectConnectorToElement(Connector.Key, Keys[0], Error))
			{
				OutError = Error.ToString();
				return false;
			}
			return Controller->ConnectConnectorToElement(Connector.Key, Keys[0], false, false, false);
		};

		// 4. Add and connect each module
		int32 NumModules = 0;
		TArray<FString> Problems;
		TMap<FName, FName> ActualNames;		// planned name -> name in the rig
		TMap<FName, FName> ParentOfModule;	// rig module name -> rig parent module name
		for (const FModulePlan& Plan : Plans)
		{
			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("AddingModule", "Adding {0}..."), FText::FromName(Plan.Name)));

			// The rig may rename a module (e.g. "Root" -> "Root_1" when it clashes with the "root" bone), so track the real names
			const FName* ParentName = Plan.Parent.IsNone() ? nullptr : ActualNames.Find(Plan.Parent);
			const FName ModuleName = Controller->AddModule(Plan.Name, ModuleClasses[Plan.Type], ParentName ? *ParentName : NAME_None, false);
			if (ModuleName.IsNone())
			{
				Problems.Add(FString::Printf(TEXT("%s: couldn't add a %s module"), *Plan.Name.ToString(), *Plan.Type));
				continue;
			}
			ActualNames.Add(Plan.Name, ModuleName);
			ParentOfModule.Add(ModuleName, ParentName ? *ParentName : NAME_None);
			++NumModules;

			TArray<FConnectorInfo> Connectors = GetConnectors(ModuleName);

			// Modules from the parent up, for picking the closest socket
			TArray<FString> Ancestors;
			for (FName Ancestor = ParentOfModule.FindRef(ModuleName); !Ancestor.IsNone(); Ancestor = ParentOfModule.FindRef(Ancestor))
			{
				Ancestors.Add(Ancestor.ToString());
			}

			// Primary connector first: it decides where the module lives.
			// Root's takes a bone; the others take a socket made by the parent module.
			for (const FConnectorInfo& Connector : Connectors)
			{
				if (!Connector.bPrimary)
				{
					continue;
				}

				const FModularRigResolveResult Matches = FindValidTargets(Connector.Key, ModuleName);
				TArray<FString> Reasons;
				FRigElementKey Chosen;
				for (const FName Candidate : Plan.PrimaryCandidates)
				{
					const FRigElementKey CandidateKey(Candidate, ERigElementType::Bone);
					FString Reason;
					if (!Candidate.IsNone() && Matches.ContainsMatch(CandidateKey, &Reason))
					{
						Chosen = CandidateKey;
						break;
					}
					Reasons.Add(FString::Printf(TEXT("%s: %s"), *Candidate.ToString(), *Reason));
				}

				if (!Chosen.IsValid())
				{
					// Best socket: on our own parent chain first, then a hinted name, then the closest module
					int32 BestScore = MAX_int32;
					for (const FRigElementResolveResult& Match : Matches.GetMatches())
					{
						const FRigElementKey Key = Match.GetKey();
						if (Key.Type != ERigElementType::Socket)
						{
							continue;
						}
						const FRigHierarchyModulePath Path(Key.Name);
						const int32 AncestorIndex = Ancestors.IndexOfByPredicate([&Path](const FString& A) { return A.Equals(Path.GetModuleNameString(), ESearchCase::IgnoreCase); });
						const int32 HintIndex = Plan.Sockets.IndexOfByPredicate([&Path](const FString& S) { return S.Equals(Path.GetElementNameString(), ESearchCase::IgnoreCase); });
						const int32 Score = (AncestorIndex == INDEX_NONE ? 1000000 : AncestorIndex)
							+ (HintIndex == INDEX_NONE ? 100000 : HintIndex * 1000);
						if (Score < BestScore)
						{
							BestScore = Score;
							Chosen = Key;
						}
					}
				}

				const FRigElementResolveResult* Default = Matches.GetDefaultMatch();
				bool bUsedDefault = false;
				if (!Chosen.IsValid() && Default)
				{
					Chosen = Default->GetKey();
					bUsedDefault = true;
				}

				// What the module would accept, for the log
				TArray<FString> Valid;
				for (const FRigElementResolveResult& Match : Matches.GetMatches())
				{
					if (Valid.Num() == 40)
					{
						Valid.Add(TEXT("..."));
						break;
					}
					Valid.Add(Match.GetKey().ToString() + (Match.GetState() == ERigElementResolveState::DefaultTarget ? TEXT(" (default)") : TEXT("")));
				}
				UE_LOG(LogVrrRigGenerator, Log, TEXT("%s '%s' accepts %d targets: %s"), *ModuleName.ToString(), *Connector.ShortName, Matches.GetMatches().Num(), *FString::Join(Valid, TEXT(", ")));

				FText Error;
				if (!Chosen.IsValid())
				{
					Problems.Add(FString::Printf(TEXT("%s: '%s' has no valid target (%s)"), *ModuleName.ToString(), *Connector.ShortName,
						Reasons.IsEmpty() ? *Matches.GetMessage().ToString() : *FString::Join(Reasons, TEXT("; "))));
				}
				else if (!Controller->ConnectConnectorToElement(Connector.Key, Chosen, false, false, true))
				{
					Problems.Add(FString::Printf(TEXT("%s: '%s' couldn't connect to %s"), *ModuleName.ToString(), *Connector.ShortName, *Chosen.ToString()));
				}
				else if (!Blueprint->GetModularRigModel().Connections.HasConnection(Connector.Key))
				{
					Problems.Add(FString::Printf(TEXT("%s: '%s' connected to %s but the connection didn't stay"), *ModuleName.ToString(), *Connector.ShortName, *Chosen.ToString()));
				}
				else
				{
					UE_LOG(LogVrrRigGenerator, Log, TEXT("%s '%s' -> %s%s"), *ModuleName.ToString(), *Connector.ShortName, *Chosen.ToString(),
						bUsedDefault ? TEXT(" (module default)") : TEXT(""));
				}
			}

			// Then the bone connectors we know the answer for
			for (const FBoneConnection& Wanted : Plan.Bones)
			{
				const FConnectorInfo* Connector = Connectors.FindByPredicate([&Wanted](const FConnectorInfo& Info) { return !Info.bPrimary && Info.ShortName.Equals(Wanted.Connector, ESearchCase::IgnoreCase); });
				if (!Connector)
				{
					continue; // module version without this connector
				}
				FString Error;
				if (!TryConnect(*Connector, Wanted.Bones, Error))
				{
					Problems.Add(FString::Printf(TEXT("%s: '%s' couldn't connect to %s (%s)"), *ModuleName.ToString(), *Connector->ShortName, *JoinNames(Wanted.Bones), *Error));
				}
			}

			// The rest (parent/settings controls, pole vector and space parents) by the module's own rules
			Controller->AutoConnectModules({ ModuleName }, false, false);
		}

		// 4b. Face/other chains: follow the control of the closest parent bone (e.g. the head) instead of the auto pick.
		// Done after a recompile, once every module has built its controls.
		Blueprint->RecompileModularRig();
		for (const FModulePlan& Plan : Plans)
		{
			const FName* ModuleNamePtr = ActualNames.Find(Plan.Name);
			if (Plan.ParentBone.IsNone() || !ModuleNamePtr)
			{
				continue;
			}
			const FName ModuleName = *ModuleNamePtr;
			const TArray<FConnectorInfo> Connectors = GetConnectors(ModuleName);
			const FConnectorInfo* ParentControl = Connectors.FindByPredicate([](const FConnectorInfo& Info) { return Info.ShortName.Equals(TEXT("Parent Control"), ESearchCase::IgnoreCase); });
			if (ParentControl)
			{
				const FString BonePrefix = Plan.ParentBone.ToString() + TEXT("_");
				const FName ParentModule = ActualNames.FindRef(Plan.ParentBoneModule);

				// Rank the parent module's controls: "<bone>_..." (FK over IK, shortest first), then known aliases,
				// then any control of a one-bone Chain parent
				TArray<TPair<int32, FRigElementKey>> Ranked;
				TArray<FString> Available;
				{
					// The rule manager knows the controls the modules spawn (the hierarchy itself doesn't list them here)
					const FModularRigResolveResult Matches = FindValidTargets(ParentControl->Key, ModuleName);
					for (const FRigElementResolveResult& Match : Matches.GetMatches())
					{
						const FRigElementKey Key = Match.GetKey();
						if (Key.Type != ERigElementType::Control)
						{
							continue;
						}
						const FRigHierarchyModulePath Path(Key.Name);
						if (!Path.GetModuleNameString().Equals(ParentModule.ToString(), ESearchCase::IgnoreCase))
						{
							continue;
						}
						const FString Element = Path.GetElementNameString();
						Available.Add(Element);

						int32 Score = INDEX_NONE;
						if (Element.StartsWith(BonePrefix, ESearchCase::IgnoreCase))
						{
							Score = (Element.Contains(TEXT("_ik"), ESearchCase::IgnoreCase) ? 1000 : 0) + Element.Len();
						}
						else if (Plan.ParentBone == Bones.Pelvis && (Element.Equals(TEXT("hips_ctrl"), ESearchCase::IgnoreCase) || Element.Equals(TEXT("body_ctrl"), ESearchCase::IgnoreCase)))
						{
							Score = 2000 + Element.Len();
						}
						if (Score != INDEX_NONE)
						{
							Ranked.Emplace(Score, Key);
						}
					}
				}
				Ranked.Sort([](const TPair<int32, FRigElementKey>& A, const TPair<int32, FRigElementKey>& B) { return A.Key < B.Key; });

				bool bConnected = false;
				TArray<FString> Refused;
				for (const TPair<int32, FRigElementKey>& Candidate : Ranked)
				{
					FText Error;
					if (Controller->CanConnectConnectorToElement(ParentControl->Key, Candidate.Value, Error)
						&& Controller->ConnectConnectorToElement(ParentControl->Key, Candidate.Value, false, false, true))
					{
						UE_LOG(LogVrrRigGenerator, Log, TEXT("%s 'Parent Control' -> %s"), *ModuleName.ToString(), *Candidate.Value.ToString());
						bConnected = true;
						break;
					}
					Refused.Add(FString::Printf(TEXT("%s (%s)"), *Candidate.Value.Name.ToString(), *Error.ToString()));
				}
				if (!bConnected)
				{
					UE_LOG(LogVrrRigGenerator, Log, TEXT("%s: kept the automatic Parent Control (parent bone %s). Refused: %s. %s has: %s"), *ModuleName.ToString(), *Plan.ParentBone.ToString(),
						*FString::Join(Refused, TEXT("; ")), *ParentModule.ToString(), *FString::Join(Available, TEXT(", ")));
				}
			}
		}

		// 5. Report every connection, and anything still open
		SlowTask.EnterProgressFrame(1.0f, LOCTEXT("Finishing", "Finishing..."));
		Blueprint->RecompileModularRig();

		const FModularRigModel& Model = Blueprint->GetModularRigModel();
		TArray<FString> ReportLines;
		for (const FModulePlan& Plan : Plans)
		{
			const FName* ModuleName = ActualNames.Find(Plan.Name);
			if (!ModuleName || !Controller->FindModule(*ModuleName))
			{
				continue;
			}
			ReportLines.Add(FString::Printf(TEXT("%s (%s):"), *ModuleName->ToString(), *Plan.Type));
			for (const FConnectorInfo& Connector : GetConnectors(*ModuleName))
			{
				if (Model.Connections.HasConnection(Connector.Key))
				{
					TArray<FString> Targets;
					for (const FRigElementKey& Target : Model.Connections.FindTargetsFromConnector(Connector.Key))
					{
						Targets.Add(Target.Name.ToString());
					}
					ReportLines.Add(FString::Printf(TEXT("    %s -> %s"), *Connector.ShortName, *FString::Join(Targets, TEXT(", "))));
				}
				else
				{
					ReportLines.Add(FString::Printf(TEXT("    %s -> (not connected%s)"), *Connector.ShortName, Connector.bOptional ? TEXT(", optional") : TEXT("")));
					if (!Connector.bOptional)
					{
						Problems.AddUnique(FString::Printf(TEXT("%s: '%s' isn't connected"), *ModuleName->ToString(), *Connector.ShortName));
					}
				}
			}
		}
		OutReport = FString::Join(ReportLines, TEXT("\n"));
		if (!Problems.IsEmpty())
		{
			OutReport += TEXT("\nProblems:\n    ") + FString::Join(Problems, TEXT("\n    "));
		}

		// 6. Save, so the rig opens with its modules compiled
		Blueprint->MarkPackageDirty();
		UEditorLoadingAndSavingUtils::SavePackages({ Blueprint->GetPackage() }, true);
		OutBlueprint = Blueprint;

		OutMessage = FText::Format(LOCTEXT("Done", "Created and saved {0} with {1} modules. The full connection list is in the Output Log (LogVrrRigGenerator)."),
			FText::FromString(Blueprint->GetName()), NumModules);
		if (!Problems.IsEmpty())
		{
			const int32 NumShown = FMath::Min(Problems.Num(), 6);
			TArray<FString> Shown(Problems.GetData(), NumShown);
			OutMessage = FText::Format(LOCTEXT("DoneWithProblems", "{0}\n{1} {1}|plural(one=item needs,other=items need) a look in the rig editor:\n{2}{3}"),
				OutMessage, Problems.Num(), FText::FromString(FString::Join(Shown, TEXT("\n"))),
				Problems.Num() > NumShown ? LOCTEXT("More", "\n...") : FText::GetEmpty());
		}
		return true;
	}
}

namespace ControlRigGenerator
{
	/** Name rules first, then the rig description (if given) overrides what it knows. */
	FRigBoneMap MapBones(const USkeletalMesh* Mesh, const FString& RigDescriptionPath)
	{
		if (!Mesh)
		{
			return FRigBoneMap();
		}
		FRigBoneMap Map = ControlRigGen::MapBones(Mesh);
		if (!RigDescriptionPath.IsEmpty())
		{
			ControlRigGen::ApplyRigDescription(Mesh, RigDescriptionPath, Map);
		}
		return Map;
	}

	/** A lone *.rig.json in the mesh folder or the folder above it on disk, or empty (none, or more than one). */
	FString FindRigDescription(const USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			return FString();
		}
		const FString OutputFolder = FPackageName::GetLongPackagePath(Mesh->GetOutermost()->GetName());
		FString Found;
		FString Folder = FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(OutputFolder));
		for (int32 Level = 0; Level < 2 && Found.IsEmpty(); ++Level)
		{
			TArray<FString> Files;
			IFileManager::Get().FindFiles(Files, *(Folder / TEXT("*.rig.json")), true, false);
			if (Files.Num() == 1)
			{
				Found = Folder / Files[0];
			}
			Folder = FPaths::GetPath(Folder);
		}
		return Found;
	}

	/** SKM_Hero -> CR_Hero */
	FString SuggestName(const USkeletalMesh* Mesh)
	{
		return FString::Printf(TEXT("CR_%s"), *VrrCore::GetBaseAssetName(Mesh));
	}

	/** The bone map as readable lines for the page: body parts, limbs per side, face/other, notes and warnings. */
	FString DescribeBones(const FRigBoneMap& Bones)
	{
		using namespace ControlRigGen;

		auto Or = [](FName Name) { return Name.IsNone() ? FString(TEXT("-")) : Name.ToString(); };

		TArray<FString> Lines;
		Lines.Add(FString::Printf(TEXT("Root: %s    Pelvis: %s    Head: %s"), *Or(Bones.Root), *Or(Bones.Pelvis), *Or(Bones.Head)));
		Lines.Add(FString::Printf(TEXT("Spine (%d): %s"), Bones.Spine.Num(), *JoinNames(Bones.Spine)));
		Lines.Add(FString::Printf(TEXT("Neck (%d): %s"), Bones.Neck.Num(), *JoinNames(Bones.Neck)));

		for (const TPair<const TCHAR*, const FRigLimbBones*> Side : { TPair<const TCHAR*, const FRigLimbBones*>(TEXT("Left"), &Bones.Left), TPair<const TCHAR*, const FRigLimbBones*>(TEXT("Right"), &Bones.Right) })
		{
			const FRigLimbBones& Limb = *Side.Value;
			TArray<FString> FingerNames;
			for (const TPair<FString, TArray<FName>>& Finger : Limb.Fingers)
			{
				FingerNames.Add(FString::Printf(TEXT("%s (%d)"), *Finger.Key, Finger.Value.Num()));
			}
			Lines.Add(FString::Printf(TEXT("%s arm: %s > %s > %s > %s    twist: %d    fingers: %s"), Side.Key,
				*Or(Limb.Clavicle), *Or(Limb.UpperArm), *Or(Limb.LowerArm), *Or(Limb.Hand), Limb.ArmTwist.Num(),
				FingerNames.IsEmpty() ? TEXT("-") : *FString::Join(FingerNames, TEXT(", "))));
			Lines.Add(FString::Printf(TEXT("%s leg: %s > %s > %s > %s    twist: %d"), Side.Key,
				*Or(Limb.Thigh), *Or(Limb.Calf), *Or(Limb.Foot), *Or(Limb.Ball), Limb.LegTwist.Num()));
		}

		Lines.Add(FString::Printf(TEXT("Face (%d): %s"), Bones.Face.Num(), *JoinNames(Bones.Face)));
		Lines.Add(FString::Printf(TEXT("Other (%d): %s"), Bones.Other.Num(), *JoinNames(Bones.Other)));

		for (const FString& Info : Bones.Info)
		{
			Lines.Add(Info);
		}

		for (const FText& Warning : Bones.Warnings)
		{
			Lines.Add(FString::Printf(TEXT("Warning: %s"), *Warning.ToString()));
		}
		return FString::Join(Lines, TEXT("\n"));
	}

	/** Generates the rig and writes the full connection report to the Output Log (LogVrrRigGenerator). */
	FControlRigGenResult Generate(USkeletalMesh* Mesh, const FRigBoneMap& Bones, const FControlRigGenSettings& Settings)
	{
		FControlRigGenResult Result;
		Result.bSuccess = ControlRigGen::Generate(Mesh, Bones, Settings, Result.Report, Result.Message, Result.Blueprint);

		UE_LOG(LogVrrRigGenerator, Log, TEXT("Control Rig generator report:\n%s%s"),
			Bones.Info.IsEmpty() ? TEXT("") : *(FString::Join(Bones.Info, TEXT("\n")) + TEXT("\n")), *Result.Report);
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
