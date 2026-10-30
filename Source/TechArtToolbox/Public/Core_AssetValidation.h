// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"

struct FTextureValidatorSettings;
struct FMeshValidatorSettings;
struct FMaterialValidatorSettings;

/** One problem found on one asset, and how Fix All repairs it (if it can). */
struct FAssetIssue
{
	enum class ESeverity : uint8 { Warning, Error };
	enum class EAssetType : uint8 { Texture, Mesh, Material, Count };

	FAssetData Asset;
	ESeverity Severity = ESeverity::Warning;
	EAssetType Type = EAssetType::Texture;
	FText Rule;
	FText Message;

	// FIX: a rename, a settings change, or both (all of an asset's naming issues carry the same final names)
	FString RenameTo;					// new name for RenameObject (or for the asset itself when RenameObject is empty)
	// When the name has a prefix or suffix the validator doesn't know (Tex_Rock_Diff), RenameTo keeps it and adds the
	// expected one (T_Tex_Rock_Diff_D); these replace it instead (T_Rock_Diff_D, T_Tex_Rock_D, T_Rock_D). Empty = no such choice.
	FString RenameReplacingPrefix;
	FString RenameReplacingSuffix;
	FString RenameReplacingBoth;
	FSoftObjectPath RenameObject;		// another asset to rename, e.g. a skeletal mesh's skeleton
	TFunction<void(UObject*)> FixSettings;	// changes the (loaded) asset's settings

	bool CanFix() const { return !RenameTo.IsEmpty() || HasPrefixChoice() || HasSuffixChoice() || FixSettings != nullptr; }
	bool HasPrefixChoice() const { return !RenameReplacingPrefix.IsEmpty() && RenameReplacingPrefix != RenameTo; }
	bool HasSuffixChoice() const { return !RenameReplacingSuffix.IsEmpty() && RenameReplacingSuffix != RenameTo; }

	/** The new name for these choices (falls back to RenameTo where a choice doesn't apply). */
	const FString& GetRenameTo(bool bReplacePrefix, bool bReplaceSuffix) const
	{
		if (bReplacePrefix && bReplaceSuffix && !RenameReplacingBoth.IsEmpty())
		{
			return RenameReplacingBoth;
		}
		if (bReplacePrefix && HasPrefixChoice())
		{
			return RenameReplacingPrefix;
		}
		if (bReplaceSuffix && HasSuffixChoice())
		{
			return RenameReplacingSuffix;
		}
		return RenameTo;
	}
};

/** Fix choices for names with a prefix or suffix the validator doesn't know. */
struct FAssetFixOptions
{
	bool bReplaceUnknownPrefix = false;	// false: keep it and add the expected one in front
	bool bReplaceUnknownSuffix = false;	// false: keep it and add the expected one after
};

/** Which asset types to check, and how: a null pointer skips that type. */
struct FAssetValidationSettings
{
	const FTextureValidatorSettings* Textures = nullptr;
	const FMeshValidatorSettings* Meshes = nullptr;
	const FMaterialValidatorSettings* Materials = nullptr;
};

struct FAssetValidationResult
{
	TArray<FAssetIssue> Issues;	// errors first, then by asset name
	int32 NumScanned[static_cast<int32>(FAssetIssue::EAssetType::Count)] = {};	// assets checked, per type
	int32 NumErrors = 0;
	int32 NumWarnings = 0;

	int32 GetNumScanned(FAssetIssue::EAssetType Type) const { return NumScanned[static_cast<int32>(Type)]; }
};

/** What FixIssues did. */
struct FAssetFixResult
{
	TArray<FSoftObjectPath> OldPaths;	// every asset that had fixes, as it was named before
	TArray<FAssetData> FixedAssets;		// the same assets after the fixes (new names), to check again
	int32 NumSettingsFixed = 0;			// assets whose settings were changed
	int32 NumRenamed = 0;				// assets renamed (including skeletons and physics assets)
	TArray<FString> SkippedRenames;		// "Old -> New": the new name was taken, or the rename was refused
};

/**
 * Runs the texture (Core_TextureValidator.h), mesh (Core_MeshValidator.h) and material (Core_MaterialValidator.h)
 * validators over a set of assets, applies their fixes, and exports the issues.
 */
namespace AssetValidation
{
	/** Every asset of the enabled types in a content folder (from the Asset Registry, nothing loaded). */
	TArray<FAssetData> FindAssets(const FString& Folder, bool bIncludeSubfolders, const FAssetValidationSettings& Settings);

	/** Only the assets of the enabled types. */
	TArray<FAssetData> FilterAssets(const TArray<FAssetData>& Assets, const FAssetValidationSettings& Settings);

	/** Checks each asset (with a cancelable progress dialog; textures and meshes are loaded), then sorts errors first. */
	FAssetValidationResult Validate(const TArray<FAssetData>& Assets, const FAssetValidationSettings& Settings);

	/**
	 * Applies the fixes of these issues: settings first (one edit per asset), then every rename in one batch
	 * through Asset Tools (references are fixed up, redirectors left). Changed packages are left unsaved.
	 */
	FAssetFixResult FixIssues(const TArray<const FAssetIssue*>& Issues, const FAssetFixOptions& Options);

	/**
	 * Fills the issue's RenameTo and RenameReplacing* from the four candidate names, indexed
	 * [replace unknown prefix][replace unknown suffix] (empty = that combination doesn't apply). A choice is only kept
	 * when it gives a different name; RenameTo is left empty when the name wouldn't change.
	 */
	void SetRenameChoices(FAssetIssue& Issue, const FString& CurrentName, const FString (&Names)[2][2]);

	/** "Tex_Rock" -> "Rock": the name without its first _ token, or empty if there's nothing after it. */
	FString RemoveFirstToken(const FString& Name);

	/** Writes the issues to a JSON file, grouped by asset. */
	bool ExportIssues(const FString& FilePath, const FString& Section, int32 NumScanned, const TArray<const FAssetIssue*>& Issues);

	/** Issue order: errors first, then by asset name. */
	bool IssueLess(const FAssetIssue& A, const FAssetIssue& B);

	/** What is wrong with a name after its prefix (spaces or symbols, double or edge underscores), or empty. */
	FText GetNameBodyProblem(const FString& Body);

	/** A name body with those problems fixed: symbols become _, runs of _ become one, edge _ are dropped. */
	FString CleanNameBody(const FString& Body);
}
