// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#include "TechArtToolboxNamingSettings.h"

#include "ISettingsModule.h"
#include "Modules/ModuleManager.h"

/** The defaults: Unreal's common naming convention (T_Rock_D, SM_Rock, M_Rock, MI_Rock...). */
UTechArtToolboxNamingSettings::UTechArtToolboxNamingSettings()
{
	using EKind = ETatTextureKind;
	Suffixes =
	{
		// Color data (sRGB). Diffuse, albedo and base color are all D (the other names are aliases)
		{ TEXT("D"), EKind::Color }, { TEXT("E"), EKind::Color }, { TEXT("Emissive"), EKind::Color },
		// Normal maps
		{ TEXT("N"), EKind::Normal }, { TEXT("Normal"), EKind::Normal },
		// Packed masks (linear, several channels). CCE: Curvature/Cavity/Edge from the Mesh Maps page
		{ TEXT("ORM"), EKind::Mask }, { TEXT("MRA"), EKind::Mask }, { TEXT("RMA"), EKind::Mask },
		{ TEXT("Mask"), EKind::Mask }, { TEXT("MSK"), EKind::Mask }, { TEXT("CCE"), EKind::Mask },
		// Single-channel data (linear). S and OM: Specular and Opacity Mask from the Consolidate page
		{ TEXT("R"), EKind::Single }, { TEXT("Rough"), EKind::Single }, { TEXT("M"), EKind::Single },
		{ TEXT("Metal"), EKind::Single }, { TEXT("AO"), EKind::Single },
		{ TEXT("O"), EKind::Single }, { TEXT("Opacity"), EKind::Single },
		{ TEXT("Curvature"), EKind::Single }, { TEXT("Cavity"), EKind::Single }, { TEXT("Edge"), EKind::Single },
		{ TEXT("S"), EKind::Single }, { TEXT("Spec"), EKind::Single }, { TEXT("OM"), EKind::Single },
		// Heightmaps (linear, single channel, displacement compression)
		{ TEXT("H"), EKind::Height }, { TEXT("Height"), EKind::Height },
		// Texture Packer and Bake Smooth Normals outputs: any compression is fine
		{ TEXT("Packed"), EKind::Any }, { TEXT("SmoothN"), EKind::Any },
	};

	SuffixAliases =
	{
		{ TEXT("Diffuse"), TEXT("D") }, { TEXT("Diff"), TEXT("D") }, { TEXT("Dif"), TEXT("D") }, { TEXT("Albedo"), TEXT("D") }, { TEXT("Alb"), TEXT("D") },
		{ TEXT("BaseColor"), TEXT("D") }, { TEXT("BC"), TEXT("D") }, { TEXT("Col"), TEXT("D") }, { TEXT("Color"), TEXT("D") }, { TEXT("Colour"), TEXT("D") },
		{ TEXT("Nrm"), TEXT("N") }, { TEXT("Norm"), TEXT("N") }, { TEXT("Nor"), TEXT("N") }, { TEXT("NormalMap"), TEXT("N") }, { TEXT("NM"), TEXT("N") },
		{ TEXT("Rgh"), TEXT("R") }, { TEXT("Roughness"), TEXT("R") },
		{ TEXT("Met"), TEXT("M") }, { TEXT("Metallic"), TEXT("M") }, { TEXT("Metalness"), TEXT("M") },
		{ TEXT("Occ"), TEXT("AO") }, { TEXT("Occlusion"), TEXT("AO") }, { TEXT("AmbientOcclusion"), TEXT("AO") },
		{ TEXT("Disp"), TEXT("H") }, { TEXT("Displacement"), TEXT("H") }, { TEXT("Bump"), TEXT("H") },
		{ TEXT("Emit"), TEXT("E") }, { TEXT("Emission"), TEXT("E") }, { TEXT("Glow"), TEXT("E") },
		{ TEXT("Alpha"), TEXT("O") }, { TEXT("Opac"), TEXT("O") },
		{ TEXT("Specular"), TEXT("S") }, { TEXT("OpacityMask"), TEXT("OM") },
		{ TEXT("Masks"), TEXT("Mask") },
	};

	// What the VAT page and AnimToTexture name their textures
	VertexAnimationNameParts = { TEXT("BonePos"), TEXT("BoneRot"), TEXT("BoneWeights"), TEXT("VertexPos"), TEXT("VertexNormal") };

	StaticMeshPrefixes = { TEXT("SM_") };
	SkeletalMeshPrefixes = { TEXT("SK_"), TEXT("SKM_") };

	MaterialPrefixes = { TEXT("M_") };
	MaterialInstancePrefixes = { TEXT("MI_") };
	MaterialFunctionPrefixes = { TEXT("MF_") };
	MaterialLayerPrefixes = { TEXT("ML_") };
	PostProcessMaterialPrefixes = { TEXT("PPM_") };
	PostProcessInstancePrefixes = { TEXT("PPMI_") };
}

void UTechArtToolboxNamingSettings::ShowInProjectSettings()
{
	if (ISettingsModule* Settings = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
	{
		const UTechArtToolboxNamingSettings& Naming = Get();
		Settings->ShowViewer(Naming.GetContainerName(), Naming.GetCategoryName(), Naming.GetSectionName());
	}
}

TArray<FString> UTechArtToolboxNamingSettings::NonEmpty(const TArray<FString>& List)
{
	TArray<FString> Result;
	for (const FString& Entry : List)
	{
		const FString Trimmed = Entry.TrimStartAndEnd();
		if (!Trimmed.IsEmpty())
		{
			Result.AddUnique(Trimmed);
		}
	}
	return Result;
}

FString UTechArtToolboxNamingSettings::First(const TArray<FString>& List)
{
	const TArray<FString> Entries = NonEmpty(List);
	return Entries.Num() > 0 ? Entries[0] : FString();
}

bool UTechArtToolboxNamingSettings::HasPrefix(const FString& Name, const FString& Prefix)
{
	return !Prefix.IsEmpty() && Name.StartsWith(Prefix, ESearchCase::CaseSensitive);
}

FString UTechArtToolboxNamingSettings::FindPrefix(const FString& Name, const TArray<FString>& Prefixes)
{
	FString Best;
	for (const FString& Prefix : NonEmpty(Prefixes))
	{
		if (Prefix.Len() > Best.Len() && HasPrefix(Name, Prefix))
		{
			Best = Prefix;
		}
	}
	return Best;
}

FString UTechArtToolboxNamingSettings::CleanSuffix(const FString& Suffix)
{
	FString Result = Suffix.TrimStartAndEnd();
	Result.RemoveFromStart(TEXT("_"));
	return Result;
}

const FTatTextureSuffix* UTechArtToolboxNamingSettings::FindSuffix(const FString& Suffix) const
{
	const FString Wanted = CleanSuffix(Suffix);
	if (Wanted.IsEmpty())
	{
		return nullptr;
	}
	return Suffixes.FindByPredicate([&Wanted](const FTatTextureSuffix& Rule) { return CleanSuffix(Rule.Suffix).Equals(Wanted, ESearchCase::IgnoreCase); });
}

FString UTechArtToolboxNamingSettings::FindAlias(const FString& Suffix) const
{
	const FString Wanted = CleanSuffix(Suffix);
	if (Wanted.IsEmpty())
	{
		return FString();
	}
	const FTatSuffixAlias* Alias = SuffixAliases.FindByPredicate([&Wanted](const FTatSuffixAlias& Entry) { return CleanSuffix(Entry.Alias).Equals(Wanted, ESearchCase::IgnoreCase); });
	return Alias ? CleanSuffix(Alias->Suffix) : FString();
}
