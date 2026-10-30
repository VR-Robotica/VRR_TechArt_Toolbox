// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Texture validation: checks texture assets against the project's texture rules.
 *
 * Every prefix and suffix comes from Project Settings > Plugins > TechArt Toolbox Asset Naming
 * (UTechArtToolboxNamingSettings); the names below are its defaults.
 *
 * Strategy
 *  - The name suffix says what a texture holds (T_Rock_N = normal map, T_Rock_ORM = packed mask...).
 *    The naming settings' suffix table maps each known suffix to a kind (color, normal, mask, single channel).
 *  - Each check compares the texture's real settings with what its kind needs: sRGB only for color data,
 *    a compression that fits the data, a sensible size, and mips/streaming for anything that isn't UI.
 *  - Curve atlases (CA_) are linear lookup tables and get their own rules: sRGB off, uncompressed, no mips,
 *    any height (one row per curve).
 *  - Vertex animation textures (VAT_, or a T_ texture with a VAT bake suffix such as _BonePos) hold exact
 *    positions/rotations/weights read per texel: sRGB off, HDR compression, Nearest filter, no mips, any size.
 *  - Heightmaps (_H) are linear single-channel data. Displacementmap compression reads the source's A channel and
 *    Grayscale reads R, so the expected compression depends on where the height is stored: Displacementmap when
 *    A holds it, Grayscale when only R does (Displacementmap on such a texture would come out flat).
 *  - Diffuse, albedo and base color all use _D; the other names (_Albedo, _BaseColor, _BC...) are aliases that get
 *    renamed. _A is ambiguous (albedo or alpha), so it's flagged and renamed by its settings (sRGB -> _D, else _O).
 *  - Virtual textures (Virtual Texture Streaming on) start with VT_ instead of T_. They need what the engine needs to
 *    build them as VT: virtual texturing on for the project, power-of-two size (or padding), mips, and not the
 *    ColorLookupTable group. They skip the max size and Never Stream checks (VT streams tiles, and big is the point),
 *    and get a warning when they're too small to gain anything.
 *  - Problems are collected as FAssetIssue rows (error or warning) for the Asset Validation page to list.
 *    Errors are things that render wrong (sRGB on a normal map); warnings are style or cost issues.
 *  - Fix All: a missing T_ / VT_ / CA_ / VAT_ prefix is added by renaming, and a missing or unknown suffix gets the one
 *    its old suffix or settings point to (_Diff -> _D, normal map compression -> _N). An unknown prefix or suffix is
 *    kept or replaced, as the person fixing chooses (Tex_Rock_Diff -> T_Tex_Rock_Diff_D or T_Rock_D); sRGB, compression, max size, mips, streaming,
 *    texture group, virtual texture streaming and power-of-two padding are set to what the check expects.
 *    An unknown suffix, a non-power-of-two regular texture or a project without VT needs a person.
 */

#include "Core_TextureValidator.h"

#include "Curves/CurveLinearColorAtlas.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "TechArtToolboxNamingSettings.h"

#define LOCTEXT_NAMESPACE "TextureValidator"

namespace TextureValidator
{
	using EKind = ETatTextureKind;
	using FSuffixRule = FTatTextureSuffix;
	using FNaming = UTechArtToolboxNamingSettings;

	/** The project's naming convention (Project Settings > Plugins > TechArt Toolbox Asset Naming). */
	const FNaming& Naming()
	{
		return FNaming::Get();
	}

	/** Takes Prefix off the start of Name if it's there (never for an empty prefix). */
	bool RemovePrefix(FString& Name, const FString& Prefix, ESearchCase::Type Case = ESearchCase::CaseSensitive)
	{
		if (Prefix.IsEmpty() || !Name.StartsWith(Prefix, Case))
		{
			return false;
		}
		Name.RightChopInline(Prefix.Len());
		return true;
	}

	/** The rule for a suffix (case-insensitive), or null for an unknown one. */
	const FSuffixRule* FindSuffixRule(const FString& Suffix)
	{
		return Naming().FindSuffix(Suffix);
	}

	/** A suffix setting as it goes into names ("D"). */
	FString Clean(const FString& Suffix)
	{
		return FNaming::CleanSuffix(Suffix);
	}

	/** "T_Rock_N_01" or "VT_Rock_N_01" -> "N" (strips the prefix and a trailing number). Empty when there is no suffix. */
	FString GetNameSuffix(FString Name)
	{
		if (!RemovePrefix(Name, Naming().VirtualTexturePrefix))
		{
			RemovePrefix(Name, Naming().TexturePrefix, ESearchCase::IgnoreCase);
		}

		int32 Underscore;
		if (Name.FindLastChar(TEXT('_'), Underscore) && Name.Mid(Underscore + 1).IsNumeric())
		{
			Name.LeftInline(Underscore);
		}

		return Name.FindLastChar(TEXT('_'), Underscore) ? Name.Mid(Underscore + 1) : FString();
	}

	/**
	 * The known suffix a texture should get: what its unknown suffix is an alias of, else what its settings say
	 * (normal map compression -> the normal suffix, sRGB color -> the color suffix). Empty when there's no safe guess.
	 */
	FString GuessSuffix(const FString& UnknownSuffix, const UTexture2D* Texture)
	{
		const FNaming& Names = Naming();
		const FString Alias = Names.FindAlias(UnknownSuffix);
		if (!Alias.IsEmpty())
		{
			return Alias;
		}
		// The ambiguous suffix (A: albedo or alpha): color when it's sRGB, else opacity
		const FString Ambiguous = Clean(Names.AmbiguousSuffix);
		if (!Ambiguous.IsEmpty() && UnknownSuffix.Equals(Ambiguous, ESearchCase::IgnoreCase))
		{
			return Clean(Texture->SRGB ? Names.ColorSuffix : Names.OpacitySuffix);
		}
		if (Texture->CompressionSettings == TC_Normalmap)
		{
			return Clean(Names.NormalSuffix);
		}
		if (Texture->SRGB && (Texture->CompressionSettings == TC_Default || Texture->CompressionSettings == TC_BC7))
		{
			return Clean(Names.ColorSuffix);
		}
		return FString();
	}

	/**
	 * Candidate names for a texture, [replace unknown prefix][replace unknown suffix] (empty = doesn't apply):
	 *  1. take off a known prefix (T_ or VT_) and a trailing number, split the rest at _,
	 *  2. an unknown prefix is the first part, when nothing known came off and there's more after it,
	 *  3. an unknown suffix is the last part (when there are two or more); NewSuffix is added after it or put in its place,
	 *  4. Prefix + the parts (cleaned) + the number.
	 * Tex_Rock_Diff_01 with NewSuffix D -> T_Tex_Rock_Diff_D_01, T_Rock_Diff_D_01, T_Tex_Rock_D_01, T_Rock_D_01.
	 */
	void MakeTextureNames(const FString& Name, const FString& Prefix, const FString& NewSuffix, FString (&OutNames)[2][2])
	{
		// 1. Known prefix, number, parts
		FString Rest = Name;
		const bool bKnownPrefix = RemovePrefix(Rest, Naming().VirtualTexturePrefix) || RemovePrefix(Rest, Naming().TexturePrefix, ESearchCase::IgnoreCase);
		FString Number;
		int32 Underscore;
		if (Rest.FindLastChar(TEXT('_'), Underscore) && Rest.Mid(Underscore + 1).IsNumeric())
		{
			Number = Rest.Mid(Underscore);
			Rest.LeftInline(Underscore);
		}
		TArray<FString> Parts;
		Rest.ParseIntoArray(Parts, TEXT("_"), true);
		// Rock_N: the first part is the name, not a prefix (taking it off would leave just the suffix)
		const int32 MinPartsForPrefix = Parts.Num() >= 2 && FindSuffixRule(Parts.Last()) ? 3 : 2;

		for (int32 bReplacePrefix = 0; bReplacePrefix < 2; ++bReplacePrefix)
		{
			for (int32 bReplaceSuffix = 0; bReplaceSuffix < 2; ++bReplaceSuffix)
			{
				FString& Out = OutNames[bReplacePrefix][bReplaceSuffix];
				Out.Reset();
				TArray<FString> Result = Parts;

				// 2. Unknown prefix
				if (bReplacePrefix)
				{
					if (bKnownPrefix || Result.Num() < MinPartsForPrefix)
					{
						continue;
					}
					Result.RemoveAt(0);
				}

				// 3. Suffix (the last of two or more parts)
				const bool bKnownSuffix = Result.Num() >= 2 && FindSuffixRule(Result.Last());
				if (bKnownSuffix || NewSuffix.IsEmpty())
				{
					if (bReplaceSuffix)
					{
						continue;	// nothing to replace with, or nothing to replace
					}
				}
				else if (bReplaceSuffix)
				{
					if (Result.Num() < 2)
					{
						continue;	// no suffix to replace: only adding applies
					}
					Result.Last() = NewSuffix;
				}
				else
				{
					Result.Add(NewSuffix);
				}

				// 4. Something must be left before the suffix
				const bool bHasSuffix = bKnownSuffix || !NewSuffix.IsEmpty();
				const FString Body = AssetValidation::CleanNameBody(FString::Join(Result, TEXT("_")));
				if (Result.Num() < (bHasSuffix ? 2 : 1) || Body.IsEmpty())
				{
					continue;
				}
				Out = Prefix + Body + Number;
			}
		}
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

	/** "_D, _N, ..." from the naming settings, for tooltips. */
	FString GetKnownSuffixList()
	{
		TArray<FString> Names;
		for (const FSuffixRule& Rule : Naming().Suffixes)
		{
			const FString Suffix = Clean(Rule.Suffix);
			if (!Suffix.IsEmpty())
			{
				Names.AddUnique(TEXT("_") + Suffix);
			}
		}
		return FString::Join(Names, TEXT(", "));
	}

	/** A Fix All settings change for a texture. */
	template <typename FEdit>
	TFunction<void(UObject*)> EditTexture(FEdit Edit)
	{
		return [Edit](UObject* Object)
		{
			if (UTexture2D* Texture = Cast<UTexture2D>(Object))
			{
				Edit(*Texture);
			}
		};
	}

	/**
	 * Curve atlases: each row is a color curve sampled across the width, read as exact linear values.
	 *  - Naming: CA_ prefix, no type suffix needed (the prefix says what it is).
	 *  - Settings: sRGB off (error: the curve values would be gamma-decoded), uncompressed HDR or another
	 *    uncompressed format (block compression bands and bleeds between rows).
	 *  - Resolution: height is the number of curves, so only the width needs to be a power of two; max size as usual.
	 *  - Mips: none (lower mips average neighbouring curves together), so "no mips" and Never Stream are expected.
	 * Unreal's Curve Atlas asset starts with these settings (TC_HDR, RGBA16F, no mips, sRGB off).
	 */
	template <typename FAddIssue>
	void ValidateCurveAtlas(const FString& Name, bool bIsAtlasAsset, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, FAddIssue& AddIssue)
	{
		using ESeverity = FAssetIssue::ESeverity;

		const FString& AtlasPrefix = Naming().CurveAtlasPrefix;
		if (Settings.bCheckNaming && bIsAtlasAsset && !AtlasPrefix.IsEmpty() && !FNaming::HasPrefix(Name, AtlasPrefix))
		{
			AddIssue(ESeverity::Warning, LOCTEXT("RuleNaming", "Naming"), FText::Format(LOCTEXT("AtlasPrefix", "Curve Atlas should start with {0}"), FText::FromString(AtlasPrefix)))
				.RenameTo = AtlasPrefix + Name;
		}

		if (Settings.bCheckSettings)
		{
			const FText Rule = LOCTEXT("RuleSettings", "Settings");
			if (Texture->SRGB)
			{
				AddIssue(ESeverity::Error, Rule, LOCTEXT("AtlasSRGB", "Curve atlas should have sRGB off (its values are linear)")).FixSettings =
					EditTexture([](UTexture2D& Fix) { Fix.SRGB = false; });
			}
			const TArray<TextureCompressionSettings> Allowed = { TC_HDR, TC_HDR_F32, TC_VectorDisplacementmap };
			if (!Allowed.Contains(Texture->CompressionSettings))
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("AtlasCompression", "Compression is {0}, expected {1}, {2} or {3} (uncompressed) for a curve atlas"),
					GetCompressionName(Texture->CompressionSettings), GetCompressionName(TC_HDR), GetCompressionName(TC_HDR_F32), GetCompressionName(TC_VectorDisplacementmap)))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.CompressionSettings = TC_HDR; });
			}
		}

		if (Settings.bCheckResolution && Texture->Source.IsValid())
		{
			const FText Rule = LOCTEXT("RuleResolution", "Resolution");
			const int32 SizeX = static_cast<int32>(Texture->Source.GetSizeX());
			const int32 SizeY = static_cast<int32>(Texture->Source.GetSizeY());
			if (!FMath::IsPowerOfTwo(SizeX))
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("AtlasWidth", "Width {0} is not a power of two"), SizeX));
			}
			if (FMath::Max(SizeX, SizeY) > Settings.MaxSize && !(Texture->MaxTextureSize > 0 && Texture->MaxTextureSize <= Settings.MaxSize))
			{
				AddIssue(ESeverity::Error, Rule, FText::Format(LOCTEXT("AtlasTooLarge", "{0} x {1} is larger than the {2} max"),
					SizeX, SizeY, FText::AsNumber(Settings.MaxSize, &FNumberFormattingOptions::DefaultNoGrouping())))
					.FixSettings = EditTexture([MaxSize = Settings.MaxSize](UTexture2D& Fix) { Fix.MaxTextureSize = MaxSize; });
			}
		}

		if (Settings.bCheckMips && Texture->MipGenSettings != TMGS_NoMipmaps)
		{
			AddIssue(ESeverity::Warning, LOCTEXT("RuleMips", "Mips"), LOCTEXT("AtlasMips", "Curve atlas has mipmaps (lower mips blend neighbouring curves); set Mip Gen Settings to NoMipmaps"))
				.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.MipGenSettings = TMGS_NoMipmaps; });
		}
	}

	/** Whether a name (without its prefix) has a VAT bake part, such as Gideon_BonePos or Gideon_VertexPos_LOD0. */
	bool HasVATSuffix(const FString& Name)
	{
		TArray<FString> Parts;
		Name.ParseIntoArray(Parts, TEXT("_"), true);
		for (const FString& Part : Parts)
		{
			for (const FString& Suffix : Naming().VertexAnimationNameParts)
			{
				if (!Suffix.IsEmpty() && Part.Equals(Suffix.TrimStartAndEnd(), ESearchCase::IgnoreCase))
				{
					return true;
				}
			}
		}
		return false;
	}

	/**
	 * Vertex animation textures: positions, rotations, normals or bone weights, one texel per vertex/bone per frame.
	 *  - Naming: VAT_ prefix (T_Gideon_BonePos -> VAT_Gideon_BonePos); the bake part says what it holds, no type suffix needed.
	 *  - Settings: sRGB off (error: the values would be gamma-decoded), HDR compression (16 or 32-bit float, uncompressed).
	 *  - Filter: Nearest (bilinear blends neighbouring vertices or frames).
	 *  - Mips: none (lower mips average vertices together).
	 * No resolution checks: the size follows vertex and frame counts, and capping it would cut data off.
	 */
	template <typename FAddIssue>
	void ValidateVAT(const FString& Name, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, FAddIssue& AddIssue)
	{
		using ESeverity = FAssetIssue::ESeverity;

		const FString& VATPrefix = Naming().VertexAnimationPrefix;
		if (Settings.bCheckNaming && !VATPrefix.IsEmpty() && !FNaming::HasPrefix(Name, VATPrefix))
		{
			FString Body = Name;
			if (!RemovePrefix(Body, Naming().TexturePrefix, ESearchCase::IgnoreCase))
			{
				RemovePrefix(Body, Naming().VirtualTexturePrefix, ESearchCase::IgnoreCase);
			}
			AddIssue(ESeverity::Warning, LOCTEXT("RuleNaming", "Naming"), FText::Format(LOCTEXT("VATPrefix", "Vertex animation texture should start with {0}"), FText::FromString(VATPrefix)))
				.RenameTo = VATPrefix + AssetValidation::CleanNameBody(Body);
		}

		if (Settings.bCheckSettings)
		{
			const FText Rule = LOCTEXT("RuleSettings", "Settings");
			if (Texture->SRGB)
			{
				AddIssue(ESeverity::Error, Rule, LOCTEXT("VATSRGB", "Vertex animation texture should have sRGB off (its values are linear data)")).FixSettings =
					EditTexture([](UTexture2D& Fix) { Fix.SRGB = false; });
			}
			if (Texture->CompressionSettings != TC_HDR && Texture->CompressionSettings != TC_HDR_F32)
			{
				AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("VATCompression", "Compression is {0}, expected {1} or {2} for a vertex animation texture"),
					GetCompressionName(Texture->CompressionSettings), GetCompressionName(TC_HDR), GetCompressionName(TC_HDR_F32)))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.CompressionSettings = TC_HDR; });
			}
			if (Texture->Filter != TF_Nearest)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("VATFilter", "Vertex animation texture should use Nearest filtering (others blend neighbouring vertices or frames)"))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.Filter = TF_Nearest; });
			}
		}

		if (Settings.bCheckMips && Texture->MipGenSettings != TMGS_NoMipmaps)
		{
			AddIssue(ESeverity::Warning, LOCTEXT("RuleMips", "Mips"), LOCTEXT("VATMips", "Vertex animation texture has mipmaps (lower mips blend vertices together); set Mip Gen Settings to NoMipmaps"))
				.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.MipGenSettings = TMGS_NoMipmaps; });
		}
	}

	/**
	 * Whether the source's alpha channel holds data (exists and isn't fully opaque).
	 * Reads the source image when the format has alpha, so it's only asked for heightmaps.
	 */
	bool HasAlphaData(const UTexture2D* Texture)
	{
		if (!Texture->Source.IsValid())
		{
			return false;
		}
		const ETextureSourceFormat Format = Texture->Source.GetFormat();
		if (Format == TSF_G8 || Format == TSF_G16 || Format == TSF_R16F || Format == TSF_R32F || Format == TSF_BGRE8)
		{
			return false;
		}

		FImage Image;
		return const_cast<FTextureSource&>(Texture->Source).GetMipImage(Image, 0) && FImageCore::DetectAlphaChannel(Image);
	}

	/**
	 * Runs every enabled check on one texture. Curve atlases go to ValidateCurveAtlas. The suffix is looked up once and shared by the checks:
	 * naming, settings (sRGB + compression), resolution, then mips and streaming.
	 */
	void ValidateTexture(const FAssetData& Asset, const UTexture2D* Texture, const FTextureValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues)
	{
		using ESeverity = FAssetIssue::ESeverity;

		// Returns the issue so a fix can be attached
		auto AddIssue = [&Asset, &OutIssues](ESeverity Severity, const FText& Rule, const FText& Message) -> FAssetIssue&
		{
			return OutIssues.Add_GetRef({ Asset, Severity, FAssetIssue::EAssetType::Texture, Rule, Message });
		};

		const FString Name = Asset.AssetName.ToString();

		// Curve atlases (CA_, or a Curve Atlas asset) are lookup tables of linear curve values: their own rules
		const bool bIsAtlasAsset = Texture->IsA<UCurveLinearColorAtlas>();
		const FNaming& Names = Naming();
		if (bIsAtlasAsset || FNaming::HasPrefix(Name, Names.CurveAtlasPrefix))
		{
			ValidateCurveAtlas(Name, bIsAtlasAsset, Texture, Settings, AddIssue);
			return;
		}

		// Vertex animation textures (VAT_, or a VAT bake part in the name) are exact data per texel: their own rules
		if (FNaming::HasPrefix(Name, Names.VertexAnimationPrefix) || HasVATSuffix(Name))
		{
			ValidateVAT(Name, Texture, Settings, AddIssue);
			return;
		}

		// What the name says this texture holds (null rule = unknown suffix)
		const FString Suffix = GetNameSuffix(Name);
		const FSuffixRule* SuffixRule = FindSuffixRule(Suffix);

		// Virtual textures: streamed in tiles, named VT_
		const bool bVirtual = Texture->VirtualTextureStreaming != 0;
		const FString& VirtualPrefix = Names.VirtualTexturePrefix;
		const bool bVirtualName = FNaming::HasPrefix(Name, VirtualPrefix);
		const bool bPowerOfTwo = Texture->Source.IsValid()
			&& FMath::IsPowerOfTwo(Texture->Source.GetSizeX()) && FMath::IsPowerOfTwo(Texture->Source.GetSizeY());
		const bool bPadded = Texture->PowerOfTwoMode != ETexturePowerOfTwoSetting::None;
		const bool bVirtualRules = bVirtual && Settings.bCheckVirtual;	// checks the VT rules replace

		// NAMING
		if (Settings.bCheckNaming)
		{
			const FText Rule = LOCTEXT("RuleNaming", "Naming");

			// The names both naming issues fix to (VT_ for virtual textures, or ones already named VT_)
			const FString& TexturePrefix = Names.TexturePrefix;
			const FString Prefix = (bVirtual || bVirtualName) && !VirtualPrefix.IsEmpty() ? VirtualPrefix : TexturePrefix;
			const FString NewSuffix = SuffixRule ? FString() : GuessSuffix(Suffix, Texture);
			FString NewNames[2][2];
			MakeTextureNames(Name, Prefix, NewSuffix, NewNames);

			if (bVirtual && !bVirtualName && !VirtualPrefix.IsEmpty())
			{
				// T_Rock_D -> VT_Rock_D
				AssetValidation::SetRenameChoices(AddIssue(ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("MissingVirtualPrefix", "Virtual texture should start with {0}"), FText::FromString(VirtualPrefix))), Name, NewNames);
			}
			// No virtual prefix set: virtual textures follow the regular texture prefix
			else if ((!bVirtual || VirtualPrefix.IsEmpty()) && !bVirtualName && !TexturePrefix.IsEmpty() && !Name.StartsWith(TexturePrefix))
			{
				AssetValidation::SetRenameChoices(AddIssue(ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("MissingPrefix", "Name should start with {0}"), FText::FromString(TexturePrefix))), Name, NewNames);
			}
			if (!SuffixRule)
			{
				const FString Alias = Names.FindAlias(Suffix);
				const FString Ambiguous = Clean(Names.AmbiguousSuffix);
				FText Message;
				if (Suffix.IsEmpty())
				{
					// A few of the known suffixes as examples
					TArray<FString> Examples;
					GetKnownSuffixList().ParseIntoArray(Examples, TEXT(", "));
					Examples.SetNum(FMath::Min(Examples.Num(), 3));
					Message = Examples.Num() > 0
						? FText::Format(LOCTEXT("MissingSuffix", "Name has no type suffix (e.g. {0})"), FText::FromString(FString::Join(Examples, TEXT(", "))))
						: LOCTEXT("MissingSuffixNoList", "Name has no type suffix");
				}
				else if (!Ambiguous.IsEmpty() && Suffix.Equals(Ambiguous, ESearchCase::IgnoreCase))
				{
					Message = FText::Format(LOCTEXT("AmbiguousSuffix", "_{0} could mean color or opacity: use _{1} for color or _{2} for opacity (its settings point to _{3})"),
						FText::FromString(Suffix), FText::FromString(Clean(Names.ColorSuffix)), FText::FromString(Clean(Names.OpacitySuffix)), FText::FromString(NewSuffix));
				}
				else if (!Alias.IsEmpty() && Alias.Equals(Clean(Names.ColorSuffix), ESearchCase::IgnoreCase))
				{
					Message = FText::Format(LOCTEXT("DiffuseSuffix", "Diffuse, albedo and base color textures use _{0}, not _{1}"), FText::FromString(Alias), FText::FromString(Suffix));
				}
				else if (!Alias.IsEmpty())
				{
					Message = FText::Format(LOCTEXT("AliasSuffix", "Use _{0} instead of _{1}"), FText::FromString(Alias), FText::FromString(Suffix));
				}
				else
				{
					Message = FText::Format(LOCTEXT("UnknownSuffix", "Unknown suffix _{0}"), FText::FromString(Suffix));
				}
				FAssetIssue& Issue = AddIssue(ESeverity::Warning, Rule, Message);
				if (!NewSuffix.IsEmpty())
				{
					AssetValidation::SetRenameChoices(Issue, Name, NewNames);
				}
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
			// HDR color (such as a consolidated emissive) is float data, stored linear
			const bool bLinearHDR = SuffixRule->Kind == EKind::Color && (Compression == TC_HDR || Compression == TC_HDR_F32) && !Texture->SRGB;

			if (Texture->SRGB != bWantsSRGB && !bLinearHDR)
			{
				AddIssue(ESeverity::Error, Rule, bWantsSRGB
					? LOCTEXT("NeedsSRGB", "Color texture should have sRGB on")
					: LOCTEXT("NoSRGB", "Data texture (normal/mask) should have sRGB off"))
					.FixSettings = EditTexture([bWantsSRGB](UTexture2D& Fix) { Fix.SRGB = bWantsSRGB; });
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

			// Heightmaps: Displacementmap reads the source's A, Grayscale (and Alpha/BC4) read R.
			// Expect displacement compression, unless the height is only in R, where it would come out flat.
			bool bHeightInRed = false;
			if (SuffixRule->Kind == EKind::Height)
			{
				bHeightInRed = !HasAlphaData(Texture);
				Allowed = bHeightInRed ? TArray<TextureCompressionSettings>{ TC_Grayscale, TC_Alpha } : TArray<TextureCompressionSettings>{ TC_Displacementmap };
			}

			if (SuffixRule->Kind == EKind::Height && bHeightInRed && Compression == TC_Displacementmap)
			{
				AddIssue(ESeverity::Error, Rule, FText::Format(LOCTEXT("HeightFlat", "{0} reads the source's A channel, but this heightmap's data is only in R (it comes out flat): use {1}"),
					GetCompressionName(TC_Displacementmap), GetCompressionName(TC_Grayscale)))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.CompressionSettings = TC_Grayscale; });
			}
			else if (!Allowed.Contains(Compression))
			{
				TArray<FString> AllowedNames;
				for (TextureCompressionSettings Option : Allowed)
				{
					AllowedNames.Add(GetCompressionName(Option).ToString());
				}

				AddIssue(SuffixRule->Kind == EKind::Normal ? ESeverity::Error : ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("WrongCompression", "Compression is {0}, expected {1} for _{2}"),
						GetCompressionName(Compression), FText::FromString(FString::Join(AllowedNames, TEXT(" or "))), FText::FromString(Suffix)))
					.FixSettings = EditTexture([Target = Allowed[0]](UTexture2D& Fix) { Fix.CompressionSettings = Target; });	// the first allowed is the usual one
			}
		}

		// RESOLUTION (source size, so it's what was imported)
		if (Settings.bCheckResolution && Texture->Source.IsValid())
		{
			const FText Rule = LOCTEXT("RuleResolution", "Resolution");
			const int32 SizeX = Texture->Source.GetSizeX();
			const int32 SizeY = Texture->Source.GetSizeY();
			const FText SizeText = FText::FromString(FString::Printf(TEXT("%d x %d"), SizeX, SizeY));

			// Non-power-of-two textures get no mips and can't stream (virtual ones have their own rule below)
			if (!bVirtualRules && (!FMath::IsPowerOfTwo(SizeX) || !FMath::IsPowerOfTwo(SizeY)))
			{
				AddIssue(ESeverity::Warning, Rule,
					FText::Format(LOCTEXT("NotPowerOfTwo", "{0} is not a power of two (no mips or streaming)"), SizeText));
			}

			// A Maximum Texture Size setting at or under the limit caps it in game, so that's fine
			const bool bCapped = Texture->MaxTextureSize > 0 && Texture->MaxTextureSize <= Settings.MaxSize;
			if (FMath::Max(SizeX, SizeY) > Settings.MaxSize && !bCapped && !bVirtual)
			{
				AddIssue(ESeverity::Error, Rule,
					FText::Format(LOCTEXT("TooLarge", "{0} is larger than the {1} max"), SizeText, FText::AsNumber(Settings.MaxSize, &FNumberFormattingOptions::DefaultNoGrouping())))
					.FixSettings = EditTexture([MaxSize = Settings.MaxSize](UTexture2D& Fix) { Fix.MaxTextureSize = MaxSize; });	// caps it in game; the source stays
			}
		}

		// MIPS & STREAMING (UI textures normally skip both)
		if (Settings.bCheckMips && Texture->LODGroup != TEXTUREGROUP_UI)
		{
			const FText Rule = LOCTEXT("RuleMips", "Mips");

			if (Texture->MipGenSettings == TMGS_NoMipmaps && !bVirtualRules)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NoMips", "Has no mipmaps (shimmers at a distance, can't stream)"))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.MipGenSettings = TMGS_FromTextureGroup; });
			}
			if (Texture->NeverStream && !bVirtual)
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NeverStream", "Never Stream is on (always fully in memory)"))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.NeverStream = false; });
			}

			// Normal maps are recognized by suffix or by their compression, whichever says so
			const bool bIsNormalMap = (SuffixRule && SuffixRule->Kind == EKind::Normal) || Texture->CompressionSettings == TC_Normalmap;
			if (bIsNormalMap && !IsNormalMapGroup(Texture->LODGroup))
			{
				AddIssue(ESeverity::Warning, Rule, LOCTEXT("NormalGroup", "Normal map should use a NormalMap texture group"))
					.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.LODGroup = TEXTUREGROUP_WorldNormalMap; });
			}
		}

		// VIRTUAL TEXTURES (the engine turns VT off, or forces settings, when these aren't met)
		if (Settings.bCheckVirtual && (bVirtual || bVirtualName))
		{
			const FText Rule = LOCTEXT("RuleVirtual", "Virtual");
			const bool bCanBeVirtual = (bPowerOfTwo || bPadded) && Texture->LODGroup != TEXTUREGROUP_ColorLookupTable;

			if (!bVirtual)
			{
				// Named VT_ but not virtual. With VT off for the project, the engine turns it off on every texture
				if (!UTexture::IsVirtualTexturingEnabled())
				{
					AddIssue(ESeverity::Error, Rule, FText::Format(LOCTEXT("ProjectVTOff", "Named {0}, but virtual texturing is off for the project (Project Settings > Rendering > Virtual Textures)"), FText::FromString(VirtualPrefix)));
				}
				else
				{
					FAssetIssue& Issue = AddIssue(ESeverity::Warning, Rule, FText::Format(bCanBeVirtual
						? LOCTEXT("VTOff", "Named {0}, but Virtual Texture Streaming is off (materials sampling it then need Virtual sampler types)")
						: LOCTEXT("VTOffCant", "Named {0}, but Virtual Texture Streaming is off, and it can't be virtual as it is (not a power of two, or ColorLookupTable group)"),
						FText::FromString(VirtualPrefix)));
					if (bCanBeVirtual)
					{
						// What UTexture::SetVirtualTextureStreaming does (it isn't exported): the property event tells the materials using it to update
						Issue.FixSettings = EditTexture([](UTexture2D& Fix)
						{
							Fix.VirtualTextureStreaming = true;
							FPropertyChangedEvent Event(UTexture::StaticClass()->FindPropertyByName(GET_MEMBER_NAME_CHECKED(UTexture, VirtualTextureStreaming)));
							Fix.PostEditChangeProperty(Event);
						});
					}
				}
			}
			else
			{
				if (Texture->Source.IsValid() && !bPowerOfTwo && !bPadded)
				{
					AddIssue(ESeverity::Error, Rule, FText::Format(LOCTEXT("VTNotPowerOfTwo", "Virtual textures need a power-of-two size ({0} x {1}): pad it, or make it a regular texture"),
						Texture->Source.GetSizeX(), Texture->Source.GetSizeY()))
						.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.PowerOfTwoMode = ETexturePowerOfTwoSetting::PadToPowerOfTwo; });
				}
				if (Texture->MipGenSettings == TMGS_NoMipmaps)
				{
					AddIssue(ESeverity::Error, Rule, LOCTEXT("VTNoMips", "Virtual textures need mipmaps (the engine forces SimpleAverage)"))
						.FixSettings = EditTexture([](UTexture2D& Fix) { Fix.MipGenSettings = TMGS_FromTextureGroup; });
				}
				if (Texture->LODGroup == TEXTUREGROUP_ColorLookupTable)
				{
					AddIssue(ESeverity::Error, Rule, LOCTEXT("VTLUT", "The ColorLookupTable group can't be virtual (it has no mips): turn Virtual Texture Streaming off"));
				}
				if (Texture->Source.IsValid() && FMath::Max(Texture->Source.GetSizeX(), Texture->Source.GetSizeY()) < Settings.VirtualMinSize)
				{
					AddIssue(ESeverity::Warning, Rule, FText::Format(LOCTEXT("VTSmall", "{0} x {1} is small for a virtual texture (under {2}): a regular texture avoids the tile overhead"),
						Texture->Source.GetSizeX(), Texture->Source.GetSizeY(), FText::AsNumber(Settings.VirtualMinSize, &FNumberFormattingOptions::DefaultNoGrouping())));
				}
			}
		}
	}
}

#undef LOCTEXT_NAMESPACE
