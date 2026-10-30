// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Asset validation: finds the assets to check and hands each one to its validator
 * (TextureValidator for textures, MeshValidator for static and skeletal meshes, MaterialValidator for materials,
 * instances and functions), then totals and sorts the issues.
 * FixIssues applies the fixes the validators attached to their issues; ExportIssues writes a JSON report.
 */

#include "Core_AssetValidation.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Core_MaterialValidator.h"
#include "Core_MeshValidator.h"
#include "Core_TextureValidator.h"
#include "Dom/JsonObject.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "IAssetTools.h"
#include "Materials/Material.h"
#include "Materials/MaterialFunctionInterface.h"
#include "Materials/MaterialInstance.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#define LOCTEXT_NAMESPACE "AssetValidation"

namespace AssetValidation
{
	bool IsMeshAsset(const FAssetData& Asset)
	{
		return Asset.IsInstanceOf(UStaticMesh::StaticClass()) || Asset.IsInstanceOf(USkeletalMesh::StaticClass());
	}

	TArray<FAssetData> FindAssets(const FString& Folder, bool bIncludeSubfolders, const FAssetValidationSettings& Settings)
	{
		FARFilter Filter;
		if (Settings.Textures)
		{
			Filter.ClassPaths.Add(UTexture2D::StaticClass()->GetClassPathName());
		}
		if (Settings.Meshes)
		{
			Filter.ClassPaths.Add(UStaticMesh::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());
		}
		if (Settings.Materials)
		{
			Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UMaterialInstance::StaticClass()->GetClassPathName());
			Filter.ClassPaths.Add(UMaterialFunctionInterface::StaticClass()->GetClassPathName());
		}
		TArray<FAssetData> Assets;
		if (Filter.ClassPaths.IsEmpty())
		{
			return Assets;
		}
		Filter.bRecursiveClasses = true;
		Filter.PackagePaths.Add(FName(*Folder));
		Filter.bRecursivePaths = bIncludeSubfolders;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get().GetAssets(Filter, Assets);
		return Assets;
	}

	TArray<FAssetData> FilterAssets(const TArray<FAssetData>& Assets, const FAssetValidationSettings& Settings)
	{
		return Assets.FilterByPredicate([&Settings](const FAssetData& Asset)
		{
			return (Settings.Textures && Asset.IsInstanceOf(UTexture2D::StaticClass()))
				|| (Settings.Meshes && IsMeshAsset(Asset))
				|| (Settings.Materials && MaterialValidator::IsMaterialAsset(Asset));
		});
	}

	/**
	 *  1. check each asset (cancelable, since loading big folders takes a while; materials only need their names),
	 *  2. count errors and warnings,
	 *  3. sort so errors come first.
	 */
	FAssetValidationResult Validate(const TArray<FAssetData>& Assets, const FAssetValidationSettings& Settings)
	{
		FAssetValidationResult Result;

		// 1. Loading is the slow part; the user can cancel and keep what was found so far
		FScopedSlowTask SlowTask(static_cast<float>(Assets.Num()), LOCTEXT("Validating", "Validating assets..."));
		SlowTask.MakeDialog(true);
		for (const FAssetData& Asset : Assets)
		{
			if (SlowTask.ShouldCancel())
			{
				break;
			}
			SlowTask.EnterProgressFrame(1.0f, FText::FromName(Asset.AssetName));

			auto Count = [&Result](FAssetIssue::EAssetType Type) { ++Result.NumScanned[static_cast<int32>(Type)]; };
			if (Settings.Textures && Asset.IsInstanceOf(UTexture2D::StaticClass()))
			{
				if (const UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
				{
					TextureValidator::ValidateTexture(Asset, Texture, *Settings.Textures, Result.Issues);
					Count(FAssetIssue::EAssetType::Texture);
				}
			}
			else if (Settings.Meshes && IsMeshAsset(Asset))
			{
				if (UObject* Mesh = Asset.GetAsset())
				{
					MeshValidator::ValidateMesh(Asset, Mesh, *Settings.Meshes, Result.Issues);
					Count(FAssetIssue::EAssetType::Mesh);
				}
			}
			else if (Settings.Materials && MaterialValidator::IsMaterialAsset(Asset))
			{
				MaterialValidator::ValidateMaterial(Asset, *Settings.Materials, Result.Issues);
				Count(FAssetIssue::EAssetType::Material);
			}
		}

		// 2. Totals for the summary badges
		for (const FAssetIssue& Issue : Result.Issues)
		{
			(Issue.Severity == FAssetIssue::ESeverity::Error ? Result.NumErrors : Result.NumWarnings)++;
		}

		// 3. Errors first, then by asset name
		Result.Issues.StableSort(&IssueLess);
		return Result;
	}

	/** The issues grouped by asset, in list order. */
	TArray<TPair<FSoftObjectPath, TArray<const FAssetIssue*>>> GroupByAsset(const TArray<const FAssetIssue*>& Issues, bool bOnlyFixable)
	{
		TArray<TPair<FSoftObjectPath, TArray<const FAssetIssue*>>> Groups;
		TMap<FSoftObjectPath, int32> GroupIndex;
		for (const FAssetIssue* Issue : Issues)
		{
			if (!Issue || (bOnlyFixable && !Issue->CanFix()))
			{
				continue;
			}
			const FSoftObjectPath Path = Issue->Asset.GetSoftObjectPath();
			int32 Index;
			if (const int32* Found = GroupIndex.Find(Path))
			{
				Index = *Found;
			}
			else
			{
				Index = Groups.Emplace(Path, TArray<const FAssetIssue*>());
				GroupIndex.Add(Path, Index);
			}
			Groups[Index].Value.Add(Issue);
		}
		return Groups;
	}

	/**
	 *  1. per asset: load it, apply every settings fix inside one edit (so a texture rebuilds once), collect its renames,
	 *  2. check each new name is free (on disk and within this batch), then rename them together through Asset Tools,
	 *  3. report the assets under their new names so the page can check them again.
	 */
	FAssetFixResult FixIssues(const TArray<const FAssetIssue*>& Issues, const FAssetFixOptions& Options)
	{
		FAssetFixResult Result;
		const TArray<TPair<FSoftObjectPath, TArray<const FAssetIssue*>>> Groups = GroupByAsset(Issues, true);

		// 1. Settings, and the renames to do
		struct FRename
		{
			TWeakObjectPtr<UObject> Object;
			FString NewName;
		};
		TArray<FRename> Renames;
		TArray<TWeakObjectPtr<UObject>> Objects;
		{
			FScopedSlowTask SlowTask(static_cast<float>(Groups.Num()), LOCTEXT("Fixing", "Fixing assets..."));
			SlowTask.MakeDialog();
			for (const TPair<FSoftObjectPath, TArray<const FAssetIssue*>>& Group : Groups)
			{
				SlowTask.EnterProgressFrame(1.0f, FText::FromString(Group.Key.GetAssetName()));
				UObject* Object = Group.Key.TryLoad();
				Objects.Add(Object);
				if (!Object)
				{
					continue;
				}

				bool bEditing = false;
				for (const FAssetIssue* Issue : Group.Value)
				{
					if (Issue->FixSettings)
					{
						if (!bEditing)
						{
							Object->PreEditChange(nullptr);
							Object->Modify();
							bEditing = true;
						}
						Issue->FixSettings(Object);
					}
				}
				if (bEditing)
				{
					Object->PostEditChange();
					Object->MarkPackageDirty();
					++Result.NumSettingsFixed;
				}

				for (const FAssetIssue* Issue : Group.Value)
				{
					const FString& NewName = Issue->GetRenameTo(Options.bReplaceUnknownPrefix, Options.bReplaceUnknownSuffix);
					if (NewName.IsEmpty())
					{
						continue;
					}
					UObject* Target = Issue->RenameObject.IsNull() ? Object : Issue->RenameObject.TryLoad();
					const bool bQueued = Renames.ContainsByPredicate([Target](const FRename& Rename) { return Rename.Object.Get() == Target; });
					if (Target && Target->GetName() != NewName && !bQueued)
					{
						Renames.Add({ Target, NewName });
					}
				}
			}
		}

		// 2. Rename the ones whose new name is free
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		TArray<FAssetRenameData> RenameData;
		TSet<FString> Claimed;
		for (const FRename& Rename : Renames)
		{
			UObject* Object = Rename.Object.Get();
			if (!Object)
			{
				continue;
			}
			const FString PackagePath = FPackageName::GetLongPackagePath(Object->GetOutermost()->GetName());
			const FString NewObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *Rename.NewName, *Rename.NewName);
			if (Claimed.Contains(NewObjectPath) || AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(NewObjectPath)).IsValid())
			{
				Result.SkippedRenames.Add(FString::Printf(TEXT("%s -> %s"), *Object->GetName(), *Rename.NewName));
				continue;
			}
			Claimed.Add(NewObjectPath);
			RenameData.Emplace(Object, PackagePath, Rename.NewName);
		}
		if (!RenameData.IsEmpty())
		{
			FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get().RenameAssets(RenameData);
		}
		for (const FAssetRenameData& Rename : RenameData)
		{
			// A rename can be refused (e.g. at source control checkout): check what actually happened
			const UObject* Object = Rename.Asset.Get();
			if (Object && Object->GetName() == Rename.NewName)
			{
				++Result.NumRenamed;
			}
			else
			{
				Result.SkippedRenames.Add(FString::Printf(TEXT("%s -> %s"), Object ? *Object->GetName() : TEXT("?"), *Rename.NewName));
			}
		}

		// 3. The fixed assets, under their new names
		for (int32 Index = 0; Index < Groups.Num(); ++Index)
		{
			Result.OldPaths.Add(Groups[Index].Key);
			if (const UObject* Object = Objects[Index].Get())
			{
				Result.FixedAssets.Emplace(Object);
			}
		}
		return Result;
	}

	/**
	 * {
	 *   "section": "Texture Validation", "exported": "<ISO 8601 time>", "scanned": 120, "errors": 3, "warnings": 9,
	 *   "assets": [ { "name": "T_Rock", "path": "/Game/...", "class": "Texture2D",
	 *                 "issues": [ { "severity": "Error", "check": "Settings", "message": "...", "fixable": true } ] } ]
	 * }
	 */
	bool ExportIssues(const FString& FilePath, const FString& Section, int32 NumScanned, const TArray<const FAssetIssue*>& Issues)
	{
		int32 NumErrors = 0;
		TArray<TSharedPtr<FJsonValue>> AssetValues;
		for (const TPair<FSoftObjectPath, TArray<const FAssetIssue*>>& Group : GroupByAsset(Issues, false))
		{
			const FAssetData& Asset = Group.Value[0]->Asset;
			TArray<TSharedPtr<FJsonValue>> IssueValues;
			for (const FAssetIssue* Issue : Group.Value)
			{
				const bool bError = Issue->Severity == FAssetIssue::ESeverity::Error;
				NumErrors += bError ? 1 : 0;
				TSharedRef<FJsonObject> IssueObject = MakeShared<FJsonObject>();
				IssueObject->SetStringField(TEXT("severity"), bError ? TEXT("Error") : TEXT("Warning"));
				IssueObject->SetStringField(TEXT("check"), Issue->Rule.ToString());
				IssueObject->SetStringField(TEXT("message"), Issue->Message.ToString());
				IssueObject->SetBoolField(TEXT("fixable"), Issue->CanFix());
				IssueValues.Add(MakeShared<FJsonValueObject>(IssueObject));
			}
			TSharedRef<FJsonObject> AssetObject = MakeShared<FJsonObject>();
			AssetObject->SetStringField(TEXT("name"), Asset.AssetName.ToString());
			AssetObject->SetStringField(TEXT("path"), Asset.GetObjectPathString());
			AssetObject->SetStringField(TEXT("class"), Asset.AssetClassPath.GetAssetName().ToString());
			AssetObject->SetArrayField(TEXT("issues"), IssueValues);
			AssetValues.Add(MakeShared<FJsonValueObject>(AssetObject));
		}

		TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("section"), Section);
		Root->SetStringField(TEXT("exported"), FDateTime::Now().ToIso8601());
		Root->SetNumberField(TEXT("scanned"), NumScanned);
		Root->SetNumberField(TEXT("errors"), NumErrors);
		Root->SetNumberField(TEXT("warnings"), Issues.Num() - NumErrors);
		Root->SetArrayField(TEXT("assets"), AssetValues);

		FString Json;
		const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
		return FJsonSerializer::Serialize(Root, Writer) && FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
	}

	void SetRenameChoices(FAssetIssue& Issue, const FString& CurrentName, const FString (&Names)[2][2])
	{
		const FString& Default = Names[0][0];
		auto Choice = [&Default](const FString& Name) { return Name == Default ? FString() : Name; };
		Issue.RenameTo = Default == CurrentName ? FString() : Default;
		Issue.RenameReplacingPrefix = Choice(Names[1][0]);
		Issue.RenameReplacingSuffix = Choice(Names[0][1]);
		Issue.RenameReplacingBoth = Choice(Names[1][1]);
		if (Issue.RenameTo.IsEmpty() && !Issue.HasPrefixChoice() && !Issue.HasSuffixChoice())
		{
			Issue.RenameReplacingBoth.Reset();
		}
	}

	FString RemoveFirstToken(const FString& Name)
	{
		int32 Underscore;
		return Name.FindChar(TEXT('_'), Underscore) ? CleanNameBody(Name.Mid(Underscore + 1)) : FString();
	}

	bool IssueLess(const FAssetIssue& A, const FAssetIssue& B)
	{
		if (A.Severity != B.Severity)
		{
			return A.Severity == FAssetIssue::ESeverity::Error;
		}
		return A.Asset.AssetName.LexicalLess(B.Asset.AssetName);
	}

	/** What is wrong with the name after its prefix (spaces or symbols, double or trailing underscores), or empty. */
	FText GetNameBodyProblem(const FString& Body)
	{
		if (Body.IsEmpty())
		{
			return LOCTEXT("EmptyName", "Nothing after the prefix");
		}
		for (const TCHAR Char : Body)
		{
			if (!FChar::IsAlnum(Char) && Char != TEXT('_'))
			{
				return LOCTEXT("BadCharacters", "Name has spaces or symbols (use letters, digits and _)");
			}
		}
		if (Body.Contains(TEXT("__")))
		{
			return LOCTEXT("DoubleUnderscore", "Name has a double underscore");
		}
		if (Body.EndsWith(TEXT("_")) || Body.StartsWith(TEXT("_")))
		{
			return LOCTEXT("EdgeUnderscore", "Name starts or ends with an underscore");
		}
		return FText::GetEmpty();
	}

	FString CleanNameBody(const FString& Body)
	{
		FString Clean;
		Clean.Reserve(Body.Len());
		for (const TCHAR Char : Body)
		{
			Clean.AppendChar(FChar::IsAlnum(Char) || Char == TEXT('_') ? Char : TEXT('_'));
		}
		while (Clean.ReplaceInline(TEXT("__"), TEXT("_")) > 0)
		{
		}
		while (Clean.RemoveFromStart(TEXT("_")))
		{
		}
		while (Clean.RemoveFromEnd(TEXT("_")))
		{
		}
		return Clean;
	}
}

#undef LOCTEXT_NAMESPACE
