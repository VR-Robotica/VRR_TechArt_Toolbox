// Copyright VR Robotica. All Rights Reserved.

/*
 * Texture validation: checks texture assets against the project's texture rules.
 *
 * Strategy
 *  - The name suffix says what a texture holds (T_Rock_N = normal map, T_Rock_ORM = packed mask...).
 *    SuffixRules maps each known suffix to a kind (color, normal, mask, single channel).
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

#define LOCTEXT_NAMESPACE "TextureValidator"

namespace TextureValidator
{
	/** What a name suffix says the texture holds. Any = accept any settings (tool outputs with their own rules). */
	enum class EKind : uint8 { Color, Normal, Mask, Single, Height, Any };

	/** One known suffix and the kind of data it means. */
	struct FSuffixRule
	{
		const TCHAR* Suffix;
		EKind Kind;
	};

	/** Every suffix the validator knows. Add new suffixes here; the Validate page tooltip lists them. */
	const FSuffixRule SuffixRules[] =
	{
		// Color data (sRGB). Diffuse, albedo and base color are all _D (the other names are SuffixAliases)
		{ TEXT("D"), EKind::Color }, { TEXT("E"), EKind::Color }, { TEXT("Emissive"), EKind::Color },
		// Normal maps
		{ TEXT("N"), EKind::Normal }, { TEXT("Normal"), EKind::Normal },
		// Packed masks (linear, several channels)
		{ TEXT("ORM"), EKind::Mask }, { TEXT("MRA"), EKind::Mask }, { TEXT("RMA"), EKind::Mask },
		{ TEXT("Mask"), EKind::Mask }, { TEXT("MSK"), EKind::Mask }, { TEXT("CCE"), EKind::Mask },	// CCE: Curvature/Cavity/Edge (Toolbox > Mesh Maps)
		// Single-channel data (linear)
		{ TEXT("R"), EKind::Single }, { TEXT("Rough"), EKind::Single }, { TEXT("M"), EKind::Single },
		{ TEXT("Metal"), EKind::Single }, { TEXT("AO"), EKind::Single },
		{ TEXT("O"), EKind::Single }, { TEXT("Opacity"), EKind::Single },
		{ TEXT("Curvature"), EKind::Single }, { TEXT("Cavity"), EKind::Single }, { TEXT("Edge"), EKind::Single },
		{ TEXT("S"), EKind::Single }, { TEXT("Spec"), EKind::Single }, { TEXT("OM"), EKind::Single },	// Specular, Opacity Mask (Toolbox > Consolidate)
		// Heightmaps (linear, single channel, displacement compression)
		{ TEXT("H"), EKind::Height }, { TEXT("Height"), EKind::Height },
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

	/** Prefixes of regular, virtual and vertex animation textures. */
	const TCHAR* const TexturePrefix = TEXT("T_");
	const TCHAR* const VirtualPrefix = TEXT("VT_");
	const TCHAR* const VATPrefix = TEXT("VAT_");

	/** Name parts the VAT bake (Toolbox > VAT) and AnimToTexture give their textures. */
	const TCHAR* const VATSuffixes[] = { TEXT("BonePos"), TEXT("BoneRot"), TEXT("BoneWeights"), TEXT("VertexPos"), TEXT("VertexNormal") };

	/** The ambiguous suffix: albedo or alpha. */
	const TCHAR* const AmbiguousSuffix = TEXT("A");

	/** "T_Rock_N_01" or "VT_Rock_N_01" -> "N" (strips the prefix and a trailing number). Empty when there is no suffix. */
	FString GetNameSuffix(FString Name)
	{
		if (!Name.RemoveFromStart(VirtualPrefix))
		{
			Name.RemoveFromStart(TexturePrefix);
		}

		int32 Underscore;
		if (Name.FindLastChar(TEXT('_'), Underscore) && Name.Mid(Underscore + 1).IsNumeric())
		{
			Name.LeftInline(Underscore);
		}

		return Name.FindLastChar(TEXT('_'), Underscore) ? Name.Mid(Underscore + 1) : FString();
	}

	/** Common suffixes from other tools and habits, and the known suffix each one means. */
	const TPair<const TCHAR*, const TCHAR*> SuffixAliases[] =
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

	/**
	 * The known suffix a texture should get: what its unknown suffix is an alias of, else what its settings say
	 * (normal map compression -> N, sRGB color -> D). Empty when there's no safe guess (masks, single channels).
	 */
	/** The known suffix an alias stands for (_Albedo -> D), or null. */
	const TCHAR* FindAlias(const FString& Suffix)
	{
		for (const TPair<const TCHAR*, const TCHAR*>& Alias : SuffixAliases)
		{
			if (Suffix.Equals(Alias.Key, ESearchCase::IgnoreCase))
			{
				return Alias.Value;
			}
		}
		return nullptr;
	}

	FString GuessSuffix(const FString& UnknownSuffix, const UTexture2D* Texture)
	{
		if (const TCHAR* Alias = FindAlias(UnknownSuffix))
		{
			return Alias;
		}
		// _A: albedo when it's sRGB color, else alpha (opacity)
		if (UnknownSuffix.Equals(AmbiguousSuffix, ESearchCase::IgnoreCase))
		{
			return Texture->SRGB ? TEXT("D") : TEXT("O");
		}
		if (Texture->CompressionSettings == TC_Normalmap)
		{
			return TEXT("N");
		}
		if (Texture->SRGB && (Texture->CompressionSettings == TC_Default || Texture->CompressionSettings == TC_BC7))
		{
			return TEXT("D");
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
		const bool bKnownPrefix = Rest.RemoveFromStart(VirtualPrefix, ESearchCase::CaseSensitive) || Rest.RemoveFromStart(TexturePrefix);
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

	/** Prefix of Curve Atlas textures (Curve Linear Color Atlas assets, or lookup textures baked from them). */
	const TCHAR* const CurveAtlasPrefix = TEXT("CA_");

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

		if (Settings.bCheckNaming && bIsAtlasAsset && !Name.StartsWith(CurveAtlasPrefix, ESearchCase::CaseSensitive))
		{
			AddIssue(ESeverity::Warning, LOCTEXT("RuleNaming", "Naming"), LOCTEXT("AtlasPrefix", "Curve Atlas should start with CA_")).RenameTo = CurveAtlasPrefix + Name;
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
			for (const TCHAR* Suffix : VATSuffixes)
			{
				if (Part.Equals(Suffix, ESearchCase::IgnoreCase))
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

		if (Settings.bCheckNaming && !Name.StartsWith(VATPrefix, ESearchCase::CaseSensitive))
		{
			FString Body = Name;
			if (!Body.RemoveFromStart(TexturePrefix))
			{
				Body.RemoveFromStart(VirtualPrefix);
			}
			AddIssue(ESeverity::Warning, LOCTEXT("RuleNaming", "Naming"), LOCTEXT("VATPrefix", "Vertex animation texture should start with VAT_"))
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
		if (bIsAtlasAsset || Name.StartsWith(CurveAtlasPrefix, ESearchCase::CaseSensitive))
		{
			ValidateCurveAtlas(Name, bIsAtlasAsset, Texture, Settings, AddIssue);
			return;
		}

		// Vertex animation textures (VAT_, or a VAT bake part in the name) are exact data per texel: their own rules
		if (Name.StartsWith(VATPrefix, ESearchCase::CaseSensitive) || HasVATSuffix(Name))
		{
			ValidateVAT(Name, Texture, Settings, AddIssue);
			return;
		}

		// What the name says this texture holds (null rule = unknown suffix)
		const FString Suffix = GetNameSuffix(Name);
		const FSuffixRule* SuffixRule = FindSuffixRule(Suffix);

		// Virtual textures: streamed in tiles, named VT_
		const bool bVirtual = Texture->VirtualTextureStreaming != 0;
		const bool bVirtualName = Name.StartsWith(VirtualPrefix, ESearchCase::CaseSensitive);
		const bool bPowerOfTwo = Texture->Source.IsValid()
			&& FMath::IsPowerOfTwo(Texture->Source.GetSizeX()) && FMath::IsPowerOfTwo(Texture->Source.GetSizeY());
		const bool bPadded = Texture->PowerOfTwoMode != ETexturePowerOfTwoSetting::None;
		const bool bVirtualRules = bVirtual && Settings.bCheckVirtual;	// checks the VT rules replace

		// NAMING
		if (Settings.bCheckNaming)
		{
			const FText Rule = LOCTEXT("RuleNaming", "Naming");

			// The names both naming issues fix to (VT_ for virtual textures, or ones already named VT_)
			const FString Prefix = bVirtual || bVirtualName ? VirtualPrefix : TexturePrefix;
			const FString NewSuffix = SuffixRule ? FString() : GuessSuffix(Suffix, Texture);
			FString Names[2][2];
			MakeTextureNames(Name, Prefix, NewSuffix, Names);

			if (bVirtual && !bVirtualName)
			{
				// T_Rock_D -> VT_Rock_D
				AssetValidation::SetRenameChoices(AddIssue(ESeverity::Warning, Rule, LOCTEXT("MissingVirtualPrefix", "Virtual texture should start with VT_")), Name, Names);
			}
			else if (!bVirtual && !bVirtualName && !Name.StartsWith(TexturePrefix))
			{
				AssetValidation::SetRenameChoices(AddIssue(ESeverity::Warning, Rule, LOCTEXT("MissingPrefix", "Name should start with T_")), Name, Names);
			}
			if (!SuffixRule)
			{
				const TCHAR* Alias = FindAlias(Suffix);
				FText Message;
				if (Suffix.IsEmpty())
				{
					Message = LOCTEXT("MissingSuffix", "Name has no type suffix (e.g. _D, _N, _ORM)");
				}
				else if (Suffix.Equals(AmbiguousSuffix, ESearchCase::IgnoreCase))
				{
					Message = FText::Format(LOCTEXT("AmbiguousSuffix", "_A could mean Albedo or Alpha: use _D for color or _O for opacity (its settings point to _{0})"),
						FText::FromString(NewSuffix));
				}
				else if (Alias && FCString::Strcmp(Alias, TEXT("D")) == 0)
				{
					Message = FText::Format(LOCTEXT("DiffuseSuffix", "Diffuse, albedo and base color textures use _D, not _{0}"), FText::FromString(Suffix));
				}
				else if (Alias)
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
					AssetValidation::SetRenameChoices(Issue, Name, Names);
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
					AddIssue(ESeverity::Error, Rule, LOCTEXT("ProjectVTOff", "Named VT_, but virtual texturing is off for the project (Project Settings > Rendering > Virtual Textures)"));
				}
				else
				{
					FAssetIssue& Issue = AddIssue(ESeverity::Warning, Rule, bCanBeVirtual
						? LOCTEXT("VTOff", "Named VT_, but Virtual Texture Streaming is off (materials sampling it then need Virtual sampler types)")
						: LOCTEXT("VTOffCant", "Named VT_, but Virtual Texture Streaming is off, and it can't be virtual as it is (not a power of two, or ColorLookupTable group)"));
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
