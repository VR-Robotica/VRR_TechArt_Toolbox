// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TechArtToolboxNamingSettings.generated.h"

/** What a texture name suffix says the texture holds; the Validate page checks its settings against this. */
UENUM()
enum class ETatTextureKind : uint8
{
	Color	UMETA(DisplayName = "Color (sRGB)"),
	Normal	UMETA(DisplayName = "Normal Map"),
	Mask	UMETA(DisplayName = "Packed Mask (linear)"),
	Single	UMETA(DisplayName = "Single Channel (linear)"),
	Height	UMETA(DisplayName = "Heightmap"),
	Any		UMETA(DisplayName = "Any (settings not checked)")
};

/** One known texture suffix (without the underscore) and the kind of data it means. */
USTRUCT()
struct FTatTextureSuffix
{
	GENERATED_BODY()

	FTatTextureSuffix() = default;
	FTatTextureSuffix(const TCHAR* InSuffix, ETatTextureKind InKind) : Suffix(InSuffix), Kind(InKind) {}

	/** Matched case-insensitively against the last part of the name (T_Rock_N -> N). */
	UPROPERTY(EditAnywhere, Category = "Suffix")
	FString Suffix;

	UPROPERTY(EditAnywhere, Category = "Suffix")
	ETatTextureKind Kind = ETatTextureKind::Color;
};

/** A suffix from another convention or tool, and the known suffix Fix All renames it to (Albedo -> D). */
USTRUCT()
struct FTatSuffixAlias
{
	GENERATED_BODY()

	FTatSuffixAlias() = default;
	FTatSuffixAlias(const TCHAR* InAlias, const TCHAR* InSuffix) : Alias(InAlias), Suffix(InSuffix) {}

	/** The suffix as found in names, without the underscore (case-insensitive). */
	UPROPERTY(EditAnywhere, Category = "Alias")
	FString Alias;

	/** The known suffix it stands for, without the underscore. */
	UPROPERTY(EditAnywhere, Category = "Alias")
	FString Suffix;
};

/**
 * Project Settings > Plugins > TechArt Toolbox Asset Naming: the prefixes and suffixes the Validate page checks
 * names against and renames to. Saved to the project's DefaultEditor.ini so a team shares them.
 * Prefixes are case-sensitive and include their underscore (T_); suffixes leave it out (D for T_Rock_D).
 * An empty prefix turns that prefix check off.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "TechArt Toolbox Asset Naming"))
class UTechArtToolboxNamingSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UTechArtToolboxNamingSettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UTechArtToolboxNamingSettings& Get() { return *GetDefault<UTechArtToolboxNamingSettings>(); }

	/** Opens this page in Project Settings. */
	static void ShowInProjectSettings();

	// TEXTURES

	UPROPERTY(config, EditAnywhere, Category = "Textures")
	FString TexturePrefix = TEXT("T_");

	/** Textures with Virtual Texture Streaming on. */
	UPROPERTY(config, EditAnywhere, Category = "Textures")
	FString VirtualTexturePrefix = TEXT("VT_");

	/** Vertex animation textures (exact per-texel data: linear, HDR, Nearest, no mips). */
	UPROPERTY(config, EditAnywhere, Category = "Textures", meta = (DisplayName = "Vertex Animation Texture Prefix"))
	FString VertexAnimationPrefix = TEXT("VAT_");

	/** Curve Linear Color Atlas assets and lookup textures baked from them (linear, uncompressed, no mips). */
	UPROPERTY(config, EditAnywhere, Category = "Textures")
	FString CurveAtlasPrefix = TEXT("CA_");

	/** Every suffix a texture name may end with, and what it means for the texture's settings. */
	UPROPERTY(config, EditAnywhere, Category = "Textures", meta = (TitleProperty = "Suffix"))
	TArray<FTatTextureSuffix> Suffixes;

	/** Suffixes from other conventions that Fix All renames to a known suffix. */
	UPROPERTY(config, EditAnywhere, Category = "Textures", meta = (TitleProperty = "Alias"))
	TArray<FTatSuffixAlias> SuffixAliases;

	/** Name parts that mark a vertex animation texture even without its prefix (T_Hero_BonePos). */
	UPROPERTY(config, EditAnywhere, Category = "Textures", meta = (DisplayName = "Vertex Animation Name Parts"))
	TArray<FString> VertexAnimationNameParts;

	/** Suffix Fix All gives an unknown-suffix texture that has color settings (sRGB). */
	UPROPERTY(config, EditAnywhere, Category = "Textures|Fix All")
	FString ColorSuffix = TEXT("D");

	/** Suffix Fix All gives an unknown-suffix texture with Normal Map compression. */
	UPROPERTY(config, EditAnywhere, Category = "Textures|Fix All")
	FString NormalSuffix = TEXT("N");

	/** Suffix the ambiguous suffix becomes when the texture is linear (alpha / opacity). */
	UPROPERTY(config, EditAnywhere, Category = "Textures|Fix All")
	FString OpacitySuffix = TEXT("O");

	/** A suffix that could mean color or opacity (A: albedo or alpha); renamed by the texture's sRGB setting. Empty: none. */
	UPROPERTY(config, EditAnywhere, Category = "Textures|Fix All")
	FString AmbiguousSuffix = TEXT("A");

	// MESHES

	/** The first one is what Fix All adds. */
	UPROPERTY(config, EditAnywhere, Category = "Meshes")
	TArray<FString> StaticMeshPrefixes;

	/** The first one is what Fix All adds. */
	UPROPERTY(config, EditAnywhere, Category = "Meshes")
	TArray<FString> SkeletalMeshPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Meshes")
	FString SkeletonPrefix = TEXT("SKEL_");

	UPROPERTY(config, EditAnywhere, Category = "Meshes")
	FString PhysicsAssetPrefix = TEXT("PHYS_");

	// MATERIALS (mesh slot checks accept the Material and Material Instance prefixes)

	/** The first one is what Fix All adds. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> MaterialPrefixes;

	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> MaterialInstancePrefixes;

	/** Material functions. Function and layer prefixes are accepted on either. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> MaterialFunctionPrefixes;

	/** Material layers and layer blends. Fix All gives layers the first of these. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> MaterialLayerPrefixes;

	/** Materials in the Post Process domain. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> PostProcessMaterialPrefixes;

	/** Instances whose root material is a post process material. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<FString> PostProcessInstancePrefixes;

	// HELPERS FOR THE VALIDATORS

	/** The list without empty entries (an empty prefix would match every name). */
	static TArray<FString> NonEmpty(const TArray<FString>& List);
	/** The first non-empty entry, or empty. */
	static FString First(const TArray<FString>& List);
	/** Whether Name starts with Prefix (case-sensitive); false for an empty prefix. */
	static bool HasPrefix(const FString& Name, const FString& Prefix);
	/** The longest entry of Prefixes that Name starts with, or empty. */
	static FString FindPrefix(const FString& Name, const TArray<FString>& Prefixes);

	/** A suffix as written in names: trimmed, without a leading underscore ("_D " -> "D"). */
	static FString CleanSuffix(const FString& Suffix);

	/** The rule for a suffix (case-insensitive, a leading underscore ignored), or null for an unknown one. */
	const FTatTextureSuffix* FindSuffix(const FString& Suffix) const;
	/** The known suffix an alias stands for, or empty. */
	FString FindAlias(const FString& Suffix) const;
};
