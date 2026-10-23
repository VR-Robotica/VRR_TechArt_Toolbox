// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Material validation: checks material assets against the project's naming rules.
 *
 * Strategy (like the mesh validator)
 *  - The prefix says what the asset is: M_ material (master material), MI_ material instance,
 *    MF_ or ML_ material function (ML_ for material layers and layer blends, by habit, but either is accepted).
 *  - If the name carries another material kind's prefix, say which one it should be (clearer than "missing prefix").
 *  - After the prefix: letters, digits and single underscores (AssetValidation::GetNameBodyProblem).
 *  - Instances shouldn't keep the _Inst suffix Unreal gives a new instance (M_Rock_Inst).
 *  - Every naming issue carries the one name that fixes them all (M_Rock_Inst -> MI_Rock) for Fix All. An unknown
 *    prefix is kept or replaced, as the person fixing chooses (Mat_Rock -> M_Mat_Rock or M_Rock).
 *  - Only the asset's class and name are needed to check, so nothing is loaded.
 */

#include "Core_MaterialValidator.h"

#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialInstance.h"

#define LOCTEXT_NAMESPACE "MaterialValidator"

namespace MaterialValidator
{
	using ESeverity = FAssetIssue::ESeverity;

	enum class EKind : uint8 { None, Material, Instance, Function };

	/** Prefixes each kind may start with (case-sensitive, as the convention is). */
	const TCHAR* const MaterialPrefixes[] = { TEXT("M_") };
	const TCHAR* const InstancePrefixes[] = { TEXT("MI_") };
	const TCHAR* const FunctionPrefixes[] = { TEXT("MF_"), TEXT("ML_") };

	EKind GetKind(const FAssetData& Asset)
	{
		if (Asset.IsInstanceOf(UMaterial::StaticClass()))
		{
			return EKind::Material;
		}
		if (Asset.IsInstanceOf(UMaterialInstance::StaticClass()))
		{
			return EKind::Instance;
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
		return FString::Printf(TEXT("%s (Material), %s (Material Instance), %s (Material Function)"),
			*JoinPrefixes(EKind::Material), *JoinPrefixes(EKind::Instance), *JoinPrefixes(EKind::Function));
	}

	bool IsMaterialAsset(const FAssetData& Asset)
	{
		return GetKind(Asset) != EKind::None;
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
		for (const EKind Other : { EKind::Material, EKind::Instance, EKind::Function })
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
		const FString WithoutInst = Kind == EKind::Instance && Settings.bCheckInstSuffix ? StripInstSuffix(Name) : FString();
		const FString Stripped = WithoutInst.IsEmpty() ? Name : WithoutInst;

		// 2. Prefix and body
		const FString Prefix = FindPrefix(Stripped, Kind);
		FString WrongPrefix;
		const EKind WrongKind = Prefix.IsEmpty() ? FindWrongKind(Stripped, Kind, WrongPrefix) : EKind::None;
		const FString Body = Stripped.Mid(Prefix.Len() + WrongPrefix.Len());
		FString FixedName;
		if (Settings.bCheckNaming)
		{
			const FString CleanBody = AssetValidation::CleanNameBody(Body);
			const FString WantedPrefix = Prefix.IsEmpty() ? GetDefaultPrefix(Asset, Kind) : Prefix;
			FixedName = CleanBody.IsEmpty() ? FString() : WantedPrefix + CleanBody;

			// No prefix we know: the first part may be another convention's (Mat_Rock): keep it, or replace it
			const FString WithoutFirst = Prefix.IsEmpty() && WrongKind == EKind::None ? AssetValidation::RemoveFirstToken(Body) : FString();
			Names[1][0] = WithoutFirst.IsEmpty() ? FString() : WantedPrefix + WithoutFirst;
		}
		else if (!WithoutInst.IsEmpty())
		{
			// Only the suffix check: still swap the parent's M_ for MI_ so the instance doesn't take its parent's name
			FixedName = WrongKind == EKind::Material ? InstancePrefixes[0] + Body : WithoutInst;
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
