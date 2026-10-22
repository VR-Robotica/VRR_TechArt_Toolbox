// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Texture validation: checks texture assets against the project's texture rules.
 *
 * Strategy
 *  - The name suffix says what a texture holds (T_Rock_N = normal map, T_Rock_ORM = packed mask...).
 *    SuffixRules maps each known suffix to a kind (color, normal, mask, single channel).
 *  - Each check compares the texture's real settings with what its kind needs: sRGB only for color data,
 *    a compression that fits the data, a sensible size, and mips/streaming for anything that isn't UI.
 *  - Problems are collected as FTextureIssue rows (error or warning) for the Validate page to list.
 *    Errors are things that render wrong (sRGB on a normal map); warnings are style or cost issues.
 */

#include "Core_TextureValidator.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Misc/ScopedSlowTask.h"

#define LOCTEXT_NAMESPACE "TextureValidator"

namespace TextureValidator
{
	/** What a name suffix says the texture holds. Any = accept any settings (tool outputs with their own rules). */
	enum class EKind : uint8 { Color, Normal, Mask, Single, Any };

	/** One known suffix and the kind of data it means. */
	struct FSuffixRule
	{
		const TCHAR* Suffix;
		EKind Kind;
	};

	/** Every suffix the validator knows. Add new suffixes here; the Validate page tooltip lists them. */
	const FSuffixRule SuffixRules[] =
	{
		// Color data (sRGB)
		{ TEXT("D"), EKind::Color }, { TEXT("BC"), EKind::Color }, { TEXT("Albedo"), EKind::Color },
		{ TEXT("Diffuse"), EKind::Color }, { TEXT("E"), EKind::Color }, { TEXT("Emissive"), EKind::Color },
		// Normal maps
		{ TEXT("N"), EKind::Normal }, { TEXT("Normal"), EKind::Normal },
		// Packed masks (linear, several channels)
		{ TEXT("ORM"), EKind::Mask }, { TEXT("MRA"), EKind::Mask }, { TEXT("RMA"), EKind::Mask },
		{ TEXT("Mask"), EKind::Mask }, { TEXT("MSK"), EKind::Mask }, { TEXT("CCE"), EKind::Mask },	// CCE: Curvature/Cavity/Edge (Toolbox > Mesh Maps)
		// Single-channel data (linear)
		{ TEXT("R"), EKind::Single }, { TEXT("Rough"), EKind::Single }, { TEXT("M"), EKind::Single },
		{ TEXT("Metal"), EKind::Single }, { TEXT("AO"), EKind::Single }, { TEXT("H"), EKind::Single },
		{ TEXT("Height"), EKind::Single }, { TEXT("O"), EKind::Single }, { TEXT("Opacity"), EKind::Single },
		{ TEXT("Curvature"), EKind::Single }, { TEXT("Cavity"), EKind::Single }, { TEXT("Edge"), EKind::Single },
		// Texture Packer output: any compression is fine
		{ TEXT("Packed"), EKind::Any },
		{ TEXT("SmoothN"), EKind::Any },
	};

	/** The rule for a suffix (case-insensitive), or null for an unknown one. */
	const FSuffixRule* FindSuffixRule(const FString& Suffix)
	{
		for (const FSuffixRule& Rule : SuffixRules)
		{
			if (Suffix.Equals(Rule.Suffix, ESearchCase::IgnoreCase))
			{
				return &Rule;
			}
		}
		return nullptr;
	}

	/** "T_Rock_N_01" -> "N" (strips the T_ prefix and a trailing number). Empty when there is no suffix. */
	FString GetNameSuffix(FString Name)
	{
		Name.RemoveFromStart(TEXT("T_"));

		int32 Underscore;
		if (Name.FindLastChar(TEXT('_'), Underscore) && Name.Mid(Underscore + 1).IsNumeric())
		{
			Name.LeftInline(Underscore);
		}

		return Name.FindLastChar(TEXT('_'), Underscore) ? Name.Mid(Underscore + 1) : FString();
	}

	/** Display name of a compression setting, as the texture editor shows it. */
	FText GetCompressionName(TextureCompressionSettings Compression)
	{
		return StaticEnum<TextureCompressionSettings>()->GetDisplayNameTextByValue(Compression);
	}

	/** Texture groups meant for normal maps (they get normal-map-friendly mip filtering and LOD bias). */
	bool IsNormalMapGroup(TextureGroup Group)
	{
		return Group == TEXTUREGROUP_WorldNormalMap || Group == TEXTUREGROUP_CharacterNormalMap
			|| Group == TEXTUREGROUP_WeaponNormalMap || Group == TEXTUREGROUP_VehicleNormalMap;
	}

	/** "_D, _BC, ..." built from SuffixRules, for tooltips. */
	FString GetKnownSuffixList()
	{
		TArray<FString> Names;
		for (const FSuffixRule& Rule : SuffixRules)
		{
			Names.Add(FString(TEXT("_")) + Rule.Suffix);
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** Asks the Asset Registry for every UTexture2D (and subclasses) under a folder, without loading them. */
	TArray<FAssetData> FindTextures(const FString& Folder, bool bIncludeSubfolders)
	{
		FARFilter Filter;
		Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = bIncludeSubfolders;

		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, Assets);
		return Assets;
	}

	/** Keeps only the textures, e.g. from a mixed Content Browser selection. */
	TArray<FAssetData> FilterTextures(const TArray<FAssetData>& Assets)
	{
		return Assets.FilterByPredicate([](const FAssetData& Asset)
		{
			return Asset.IsInstanceOf(UTexture2D::StaticClass());
		});
	}

	/**
	 * Validates a list of textures:
	 *  1. load and check each one (cancelable, since big folders take a while to load),
	 *  2. count errors and warnings,
	 *  3. sort so errors come first.
	 */
	FTextureValidationResult Validate(const TArray<FAssetData>& Assets, const FTextureValidatorSettings& Settings)
	{
		FTextureValidationResult Result;

		// 1. Check each texture. Loading is the slow part; the user can cancel and keep what was found so far
		FScopedSlowTask SlowTask(Assets.Num(), LOCTEXT("Validating", "Validating textures..."));
		SlowTask.MakeDialog(true);

		for (const FAssetData& Asset : Assets)
		{
			if (SlowTask.ShouldCancel())
			{
				break;
			}
			SlowTask.EnterProgressFrame(1.0f, FText::FromName(Asset.AssetName));

			if (const UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
			{
				ValidateTexture(Asset, Texture, Settings, Result.Issues);
				++Result.NumScanned;
			}
		}

		// 2. Totals for the summary line
		for (const FTextureIssue& Issue : Result.Issues)
		{
			(Issue.Severity == FTextureIssue::ESeverity::Error ? Result.NumErrors : Result.NumWarnings)++;
		}

		// 3. Errors first, then by texture name
		Result.Issues.StableSort([](const FTextureIssue& A, const FTextureIssue& B)
		{
			if (A.Severity != B.Severity)
			{
				return A.Severity == FTextureIssue::ESeverity::Error;
			}
			return A.Asset.AssetName.LexicalLess(B.Asset.AssetName);
		});
		return Result;
	}

	/**
	 * Runs every enabled check on one texture. The suffix is looked up once and shared by the checks:
	 * naming, settings (sRGB + compression), resolution, then mips and streaming.
	 */
	void ValidateTexture(const FAssetData& Asset, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, TArray<FTextureIssue>& OutIssues)
	{
		using ESeverity = FTextureIssue::ESeverity;

		auto AddIssue = [&Asset, &OutIssues](ESeverity Severity, const FText& Rule, const FText& Message)
		{
			OutIssues.Add({ Asset, Severity, Rule, Message });
		};

		const FString Name = Asset.AssetName.ToString();
		// What the name says this texture holds (null rule = unknown suffix)
		const FString Suffix = GetNameSuffix(Name);
		const FSuffixRule* SuffixRule = FindSuffixRule(Suffix);

		// NAMING
		if (Settings.bCheckNaming)
		{
			const FText Rule = LOCTEXT("RuleNaming", "Naming");
			if (!Name.StartsWith(TEXT("T_")))
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("MissingPrefix", "Name should start with T_"));
			}
			if (!SuffixRule)
			{
				AddIssue(ESeverity::Warning, Rule, Suffix.IsEmpty()
					? LOCTEXT("MissingSuffix", "Name has no type suffix (e.g. _D, _N, _ORM)")
					: FText::Format(LOCTEXT("UnknownSuffix", "Unknown suffix _{0}"), FText::FromString(Suffix)));
			}
		}

		// SETTINGS MATCH SUFFIX
		if (Settings.bCheckSettings && SuffixRule && SuffixRule->Kind != EKind::Any)
		{
			const FText Rule = LOCTEXT("RuleSettings", "Settings");
			const TextureCompressionSettings Compression = Texture->CompressionSettings;

			// Color data is stored in sRGB; everything else (normals, masks, single channels) must be linear,
			// or the GPU "decodes" values that were never encoded and the data comes out wrong.
			const bool bWantsSRGB = SuffixRule->Kind == EKind::Color;

			if (Texture->SRGB != bWantsSRGB)
			{
				AddIssue(ESeverity::Error, Rule, bWantsSRGB
					? LOCTEXT("NeedsSRGB", "Color texture should have sRGB on")
					: LOCTEXT("NoSRGB", "Data texture (normal/mask) should have sRGB off"));
			}

			// Compressions that keep this kind of data intact. A wrong one on a normal map breaks shading (error);
			// for the others it mostly wastes memory or quality (warning).
			TArray<TextureCompressionSettings> Allowed;
			switch (SuffixRule->Kind)
			{
			case EKind::Color:	Allowed = { TC_Default, TC_BC7, TC_HDR }; break;
			case EKind::Normal:	Allowed = { TC_Normalmap }; break;
			case EKind::Mask:	Allowed = { TC_Masks, TC_Default, TC_BC7 }; break;
			case EKind::Single:	Allowed = { TC_Grayscale, TC_Masks, TC_Alpha, TC_Displacementmap, TC_Default }; break;
			default: break;
			}

			if (!Allowed.Contains(Compression))
			{
				TArray<FString> AllowedNames;
				for (TextureCompressionSettings Option : Allowed)
				{
					AllowedNames.Add(GetCompressionName(Option).ToString());
				}

				AddIssue(SuffixRule->Kind == EKind::Normal ? ESeverity::Error : ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("WrongCompression", "Compression is {0}, expected {1} for _{2}"),
						GetCompressionName(Compression), FText::FromString(FString::Join(AllowedNames, TEXT(" or "))), FText::FromString(Suffix)));
			}
		}

		// RESOLUTION (source size, so it's what was imported)
		if (Settings.bCheckResolution && Texture->Source.IsValid())
		{
			const FText Rule = LOCTEXT("RuleResolution", "Resolution");
			const int32 SizeX = Texture->Source.GetSizeX();
			const int32 SizeY = Texture->Source.GetSizeY();
			const FText SizeText = FText::FromString(FString::Printf(TEXT("%d x %d"), SizeX, SizeY));

			// Non-power-of-two textures get no mips and can't stream
			if (!FMath::IsPowerOfTwo(SizeX) || !FMath::IsPowerOfTwo(SizeY))
			{
				AddIssue(ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("NotPowerOfTwo", "{0} is not a power of two (no mips or streaming)"), SizeText));
			}

			// A Maximum Texture Size setting at or under the limit caps it in game, so that's fine
			const bool bCapped = Texture->MaxTextureSize > 0 && Texture->MaxTextureSize <= Settings.MaxSize;
			if (FMath::Max(SizeX, SizeY) > Settings.MaxSize && !bCapped)
			{
				AddIssue(ESeverity::Error, Rule,
					FText::Format(LOCTEXT("TooLarge", "{0} is larger than the {1} max"), SizeText, FText::AsNumber(Settings.MaxSize, &FNumberFormattingOptions::DefaultNoGrouping())));
			}
		}

		// MIPS & STREAMING (UI textures normally skip both)
		if (Settings.bCheckMips && Texture->LODGroup != TEXTUREGROUP_UI)
		{
			const FText Rule = LOCTEXT("RuleMips", "Mips");

			if (Texture->MipGenSettings == TMGS_NoMipmaps)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NoMips", "Has no mipmaps (shimmers at a distance, can't stream)"));
			}
			if (Texture->NeverStream)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NeverStream", "Never Stream is on (always fully in memory)"));
			}

			// Normal maps are recognized by suffix or by their compression, whichever says so
			const bool bIsNormalMap = (SuffixRule && SuffixRule->Kind == EKind::Normal) || Texture->CompressionSettings == TC_Normalmap;
			if (bIsNormalMap && !IsNormalMapGroup(Texture->LODGroup))
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NormalGroup", "Normal map should use a NormalMap texture group"));
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
