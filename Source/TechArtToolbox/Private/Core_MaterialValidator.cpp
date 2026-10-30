// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Material validation: checks material assets against the project's naming rules.
 *
 * Strategy (like the mesh validator)
 *  - The prefix says what the asset is: M_ material (master material), MI_ material instance,
 *    MF_ or ML_ material function (ML_ for material layers and layer blends, by habit, but either is accepted),
 *    PPM_ post process material (Post Process domain), PPMI_ instance of one (its root parent is post process).
 *  - If the name carries another material kind's prefix, say which one it should be (clearer than "missing prefix").
 *  - After the prefix: letters, digits and single underscores (AssetValidation::GetNameBodyProblem).
 *  - Instances shouldn't keep the _Inst suffix Unreal gives a new instance (M_Rock_Inst).
 *  - Every naming issue carries the one name that fixes them all (M_Rock_Inst -> MI_Rock) for Fix All. An unknown
 *    prefix is kept or replaced, as the person fixing chooses (Mat_Rock -> M_Mat_Rock or M_Rock).
 *  - Only the asset's class, name and Asset Registry tags (MaterialDomain, an instance's Parent) are needed, so nothing is loaded.
 *  - Renaming to a post process prefix drops a leading PP_ that the old name carried (M_PP_CelShadow -> PPM_CelShadow).
 */

#include "Core_MaterialValidator.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialInstance.h"
#include "Misc/PackageName.h"

#define LOCTEXT_NAMESPACE "MaterialValidator"

namespace MaterialValidator
{
	using ESeverity = FAssetIssue::ESeverity;

	enum class EKind : uint8 { None, Material, Instance, Function, PostProcess, PostProcessInstance };

	constexpr EKind AllKinds[] = { EKind::Material, EKind::Instance, EKind::Function, EKind::PostProcess, EKind::PostProcessInstance };

	/** Prefixes each kind may start with (case-sensitive, as the convention is). */
	const TCHAR* const MaterialPrefixes[] = { TEXT("M_") };
	const TCHAR* const InstancePrefixes[] = { TEXT("MI_") };
	const TCHAR* const FunctionPrefixes[] = { TEXT("MF_"), TEXT("ML_") };
	const TCHAR* const PostProcessPrefixes[] = { TEXT("PPM_") };
	const TCHAR* const PostProcessInstancePrefixes[] = { TEXT("PPMI_") };

	/** Leading part of an old post process name that the PPM_/PPMI_ prefix replaces (M_PP_CelShadow -> PPM_CelShadow). */
	const TCHAR* const OldPostProcessTag = TEXT("PP_");

	enum class EDomain : uint8 { Unknown, PostProcess, Other };

	/** A material's domain from its MaterialDomain tag, or Unknown if the tag is missing. */
	EDomain GetMaterialDomain(const FAssetData& Material)
	{
		FString Domain;
		if (!Material.GetTagValue(GET_MEMBER_NAME_CHECKED(UMaterial, MaterialDomain), Domain))
		{
			return EDomain::Unknown;
		}
		return Domain.EndsWith(TEXT("PostProcess")) ? EDomain::PostProcess : EDomain::Other;
	}

	/** An instance's domain: follows the Parent tags up to the root material. Unknown if the chain breaks. */
	EDomain GetInstanceDomain(const FAssetData& Instance)
	{
		const IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
		FAssetData Current = Instance;
		for (int32 Depth = 0; Depth < 32; ++Depth)	// guards against a parent loop
		{
			if (Current.IsInstanceOf(UMaterial::StaticClass()))
			{
				return GetMaterialDomain(Current);
			}
			FString ParentPath;
			if (!Current.GetTagValue(GET_MEMBER_NAME_CHECKED(UMaterialInstance, Parent), ParentPath) || ParentPath.IsEmpty() || ParentPath == TEXT("None"))
			{
				return EDomain::Unknown;
			}
			Current = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(FPackageName::ExportTextPathToObjectPath(ParentPath)));
			if (!Current.IsValid())
			{
				return EDomain::Unknown;
			}
		}
		return EDomain::Unknown;
	}

	/**
	 * The post process kind if the domain says so. If the domain can't be read (tag missing, parent not found),
	 * a PPM_/PPMI_ name is trusted rather than flagged.
	 */
	EKind PickKind(EDomain Domain, const FAssetData& Asset, EKind Normal, EKind PostProcess, const TCHAR* PostProcessPrefix)
	{
		if (Domain == EDomain::PostProcess
			|| (Domain == EDomain::Unknown && Asset.AssetName.ToString().StartsWith(PostProcessPrefix, ESearchCase::CaseSensitive)))
		{
			return PostProcess;
		}
		return Normal;
	}

	EKind GetKind(const FAssetData& Asset)
	{
		if (Asset.IsInstanceOf(UMaterial::StaticClass()))
		{
			return PickKind(GetMaterialDomain(Asset), Asset, EKind::Material, EKind::PostProcess, PostProcessPrefixes[0]);
		}
		if (Asset.IsInstanceOf(UMaterialInstance::StaticClass()))
		{
			return PickKind(GetInstanceDomain(Asset), Asset, EKind::Instance, EKind::PostProcessInstance, PostProcessInstancePrefixes[0]);
		}
		if (Asset.IsInstanceOf(UMaterialFunctionInterface::StaticClass()))
		{
			return EKind::Function;
		}
		return EKind::None;
	}

	TConstArrayView<const TCHAR*> GetPrefixes(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::Material:	return MaterialPrefixes;
		case EKind::Instance:	return InstancePrefixes;
		case EKind::Function:	return FunctionPrefixes;
		case EKind::PostProcess:	return PostProcessPrefixes;
		case EKind::PostProcessInstance:	return PostProcessInstancePrefixes;
		default:				return {};
		}
	}

	FText GetKindName(EKind Kind)
	{
		switch (Kind)
		{
		case EKind::Material:	return LOCTEXT("KindMaterial", "material");
		case EKind::Instance:	return LOCTEXT("KindInstance", "material instance");
		case EKind::Function:	return LOCTEXT("KindFunction", "material function");
		case EKind::PostProcess:	return LOCTEXT("KindPostProcess", "post process material");
		case EKind::PostProcessInstance:	return LOCTEXT("KindPostProcessInstance", "post process material instance");
		default:				return FText::GetEmpty();
		}
	}

	/** "M_", "MI_" or "MF_ or ML_". */
	FString JoinPrefixes(EKind Kind)
	{
		return FString::Join(GetPrefixes(Kind), TEXT(" or "));
	}

	/** The prefix Name starts with, or empty. */
	FString FindPrefix(const FString& Name, EKind Kind)
	{
		for (const TCHAR* Prefix : GetPrefixes(Kind))
		{
			if (Name.StartsWith(Prefix, ESearchCase::CaseSensitive))
			{
				return Prefix;
			}
		}
		return FString();
	}

	FString GetPrefixList()
	{
		return FString::Printf(TEXT("%s (Material), %s (Material Instance), %s (Material Function), %s (Post Process Material), %s (Post Process Material Instance)"),
			*JoinPrefixes(EKind::Material), *JoinPrefixes(EKind::Instance), *JoinPrefixes(EKind::Function),
			*JoinPrefixes(EKind::PostProcess), *JoinPrefixes(EKind::PostProcessInstance));
	}

	bool IsMaterialAsset(const FAssetData& Asset)
	{
		return GetKind(Asset) != EKind::None;
	}

	bool IsInstanceKind(EKind Kind)
	{
		return Kind == EKind::Instance || Kind == EKind::PostProcessInstance;
	}

	bool IsPostProcessKind(EKind Kind)
	{
		return Kind == EKind::PostProcess || Kind == EKind::PostProcessInstance;
	}

	/** The material kind an instance kind is made from (Instance -> Material), or None. */
	EKind GetParentKind(EKind Kind)
	{
		return Kind == EKind::Instance ? EKind::Material : Kind == EKind::PostProcessInstance ? EKind::PostProcess : EKind::None;
	}

	const TCHAR* const InstSuffix = TEXT("_Inst");

	/**
	 * Name without Unreal's default _Inst suffix, keeping the number Unreal adds for uniqueness
	 * (M_Rock_Inst -> M_Rock, M_Rock_Inst2 -> M_Rock_2), or empty if it has none.
	 */
	FString StripInstSuffix(const FString& Name)
	{
		const int32 SuffixAt = Name.Find(InstSuffix, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
		if (SuffixAt <= 0)
		{
			return FString();
		}
		FString Number = Name.Mid(SuffixAt + FCString::Strlen(InstSuffix));
		Number.RemoveFromStart(TEXT("_"));
		if (!Number.IsEmpty() && !Number.IsNumeric())
		{
			return FString();
		}
		return Number.IsEmpty() ? Name.Left(SuffixAt) : Name.Left(SuffixAt) + TEXT("_") + Number;
	}

	/** The prefix a name without one gets: ML_ for material layers and layer blends, else the kind's first prefix. */
	FString GetDefaultPrefix(const FAssetData& Asset, EKind Kind)
	{
		if (Kind == EKind::Function && (Asset.IsInstanceOf(UMaterialFunctionMaterialLayer::StaticClass())
			|| Asset.IsInstanceOf(UMaterialFunctionMaterialLayerBlend::StaticClass())))
		{
			return FunctionPrefixes[1];
		}
		return GetPrefixes(Kind)[0];
	}

	/** The other kind whose prefix Name starts with (an M_ instance, an MI_ material...), or None. */
	EKind FindWrongKind(const FString& Name, EKind Kind, FString& OutPrefix)
	{
		for (const EKind Other : AllKinds)
		{
			if (Other != Kind)
			{
				OutPrefix = FindPrefix(Name, Other);
				if (!OutPrefix.IsEmpty())
				{
					return Other;
				}
			}
		}
		return EKind::None;
	}

	/**
	 *  1. instances: drop the _Inst suffix,
	 *  2. work out the name every naming issue fixes to: the right prefix (kept, swapped for a wrong kind's, or added)
	 *     and a cleaned-up body. M_Rock_Inst -> M_Rock -> MI_Rock, since Unreal names an instance after its parent,
	 *  3. add the issues, each carrying that name for Fix All.
	 */
	void ValidateMaterial(const FAssetData& Asset, const FMaterialValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues)
	{
		const EKind Kind = GetKind(Asset);
		if (Kind == EKind::None)
		{
			return;
		}
		const FString Name = Asset.AssetName.ToString();
		FString Names[2][2];	// [replace unknown prefix][unused]: filled below
		auto AddIssue = [&Asset, &OutIssues, &Name, &Names](const FText& Rule, const FText& Message)
		{
			FAssetIssue& Issue = OutIssues.Add_GetRef({ Asset, FAssetIssue::ESeverity::Warning, FAssetIssue::EAssetType::Material, Rule, Message });
			AssetValidation::SetRenameChoices(Issue, Name, Names);
		};

		// 1. _Inst suffix
		const FString WithoutInst = IsInstanceKind(Kind) && Settings.bCheckInstSuffix ? StripInstSuffix(Name) : FString();
		const FString Stripped = WithoutInst.IsEmpty() ? Name : WithoutInst;

		// 2. Prefix and body
		const FString Prefix = FindPrefix(Stripped, Kind);
		FString WrongPrefix;
		const EKind WrongKind = Prefix.IsEmpty() ? FindWrongKind(Stripped, Kind, WrongPrefix) : EKind::None;
		const FString Body = Stripped.Mid(Prefix.Len() + WrongPrefix.Len());
		FString FixedName;
		if (Settings.bCheckNaming)
		{
			// Getting a post process prefix: drop the old PP_ tag (M_PP_CelShadow -> PPM_CelShadow)
			FString NewBody = Body;
			if (Prefix.IsEmpty() && IsPostProcessKind(Kind) && NewBody.StartsWith(OldPostProcessTag, ESearchCase::IgnoreCase))
			{
				const FString WithoutTag = NewBody.Mid(FCString::Strlen(OldPostProcessTag));
				if (!AssetValidation::CleanNameBody(WithoutTag).IsEmpty())
				{
					NewBody = WithoutTag;
				}
			}
			const FString CleanBody = AssetValidation::CleanNameBody(NewBody);
			const FString WantedPrefix = Prefix.IsEmpty() ? GetDefaultPrefix(Asset, Kind) : Prefix;
			FixedName = CleanBody.IsEmpty() ? FString() : WantedPrefix + CleanBody;

			// No prefix we know: the first part may be another convention's (Mat_Rock): keep it, or replace it
			const FString WithoutFirst = Prefix.IsEmpty() && WrongKind == EKind::None ? AssetValidation::RemoveFirstToken(Body) : FString();
			Names[1][0] = WithoutFirst.IsEmpty() ? FString() : WantedPrefix + WithoutFirst;
		}
		else if (!WithoutInst.IsEmpty())
		{
			// Only the suffix check: still swap the parent's M_ for MI_ (PPM_ for PPMI_) so the instance doesn't take its parent's name
			FixedName = WrongKind != EKind::None && WrongKind == GetParentKind(Kind) ? GetPrefixes(Kind)[0] + Body : WithoutInst;
		}

		// 3. Issues
		Names[0][0] = FixedName;
		if (!WithoutInst.IsEmpty())
		{
			AddIssue(LOCTEXT("RuleInstSuffix", "_Inst suffix"), FixedName.IsEmpty()
				? LOCTEXT("InstSuffixNoFix", "Ends with Unreal's default _Inst suffix")
				: FText::Format(LOCTEXT("InstSuffix", "Ends with Unreal's default _Inst suffix (rename to {0})"), FText::FromString(FixedName)));
		}
		if (!Settings.bCheckNaming)
		{
			return;
		}
		const FText Rule = LOCTEXT("RuleNaming", "Naming");
		if (WrongKind != EKind::None)
		{
			AddIssue(Rule, FText::Format(LOCTEXT("WrongKindPrefix", "Named like a {0} ({1}) but this is a {2} (use {3})"),
				GetKindName(WrongKind), FText::FromString(WrongPrefix), GetKindName(Kind), FText::FromString(JoinPrefixes(Kind))));
		}
		else if (Prefix.IsEmpty())
		{
			AddIssue(Rule, FText::Format(LOCTEXT("MissingPrefix", "Name should start with {0}"), FText::FromString(JoinPrefixes(Kind))));
		}
		const FText BodyProblem = AssetValidation::GetNameBodyProblem(Body);
		if (!BodyProblem.IsEmpty())
		{
			AddIssue(Rule, BodyProblem);
		}
	}
}

#undef LOCTEXT_NAMESPACE
