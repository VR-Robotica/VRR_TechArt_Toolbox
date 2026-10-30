// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Mesh validation: checks Static and Skeletal Mesh assets against the project's mesh rules.
 *
 * Strategy (like the texture validator)
 *  - The prefix says what the asset is: SM_ static mesh, SK_ (or SKM_) skeletal mesh. A skeletal mesh's
 *    skeleton is SKEL_ and its physics asset PHYS_. Names after the prefix are letters, digits and single underscores (AssetValidation::GetNameBodyProblem).
 *  - Each other check looks at what makes a mesh render or cost wrong in game: missing or badly named materials,
 *    no collision, dense meshes without LODs, bounds that point to a unit scale mistake, a missing lightmap channel.
 *  - Errors are things that are broken in game (a lightmap channel that doesn't exist); the rest are warnings.
 *  - Fix All renames: the right prefix plus a cleaned-up name (SK_Rock Mesh on a static mesh -> SM_Rock_Mesh; an
 *    unknown prefix is kept or replaced, as the person fixing chooses: Mesh_Rock -> SM_Mesh_Rock or SM_Rock), and
 *    SKEL_ / PHYS_ for the skeleton and physics asset (SK_Hero_Skeleton -> SKEL_Hero). The other checks
 *    (materials, collision, LODs, scale, lightmaps) need a person, so they have no fix.
 */

#include "Core_MeshValidator.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

#define LOCTEXT_NAMESPACE "MeshValidator"

namespace MeshValidator
{
	using ESeverity = FAssetIssue::ESeverity;

	/** Prefixes a mesh may start with. */
	const TCHAR* const StaticPrefixes[] = { TEXT("SM_") };
	const TCHAR* const SkeletalPrefixes[] = { TEXT("SK_"), TEXT("SKM_") };
	/** Prefixes of the assets a skeletal mesh points to, and of assigned materials. */
	const TCHAR* const SkeletonPrefix = TEXT("SKEL_");
	const TCHAR* const PhysicsPrefix = TEXT("PHYS_");
	const TCHAR* const MaterialPrefixes[] = { TEXT("M_"), TEXT("MI_") };

	/** The prefix Name starts with (case-sensitive, as the convention is), or empty. */
	template <SIZE_T N>
	FString FindPrefix(const FString& Name, const TCHAR* const (&Prefixes)[N])
	{
		for (const TCHAR* Prefix : Prefixes)
		{
			if (Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return Prefix;
			}
		}
		return FString();
	}

	template <SIZE_T N>
	FString JoinPrefixes(const TCHAR* const (&Prefixes)[N], const TCHAR* Separator)
	{
		TArray<FString> Names;
		for (const TCHAR* Prefix : Prefixes)
		{
			Names.Add(Prefix);
		}
		return FString::Join(Names, Separator);
	}

	FString GetPrefixList()
	{
		return FString::Printf(TEXT("%s (Static Mesh), %s (Skeletal Mesh), %s (Skeleton), %s (Physics Asset); materials %s"),
			*JoinPrefixes(StaticPrefixes, TEXT(" or ")), *JoinPrefixes(SkeletalPrefixes, TEXT(" or ")), SkeletonPrefix, PhysicsPrefix,
			*JoinPrefixes(MaterialPrefixes, TEXT(" or ")));
	}

	/**
	 * Name for a skeleton or physics asset: Prefix + the name without a mesh prefix or Unreal's default suffix
	 * (SK_Hero_Skeleton -> SKEL_Hero, SK_Hero_PhysicsAsset -> PHYS_Hero). Empty if nothing is left.
	 */
	FString MakeLinkedAssetName(FString Name, const TCHAR* Prefix, const TCHAR* DefaultSuffix)
	{
		const FString MeshPrefix = FindPrefix(Name, SkeletalPrefixes) + FindPrefix(Name, StaticPrefixes);
		Name.RemoveFromStart(MeshPrefix);
		Name.RemoveFromEnd(DefaultSuffix, ESearchCase::IgnoreCase);
		const FString Body = AssetValidation::CleanNameBody(Name);
		return Body.IsEmpty() ? FString() : Prefix + Body;
	}

	/** Engine and plugin content isn't ours to rename. */
	bool IsProjectContent(const UObject* Object)
	{
		return Object && Object->GetPathName().StartsWith(TEXT("/Game/"));
	}

	/**
	 * Runs every enabled check on one mesh: naming (mesh, then skeleton and physics asset), materials,
	 * collision, LODs, scale, and lightmap UVs.
	 */
	void ValidateMesh(const FAssetData& Asset, UObject* Mesh, const FMeshValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues)
	{
		// Returns the issue so a fix can be attached
		auto AddIssue = [&Asset, &OutIssues](ESeverity Severity, const FText& Rule, const FText& Message) -> FAssetIssue&
		{
			return OutIssues.Add_GetRef({ Asset, Severity, FAssetIssue::EAssetType::Mesh, Rule, Message });
		};

		UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);
		if (!StaticMesh && !SkeletalMesh)
		{
			return;
		}
		const FString Name = Asset.AssetName.ToString();

		// NAMING
		if (Settings.bCheckNaming)
		{
			const FText Rule = LOCTEXT("RuleNaming", "Naming");
			const FString Prefix = StaticMesh ? FindPrefix(Name, StaticPrefixes) : FindPrefix(Name, SkeletalPrefixes);
			const FString WrongPrefix = Prefix.IsEmpty() ? (StaticMesh ? FindPrefix(Name, SkeletalPrefixes) : FindPrefix(Name, StaticPrefixes)) : FString();
			const FString Body = Name.Mid(Prefix.Len() + WrongPrefix.Len());

			// The name both naming issues fix to: the right prefix (kept, swapped or added) and a clean body
			const FString CleanBody = AssetValidation::CleanNameBody(Body);
			const FString WantedPrefix = Prefix.IsEmpty() ? FString(StaticMesh ? StaticPrefixes[0] : SkeletalPrefixes[0]) : Prefix;
			const FString FixedName = CleanBody.IsEmpty() ? FString() : WantedPrefix + CleanBody;

			// No prefix we know: the first part may be another convention's (Mesh_Rock): keep it, or replace it
			const FString WithoutFirst = Prefix.IsEmpty() && WrongPrefix.IsEmpty() ? AssetValidation::RemoveFirstToken(Body) : FString();
			const FString Names[2][2] = { { FixedName, FString() }, { WithoutFirst.IsEmpty() ? FString() : WantedPrefix + WithoutFirst, FString() } };

			if (Prefix.IsEmpty())
			{
				// The other type's prefix is a clearer message than "missing prefix"
				AddIssue(ESeverity::Warning, Rule, !WrongPrefix.IsEmpty()
					? (StaticMesh ? LOCTEXT("SkeletalPrefixOnStatic", "Skeletal mesh prefix on a static mesh (use SM_)") : LOCTEXT("StaticPrefixOnSkeletal", "Static mesh prefix on a skeletal mesh (use SK_)"))
					: (StaticMesh ? LOCTEXT("MissingStaticPrefix", "Name should start with SM_") : LOCTEXT("MissingSkeletalPrefix", "Name should start with SK_")));
				AssetValidation::SetRenameChoices(OutIssues.Last(), Name, Names);
			}
			const FText BodyProblem = AssetValidation::GetNameBodyProblem(Body);
			if (!BodyProblem.IsEmpty())
			{
				AssetValidation::SetRenameChoices(AddIssue(ESeverity::Warning, Rule, BodyProblem), Name, Names);
			}

			// The skeleton and physics asset a skeletal mesh uses
			if (SkeletalMesh)
			{
				const USkeleton* Skeleton = SkeletalMesh->GetSkeleton();
				if (IsProjectContent(Skeleton) && !Skeleton->GetName().StartsWith(SkeletonPrefix, ESearchCase::CaseSensitive))
				{
					FAssetIssue& Issue = AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("SkeletonPrefix", "Skeleton {0} should start with SKEL_"), FText::FromString(Skeleton->GetName())));
					Issue.RenameTo = MakeLinkedAssetName(Skeleton->GetName(), SkeletonPrefix, TEXT("_Skeleton"));
					Issue.RenameObject = FSoftObjectPath(Skeleton);
				}
				const UPhysicsAsset* Physics = SkeletalMesh->GetPhysicsAsset();
				if (IsProjectContent(Physics) && !Physics->GetName().StartsWith(PhysicsPrefix, ESearchCase::CaseSensitive))
				{
					FAssetIssue& Issue = AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("PhysicsPrefix", "Physics asset {0} should start with PHYS_"), FText::FromString(Physics->GetName())));
					Issue.RenameTo = MakeLinkedAssetName(Physics->GetName(), PhysicsPrefix, TEXT("_PhysicsAsset"));
					Issue.RenameObject = FSoftObjectPath(Physics);
				}
			}
		}

		// MATERIALS: every slot filled, project materials named M_ / MI_
		if (Settings.bCheckMaterials)
		{
			const FText Rule = LOCTEXT("RuleMaterials", "Materials");
			auto CheckSlot = [&](int32 Index, FName SlotName, const UMaterialInterface* Material)
			{
				const FText Slot = SlotName.IsNone() ? FText::AsNumber(Index) : FText::Format(LOCTEXT("SlotNamed", "{0} ({1})"), Index, FText::FromName(SlotName));
				if (!Material)
				{
					AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("NoMaterial", "Slot {0} has no material (renders with the default material)"), Slot));
				}
				else if (IsProjectContent(Material) && FindPrefix(Material->GetName(), MaterialPrefixes).IsEmpty())
				{
					AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("MaterialPrefix", "Slot {0}: material {1} should start with M_ or MI_"), Slot, FText::FromString(Material->GetName())));
				}
			};
			if (StaticMesh)
			{
				const TArray<FStaticMaterial>& Materials = StaticMesh->GetStaticMaterials();
				for (int32 Index = 0; Index < Materials.Num(); ++Index)
				{
					CheckSlot(Index, Materials[Index].MaterialSlotName, Materials[Index].MaterialInterface);
				}
			}
			else
			{
				const TArray<FSkeletalMaterial>& Materials = SkeletalMesh->GetMaterials();
				for (int32 Index = 0; Index < Materials.Num(); ++Index)
				{
					CheckSlot(Index, Materials[Index].MaterialSlotName, Materials[Index].MaterialInterface);
				}
			}
		}

		// COLLISION: simple shapes on static meshes (unless complex is used as simple), a physics asset on skeletal ones
		if (Settings.bCheckCollision)
		{
			const FText Rule = LOCTEXT("RuleCollision", "Collision");
			if (StaticMesh)
			{
				const UBodySetup* BodySetup = StaticMesh->GetBodySetup();
				const bool bComplexAsSimple = BodySetup && BodySetup->GetCollisionTraceFlag() == CTF_UseComplexAsSimple;
				if (!bComplexAsSimple && (!BodySetup || BodySetup->AggGeom.GetElementCount() == 0))
				{
					AddIssue(ESeverity::Warning, Rule, LOCTEXT("NoSimpleCollision", "No simple collision (add some, or set Collision Complexity to Use Complex As Simple)"));
				}
			}
			else if (!SkeletalMesh->GetPhysicsAsset())
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NoPhysicsAsset", "No physics asset (no collision, ragdoll or per-bone hits)"));
			}
		}

		// LODs: dense meshes need LODs, unless Nanite handles them
		if (Settings.bCheckLODs)
		{
			const FText Rule = LOCTEXT("RuleLODs", "LODs");
			int32 NumLODs = 0;
			int32 NumTriangles = 0;
			bool bNanite = false;
			if (StaticMesh)
			{
				NumLODs = StaticMesh->GetNumLODs();
				NumTriangles = NumLODs > 0 ? StaticMesh->GetNumTriangles(0) : 0;
				bNanite = StaticMesh->IsNaniteEnabled();
			}
			else
			{
				NumLODs = SkeletalMesh->GetLODNum();
				const FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
				NumTriangles = RenderData && RenderData->LODRenderData.Num() > 0 ? static_cast<int32>(RenderData->LODRenderData[0].GetTotalFaces()) : 0;
			}
			if (!bNanite && NumLODs <= 1 && NumTriangles > Settings.MaxTrianglesWithoutLODs)
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("NoLODs", "{0} triangles and no LODs (over {1}: add LODs{2})"),
					FText::AsNumber(NumTriangles), FText::AsNumber(Settings.MaxTrianglesWithoutLODs),
					StaticMesh ? LOCTEXT("OrNanite", " or enable Nanite") : FText::GetEmpty()));
			}
		}

		// SCALE: bounds far outside the expected range usually mean a unit mistake on import (m vs cm)
		if (Settings.bCheckScale)
		{
			const FText Rule = LOCTEXT("RuleScale", "Scale");
			const FBoxSphereBounds Bounds = StaticMesh ? StaticMesh->GetBounds() : SkeletalMesh->GetImportedBounds();
			const float Largest = static_cast<float>(Bounds.BoxExtent.GetMax() * 2.0);
			FNumberFormattingOptions Format;
			Format.SetUseGrouping(false).SetMaximumFractionalDigits(2);
			const FText SizeText = FText::Format(LOCTEXT("SizeCm", "{0} cm"), FText::AsNumber(Largest, &Format));
			if (Largest > Settings.MaxSize)
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("TooBig", "{0} across: imported at the wrong scale? (over {1} cm)"), SizeText, FText::AsNumber(Settings.MaxSize)));
			}
			else if (Largest < Settings.MinSize)
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("TooSmall", "{0} across: imported at the wrong scale? (under {1} cm)"), SizeText, FText::AsNumber(Settings.MinSize)));
			}
		}

		// LIGHTMAP UVs (static meshes): the channel exists and isn't the (usually overlapping) texture channel
		if (Settings.bCheckLightmap && StaticMesh)
		{
			const FText Rule = LOCTEXT("RuleLightmap", "Lightmap");
			const int32 Channel = StaticMesh->GetLightMapCoordinateIndex();
			const int32 NumChannels = StaticMesh->GetNumUVChannels(0);
			if (Channel >= NumChannels)
			{
				AddIssue(ESeverity::Error, Rule, FText::Format(LOCTEXT("NoLightmapChannel", "Lightmap uses UV{0}, but the mesh has {1} UV channels"), Channel, NumChannels));
			}
			else if (Channel == 0)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("LightmapUV0", "Lightmap uses UV0, whose islands usually overlap (create one in UV Layouts > Balanced UV Layout)"));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
