// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Shared editor helpers for the Toolbox core code (Core_*.cpp).
 *
 * Everything here is small and stateless, and used by more than one tool:
 *  - Content paths and the Content Browser (folders, selection, unique names, saving).
 *  - Texture assets: FindOrCreateTexture is how every baker gets the texture it writes (overwrite or new name).
 *  - Meshes: Static and Skeletal Meshes read the same way. The bakers work on FMeshDescription (the editable
 *    source data an asset is built from), so they don't need to care which of the two mesh types they were given.
 *  - Texture baking: the UV-space rasterizing math (Core_Common.h) and PadIslands, the island dilation
 *    every baker runs before writing.
 */

#include "Core_Common.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "ContentBrowserModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"

namespace VrrCore
{
	/** Cleans up a typed content path: forward slashes, a leading "/", no trailing "/". */
	FString NormalizeContentFolder(const FString& Folder, const FString& Default)
	{
		FString Result = Folder.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		while (Result.EndsWith(TEXT("/")))
		{
			Result.LeftChopInline(1);
		}
		// Accept "Game/Textures" or "/Game/Textures"
		if (!Result.IsEmpty() && !Result.StartsWith(TEXT("/")))
		{
			Result = TEXT("/") + Result;
		}
		return Result.IsEmpty() ? Default : Result;
	}

	/** The Content Browser's current path as a content path ("/Game/..."), or empty for virtual folders. */
	FString GetContentBrowserFolder()
	{
		FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
		return ContentBrowser.Get().GetCurrentPath().GetInternalPathString();
	}

	/** The selection in the primary Content Browser. */
	TArray<FAssetData> GetSelectedAssets()
	{
		TArray<FAssetData> Selected;
		FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser").Get().GetSelectedAssets(Selected);
		return Selected;
	}

	/** Selects the assets in the Content Browser (opening their folder). Does nothing for an empty list. */
	void ShowInContentBrowser(const TArray<UObject*>& Assets)
	{
		if (!Assets.IsEmpty())
		{
			FContentBrowserModule& ContentBrowser = FModuleManager::LoadModuleChecked<FContentBrowserModule>("ContentBrowser");
			ContentBrowser.Get().SyncBrowserToAssets(Assets);
		}
	}

	/** A free package path for a new asset: Name, then Name_01, Name_02... Never returns a taken name. */
	bool MakeUniqueAssetPath(const FString& Folder, const FString& Name, FString& OutPackageName, FString& OutAssetName)
	{
		// 1. Reject paths Unreal can't save to (empty name, bad characters, not under a mount point)
		OutAssetName = Name;
		OutPackageName = Folder / Name;
		if (Name.IsEmpty() || !FPackageName::IsValidLongPackageName(OutPackageName))
		{
			return false;
		}

		// 2. Count up until neither a loaded package nor a file on disk uses the name
		for (int32 Suffix = 1; FindPackage(nullptr, *OutPackageName) || FPackageName::DoesPackageExist(OutPackageName); ++Suffix)
		{
			OutAssetName = FString::Printf(TEXT("%s_%02d"), *Name, Suffix);
			OutPackageName = Folder / OutAssetName;
		}
		return true;
	}

	/** Strips the first matching type prefix, so output names read T_Rock_..., not T_SM_Rock_... */
	FString GetBaseAssetName(const UObject* Asset)
	{
		FString Name = Asset ? Asset->GetName() : FString();
		// SKM_ before SK_, so "SKM_Hero" doesn't become "M_Hero"
		for (const TCHAR* Prefix : { TEXT("SKM_"), TEXT("SM_"), TEXT("SK_") })
		{
			if (Name.RemoveFromStart(Prefix))
			{
				break;
			}
		}
		return Name;
	}

	UObject* GetFirstSelectedAsset(TFunctionRef<bool(const FAssetData&)> Accept)
	{
		for (const FAssetData& Asset : GetSelectedAssets())
		{
			if (Accept(Asset))
			{
				return Asset.GetAsset();
			}
		}
		return nullptr;
	}

	bool SaveAssets(const TArray<UObject*>& Assets)
	{
		TArray<UPackage*> Packages;
		for (const UObject* Asset : Assets)
		{
			if (Asset)
			{
				Packages.AddUnique(Asset->GetOutermost());
			}
		}
		return Packages.IsEmpty() || UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
	}

	UTexture2D* FindOrCreateTexture(const FString& Folder, const FString& Name, bool bOverwrite, FText& OutError)
	{
		// 1. Reject paths Unreal can't save to
		const FString PackageName = Folder / Name;
		if (Name.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
		{
			OutError = FText::Format(NSLOCTEXT("VrrCore", "BadTexturePath", "Invalid output path: {0}"), FText::FromString(PackageName));
			return nullptr;
		}

		// 2. Overwrite: reuse the texture already at that name (a different asset type there falls through to a new name)
		if (bOverwrite && (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName)))
		{
			if (UTexture2D* Existing = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				return Existing;
			}
		}

		// 3. A new texture at the first free name, registered so the Content Browser shows it
		FString NewPackageName, AssetName;
		MakeUniqueAssetPath(Folder, Name, NewPackageName, AssetName);
		UPackage* Package = CreatePackage(*NewPackageName);
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Texture);
		return Texture;
	}

	/** True for the two mesh types the bakers accept. */
	bool IsStaticOrSkeletalMesh(const UObject* Object)
	{
		return Cast<UStaticMesh>(Object) || Cast<USkeletalMesh>(Object);
	}

	bool IsSkeletalMesh(const UObject* Object)
	{
		return Cast<USkeletalMesh>(Object) != nullptr;
	}

	bool IsMeshAsset(const FAssetData& Asset, bool bSkeletalOnly)
	{
		return Asset.IsInstanceOf(USkeletalMesh::StaticClass()) || (!bSkeletalOnly && Asset.IsInstanceOf(UStaticMesh::StaticClass()));
	}

	/**
	 * The editable source data of one LOD. Static and Skeletal Meshes store it differently,
	 * but both hand out an FMeshDescription, which is all the bakers read and write.
	 */
	FMeshDescription* GetMeshDescription(UObject* Mesh, int32 LODIndex)
	{
		if (UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			return StaticMesh->IsMeshDescriptionValid(LODIndex) ? StaticMesh->GetMeshDescription(LODIndex) : nullptr;
		}
		if (USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			return SkeletalMesh->HasMeshDescription(LODIndex) ? SkeletalMesh->GetMeshDescription(LODIndex) : nullptr;
		}
		return nullptr;
	}

	/** LODs that were imported (have source data). LODs generated by reduction are skipped: they're rebuilt from LOD 0. */
	TArray<int32> GetLODsWithMeshData(const UObject* Mesh)
	{
		TArray<int32> LODs;
		if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			for (int32 LOD = 0; LOD < StaticMesh->GetNumSourceModels(); ++LOD)
			{
				if (StaticMesh->IsMeshDescriptionValid(LOD))
				{
					LODs.Add(LOD);
				}
			}
		}
		else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			for (int32 LOD = 0; LOD < SkeletalMesh->GetLODNum(); ++LOD)
			{
				if (SkeletalMesh->HasMeshDescription(LOD))
				{
					LODs.Add(LOD);
				}
			}
		}
		return LODs;
	}

	void PadIslands(int32 Size, int32 Passes, TBitArray<>& Filled, TFunctionRef<void(int32 Index, TConstArrayView<int32> FilledNeighbors)> FillTexel)
	{
		TArray<int32> NewlyFilled;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
			// 1. Every empty texel with filled neighbors gets its value from them. They're marked filled only after
			//    the pass, so a texel filled in this pass doesn't feed its neighbors until the next one (even growth).
			NewlyFilled.Reset();
			for (int32 Y = 0; Y < Size; ++Y)
			{
				for (int32 X = 0; X < Size; ++X)
				{
					const int32 Index = Y * Size + X;
					if (Filled[Index])
					{
						continue;
					}
					int32 Neighbors[8];
					int32 NumNeighbors = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if ((DX || DY) && NX >= 0 && NX < Size && NY >= 0 && NY < Size && Filled[NY * Size + NX])
							{
								Neighbors[NumNeighbors++] = NY * Size + NX;
							}
						}
					}
					if (NumNeighbors > 0)
					{
						FillTexel(Index, TConstArrayView<int32>(Neighbors, NumNeighbors));
						NewlyFilled.Add(Index);
					}
				}
			}

			// 2. Stop once the islands have nothing left to grow into
			if (NewlyFilled.IsEmpty())
			{
				break;
			}
			for (const int32 Index : NewlyFilled)
			{
				Filled[Index] = true;
			}
		}
	}

	/** Number of UV sets on LOD 0, for the bakers' UV channel pickers. */
	int32 GetNumUVChannels(UObject* Mesh)
	{
		if (const FMeshDescription* MeshDescription = GetMeshDescription(Mesh, 0))
		{
			FStaticMeshConstAttributes Attributes(*MeshDescription);
			return Attributes.GetVertexInstanceUVs().GetNumChannels();
		}
		return 0;
	}
}
