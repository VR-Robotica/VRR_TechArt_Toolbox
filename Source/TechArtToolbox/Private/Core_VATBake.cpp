// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Toolbox > VAT page (the work, no UI).
 *
 * The baking itself is Unreal's AnimToTexture plugin. It reads everything from a UAnimToTextureDataAsset,
 * which holds one static mesh, one LOD and one set of textures. So each skeletal LOD gets its own static mesh
 * copy and Data Asset, and the baked LOD meshes are merged into one static mesh at the end.
 *
 * Create (CreateAssets):
 *   1. Pick a base name that's free for every asset of the set
 *   2. Convert each skeletal LOD to SM_<Name>_LOD#, and choose one free UV channel for the VAT UVs on all of them
 *   3. Create the empty textures (Bone: shared position/rotation + a weight texture per LOD; Vertex: position/normal per LOD)
 *   4. Create DA_<Name>_LOD# pointing at its skeletal LOD, its static mesh and its textures, with the page's
 *      texture size, precision, bone influences and (seamless loops) frame ranges that drop each last key
 *
 * Bake:
 *   1. Run the plugin on each Data Asset (writes the textures and the VAT UV channel of SM_<Name>_LOD#)
 *   2. MI_<Name>, an instance of the master material (default from Project Settings) with
 *      the baked settings from the LOD0 Data Asset (rows per frame, sample rate, bounds, bone count, textures,
 *      animation frame ranges, UV/influence switches). Only parameters the master has are set.
 *   3. Copy the baked LOD meshes into SM_<Name> as LOD0..N, with LOD screen sizes from the skeletal mesh,
 *      a material slot per LOD (each LOD reads its own textures) and bounds that cover the animation.
 *      Each slot gets MI_<Name>_<Slot>_LOD#, a child of MI_<Name> with that LOD's own textures and values.
 *   4. Optionally place (or update) a preview actor showing SM_<Name> at the level origin
 *
 * Re-baking updates the existing material instances and preview actor in place, so other edits are kept.
 */

#include "Core_VATBake.h"

#include "AnimToTextureBPLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkinnedAssetCommon.h"
#include "Engine/StaticMesh.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "EngineUtils.h"
#include "MaterialEditingLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/EngineVersionComparison.h"
#include "Misc/PackageName.h"
#include "Misc/ScopedSlowTask.h"
#include "ObjectTools.h"
#include "ScopedTransaction.h"
#include "StaticMeshCompiler.h"
#include "StaticMeshResources.h"
#include "TechArtToolboxSettings.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "VatBaker"

namespace VatBakerImpl
{
	/** Material static switches only go up to UV3, so the VAT UVs must land in channel 0-3. */
	constexpr int32 MaxVatUVChannel = 3;

	/** AnimToTexture's 8-bit weight textures store bone indices in 0-255. */
	constexpr int32 MaxBonesEightBit = 256;

	/** The parameter names a VAT master material uses (listed in the README's VAT section). */
	namespace VatParams
	{
		const FName RowsPerFrame = TEXT("Rows Per Frame");
		const FName SampleRate = TEXT("Sample Rate");
		const FName PlayRate = TEXT("Play Rate");
		const FName TimeOffset = TEXT("Time Offset");
		const FName HeightOffset = TEXT("VAT Height Offset");
		const FName RotationOffset = TEXT("VAT Rotation Offset");
		const FName NumBones = TEXT("Number of Bones");
		const FName UVSetSelection = TEXT("VAT UV Set Selection");
		const FName MinBBox = TEXT("Atlas - MinBBox");
		const FName SizeBBox = TEXT("Atlas - SizeBBox");
		const FName BonePosition = TEXT("Atlas - Bone Position");
		const FName BoneRotation = TEXT("Atlas - Bone Rotation");
		const FName BoneWeights = TEXT("Atlas - Bone Weights");
		const FName VertexPosition = TEXT("Atlas - Vertex Position");
		const FName VertexNormal = TEXT("Atlas - Vertex Normal");
		const FName AnimSelection = TEXT("VAT Selection");
		const FName UseTwoInfluences = TEXT("Use Two Influences");
		const FName UseFourInfluences = TEXT("Use Four Influences");

		FName UseUV(int32 Channel)			{ return FName(*FString::Printf(TEXT("UseUV%d"), Channel)); }
		FName AnimStartFrame(int32 Index)	{ return FName(*FString::Printf(TEXT("Anim%d - Start Frame"), Index)); }
		FName AnimEndFrame(int32 Index)		{ return FName(*FString::Printf(TEXT("Anim%d - End Frame"), Index)); }

		/** What a master material must have for each mode; everything else is set only when the material has it. */
		TArray<FName> Required(EAnimToTextureMode Mode)
		{
			TArray<FName> Names = { RowsPerFrame, SampleRate, MinBBox, SizeBBox, AnimStartFrame(0), AnimEndFrame(0) };
			if (Mode == EAnimToTextureMode::Bone)
			{
				Names.Append({ NumBones, BonePosition, BoneRotation, BoneWeights });
			}
			else
			{
				Names.Append({ VertexPosition, VertexNormal });
			}
			return Names;
		}
	}

	/** A master material's global parameter names, by type. */
	struct FMaterialParams
	{
		TSet<FName> Scalars, Vectors, Textures, Switches;

		explicit FMaterialParams(const UMaterialInterface* Material)
		{
			if (!Material)
			{
				return;
			}
			auto Gather = [Material](EMaterialParameterType Type, TSet<FName>& Out)
			{
				TArray<FMaterialParameterInfo> Infos;
				TArray<FGuid> Ids;
				Material->GetAllParameterInfoOfType(Type, Infos, Ids);
				for (const FMaterialParameterInfo& Info : Infos)
				{
					if (Info.Association == EMaterialParameterAssociation::GlobalParameter)
					{
						Out.Add(Info.Name);
					}
				}
			};
			Gather(EMaterialParameterType::Scalar, Scalars);
			Gather(EMaterialParameterType::Vector, Vectors);
			Gather(EMaterialParameterType::Texture, Textures);
			Gather(EMaterialParameterType::StaticSwitch, Switches);
		}

		bool Has(FName Name) const
		{
			return Scalars.Contains(Name) || Vectors.Contains(Name) || Textures.Contains(Name) || Switches.Contains(Name);
		}

		/** Anim0, Anim1... until a "Start Frame" parameter is missing. */
		int32 NumAnimationSlots() const
		{
			int32 Count = 0;
			while (Scalars.Contains(VatParams::AnimStartFrame(Count)) && Scalars.Contains(VatParams::AnimEndFrame(Count)))
			{
				++Count;
			}
			return Count;
		}
	};

	/** Sets material instance parameters, skipping the ones its master material doesn't have. */
	struct FParamWriter
	{
		UMaterialInstanceConstant* Instance;
		const FMaterialParams& Params;

		void Scalar(FName Name, float Value) const
		{
			if (Params.Scalars.Contains(Name))
			{
				UMaterialEditingLibrary::SetMaterialInstanceScalarParameterValue(Instance, Name, Value);
			}
		}
		void Vector(FName Name, const FLinearColor& Value) const
		{
			if (Params.Vectors.Contains(Name))
			{
				UMaterialEditingLibrary::SetMaterialInstanceVectorParameterValue(Instance, Name, Value);
			}
		}
		void Texture(FName Name, UTexture* Value) const
		{
			if (Params.Textures.Contains(Name))
			{
				UMaterialEditingLibrary::SetMaterialInstanceTextureParameterValue(Instance, Name, Value);
			}
		}
		/** The instance is updated once at the end (UMaterialEditingLibrary::UpdateMaterialInstance). */
		void Switch(FName Name, bool bValue) const
		{
			if (Params.Switches.Contains(Name))
			{
				UMaterialEditingLibrary::SetMaterialInstanceStaticSwitchParameterValue(Instance, Name, bValue, EMaterialParameterAssociation::GlobalParameter, false);
			}
		}
	};

	FString LODSuffix(int32 LOD)
	{
		return FString::Printf(TEXT("_LOD%d"), LOD);
	}

	/** Every asset name of a set, so a base name can be checked before anything is created. */
	struct FSetNames
	{
		FString BaseName;
		int32 NumLODs = 0;
		EAnimToTextureMode Mode = EAnimToTextureMode::Bone;

		FString DataAsset(int32 LOD) const		{ return TEXT("DA_") + BaseName + LODSuffix(LOD); }
		FString LODMesh(int32 LOD) const		{ return TEXT("SM_") + BaseName + LODSuffix(LOD); }
		FString CombinedMesh() const			{ return TEXT("SM_") + BaseName; }
		FString BaseMaterial() const			{ return TEXT("MI_") + BaseName; }
		FString BonePosition() const			{ return TEXT("VAT_") + BaseName + TEXT("_BonePos"); }
		FString BoneRotation() const			{ return TEXT("VAT_") + BaseName + TEXT("_BoneRot"); }
		FString BoneWeights(int32 LOD) const	{ return TEXT("VAT_") + BaseName + TEXT("_BoneWeights") + LODSuffix(LOD); }
		FString VertexPosition(int32 LOD) const	{ return TEXT("VAT_") + BaseName + TEXT("_VertexPos") + LODSuffix(LOD); }
		FString VertexNormal(int32 LOD) const	{ return TEXT("VAT_") + BaseName + TEXT("_VertexNormal") + LODSuffix(LOD); }

		TArray<FString> All() const
		{
			TArray<FString> Names = { CombinedMesh() };
			if (Mode == EAnimToTextureMode::Bone)
			{
				Names.Append({ BonePosition(), BoneRotation(), BaseMaterial() });
			}
			for (int32 LOD = 0; LOD < NumLODs; ++LOD)
			{
				Names.Append({ DataAsset(LOD), LODMesh(LOD) });
				if (Mode == EAnimToTextureMode::Bone)
				{
					Names.Add(BoneWeights(LOD));
				}
				else
				{
					Names.Append({ VertexPosition(LOD), VertexNormal(LOD) });
				}
			}
			return Names;
		}
	};

	/**
	 * True when an asset lives at this package path, in memory or on disk. An empty in-memory package doesn't
	 * count: a failed LoadObject of a missing asset leaves one behind, and it would make every free name look taken.
	 */
	bool IsPackageTaken(const FString& PackageName)
	{
		UPackage* Package = FindPackage(nullptr, *PackageName);
		if (Package && FindObject<UObject>(Package, *FPackageName::GetShortName(PackageName)))
		{
			return true;
		}
		return FPackageName::DoesPackageExist(PackageName);
	}

	/**
	 * The asset an earlier bake left at PackageName (in memory or on disk), to update in place. Only loads packages
	 * that exist on disk, so looking up a free name doesn't leave an empty package behind.
	 * bOutTaken is true when something else (another class) is there.
	 */
	template<typename AssetType>
	AssetType* FindExistingAsset(const FString& PackageName, bool& bOutTaken)
	{
		const FString Name = FPackageName::GetShortName(PackageName);
		bOutTaken = false;
		if (UPackage* Package = FindPackage(nullptr, *PackageName))
		{
			if (UObject* Existing = FindObject<UObject>(Package, *Name))
			{
				AssetType* Asset = Cast<AssetType>(Existing);
				bOutTaken = Asset == nullptr;
				return Asset;
			}
		}
		if (FPackageName::DoesPackageExist(PackageName))
		{
			AssetType* Asset = LoadObject<AssetType>(nullptr, *(PackageName + TEXT(".") + Name), nullptr, LOAD_NoWarn | LOAD_Quiet);
			bOutTaken = Asset == nullptr;
			return Asset;
		}
		return nullptr;
	}

	/** Name, then Name_01, Name_02... until no asset of the set exists yet. False when the path can't be saved to. */
	bool PickFreeBaseName(const FString& Folder, const FString& Name, FSetNames& InOutNames)
	{
		if (Name.IsEmpty() || !FPackageName::IsValidLongPackageName(Folder / Name))
		{
			return false;
		}

		for (int32 Suffix = 0; ; ++Suffix)
		{
			InOutNames.BaseName = Suffix == 0 ? Name : FString::Printf(TEXT("%s_%02d"), *Name, Suffix);
			const bool bAnyTaken = InOutNames.All().ContainsByPredicate([&Folder](const FString& Asset) { return IsPackageTaken(Folder / Asset); });
			if (!bAnyTaken)
			{
				return true;
			}
		}
	}

	/** An empty (1x1) VAT texture: linear, Nearest, no mips, HDR. The plugin replaces its pixels when baking. */
	UTexture2D* CreateVatTexture(const FString& Folder, const FString& Name)
	{
		UPackage* Package = CreatePackage(*(Folder / Name));
		UTexture2D* Texture = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);

		const FColor Black(0, 0, 0, 0);
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(1, 1, 1, 1, TSF_BGRA8, reinterpret_cast<const uint8*>(&Black));
		Texture->SRGB = false;
		Texture->Filter = TF_Nearest;
		Texture->CompressionSettings = TC_HDR;	// the plugin resets this when baking; UseHDRCompression sets it again after
		Texture->MipGenSettings = TMGS_NoMipmaps;
		Texture->PostEditChange();

		FAssetRegistryModule::AssetCreated(Texture);
		Package->MarkPackageDirty();
		return Texture;
	}

	/**
	 * Sets HDR compression on a baked set's textures, as the Texture Validator expects for VAT_ textures.
	 * The plugin writes 8-bit bakes as Vector Displacement (RGBA8) and 16-bit ones as HDR; the 8-bit values
	 * come through unchanged as 16-bit floats, so the material reads the same data.
	 */
	void UseHDRCompression(UAnimToTextureDataAsset* DataAsset)
	{
		const TArray<UTexture2D*> Textures =
		{
			DataAsset->GetBonePositionTexture(), DataAsset->GetBoneRotationTexture(), DataAsset->GetBoneWeightTexture(),
			DataAsset->GetVertexPositionTexture(), DataAsset->GetVertexNormalTexture()
		};
		for (UTexture2D* Texture : Textures)
		{
			if (Texture && Texture->CompressionSettings != TC_HDR)
			{
				Texture->PreEditChange(nullptr);
				Texture->CompressionSettings = TC_HDR;
				Texture->PostEditChange();
				Texture->MarkPackageDirty();
			}
		}
	}

	/**
	 * Gets a converted LOD mesh ready for the plugin: no generated lightmap UVs (the converter puts them in the
	 * channel the VAT UVs need, and an animated mesh is movable anyway), and empty UV channels up to VatUVChannel
	 * so the plugin can add its channel at that index.
	 */
	void PrepareLODMesh(UStaticMesh* Mesh, int32 VatUVChannel)
	{
		Mesh->GetSourceModel(0).BuildSettings.bGenerateLightmapUVs = false;
		while (Mesh->GetNumUVChannels(0) < VatUVChannel)
		{
			Mesh->AddUVChannel(0);
		}
		Mesh->PostEditChange();
		Mesh->MarkPackageDirty();
	}

	/** A LOD mesh section's slot name: the slot name, else the material's name, else "Section#". */
	FString SourceSlotName(const FStaticMaterial& Material, int32 SectionIndex)
	{
		return !Material.MaterialSlotName.IsNone() ? Material.MaterialSlotName.ToString()
			: Material.MaterialInterface ? Material.MaterialInterface->GetName()
			: FString::Printf(TEXT("Section%d"), SectionIndex);
	}

	/** Slot name for a material in the combined mesh: its own slot name on LOD0, "<slot>_LOD#" on the other LODs. */
	FName CombinedSlotName(const FString& SourceSlot, int32 LOD)
	{
		return FName(*(LOD > 0 ? SourceSlot + LODSuffix(LOD) : SourceSlot));
	}

	/** The material instance of an earlier bake (updated in place), or a new one. Its parent is set to Parent. */
	UMaterialInstanceConstant* FindOrCreateInstance(const FString& Folder, const FString& Name, UMaterialInterface* Parent, FText& OutError)
	{
		const FString PackageName = Folder / Name;
		bool bTaken = false;
		UMaterialInstanceConstant* Instance = FindExistingAsset<UMaterialInstanceConstant>(PackageName, bTaken);
		if (!Instance)
		{
			if (bTaken)
			{
				OutError = FText::Format(LOCTEXT("InstanceTaken", "{0} already exists and isn't a material instance."), FText::FromString(PackageName));
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			Instance = NewObject<UMaterialInstanceConstant>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Instance);
		}

		if (Instance->Parent != Parent)
		{
			Instance->SetParentEditorOnly(Parent);
		}
		Instance->MarkPackageDirty();
		return Instance;
	}

	bool IsScalarOverridden(const UMaterialInstanceConstant* Instance, FName Name)
	{
		return Instance->ScalarParameterValues.ContainsByPredicate([Name](const FScalarParameterValue& Value) { return Value.ParameterInfo.Name == Name; });
	}

	/**
	 * The parameters that differ per LOD: the LOD's own textures, the values that depend on its vertex count, and
	 * the material editor's preview mesh (the LOD's static mesh).
	 * Bone: weight texture and its rows per frame ("Rows Per Frame" feeds ML_VAT's BoneWeightsRowsPerFrame input;
	 *       the bone position/rotation lookup assumes one row per frame).
	 * Vertex: position/normal textures, rows per frame and bounds.
	 */
	void WriteLODParams(const FParamWriter& Writer, const UAnimToTextureDataAsset* DataAsset)
	{
		// Preview on the LOD's own baked mesh (SM_<Name>_LOD#): its VAT UVs match this LOD's textures
		Writer.Instance->PreviewMesh = DataAsset->StaticMesh.ToSoftObjectPath();

		if (DataAsset->Mode == EAnimToTextureMode::Bone)
		{
			Writer.Texture(VatParams::BoneWeights, DataAsset->GetBoneWeightTexture());
			Writer.Scalar(VatParams::RowsPerFrame, DataAsset->BoneWeightRowsPerFrame);
		}
		else
		{
			Writer.Texture(VatParams::VertexPosition, DataAsset->GetVertexPositionTexture());
			Writer.Texture(VatParams::VertexNormal, DataAsset->GetVertexNormalTexture());
			Writer.Scalar(VatParams::RowsPerFrame, DataAsset->VertexRowsPerFrame);
			Writer.Vector(VatParams::MinBBox, FLinearColor(DataAsset->VertexMinBBox));
			Writer.Vector(VatParams::SizeBBox, FLinearColor(DataAsset->VertexSizeBBox));
		}
	}

	/**
	 * MI_<Name>: the master material with the settings the bake wrote to the LOD0 Data Asset. Each slot's instance
	 * then overrides only its LOD's values (WriteLODParams). OutNote gets a warning when the Data Asset has more
	 * animations than the material has frame ranges for, or the material lacks required parameters.
	 */
	UMaterialInstanceConstant* CreateBaseMaterial(const FVatAssetSet& Set, UMaterialInterface* Master, const UAnimToTextureDataAsset* DataAsset, FText& OutError, FText& OutNote)
	{
		UMaterialInstanceConstant* Instance = FindOrCreateInstance(Set.Folder, Set.GetBaseMaterialName(), Master, OutError);
		if (!Instance)
		{
			return nullptr;
		}

		const FMaterialParams Params(Master);
		const FParamWriter Writer{ Instance, Params };
		TArray<FText> Notes;

		// 1. Playback, plus the bone data every LOD shares
		Writer.Scalar(VatParams::SampleRate, DataAsset->SampleRate);
		if (DataAsset->Mode == EAnimToTextureMode::Bone)
		{
			Writer.Scalar(VatParams::NumBones, DataAsset->NumBones);
			Writer.Vector(VatParams::MinBBox, FLinearColor(DataAsset->BoneMinBBox));
			Writer.Vector(VatParams::SizeBBox, FLinearColor(DataAsset->BoneSizeBBox));
			Writer.Texture(VatParams::BonePosition, DataAsset->GetBonePositionTexture());
			Writer.Texture(VatParams::BoneRotation, DataAsset->GetBoneRotationTexture());
		}
		WriteLODParams(Writer, DataAsset);

		// 2. The Data Asset has no play rate, time offset or height/rotation offset (RootTransform is baked into the
		//    textures). Override them with the material's defaults so they're ready to edit here; a re-bake keeps edits.
		for (const FName& Name : { VatParams::PlayRate, VatParams::TimeOffset, VatParams::HeightOffset, VatParams::RotationOffset })
		{
			float Default = 0.0f;
			if (Params.Scalars.Contains(Name) && !IsScalarOverridden(Instance, Name) && Master->GetScalarParameterDefaultValue(FHashedMaterialParameterInfo(Name), Default))
			{
				Writer.Scalar(Name, Default);
			}
		}

		// 3. Frame range of each animation, in Data Asset order; unused ranges are zeroed so a re-bake with fewer animations leaves none behind
		const TArray<FAnimToTextureAnimInfo>& Animations = DataAsset->Animations;
		const int32 NumSlots = Params.NumAnimationSlots();
		for (int32 Index = 0; Index < NumSlots; ++Index)
		{
			const bool bUsed = Animations.IsValidIndex(Index);
			Writer.Scalar(VatParams::AnimStartFrame(Index), bUsed ? Animations[Index].StartFrame : 0);
			Writer.Scalar(VatParams::AnimEndFrame(Index), bUsed ? Animations[Index].EndFrame : 0);
		}
		if (Animations.Num() > NumSlots)
		{
			Notes.Add(FText::Format(LOCTEXT("TooManyAnims", "{0} has frame ranges for {1} animation(s); animations {1}-{2} were left out."),
				FText::FromString(Master->GetName()), NumSlots, Animations.Num() - 1));
		}

		// 4. Static switches: the VAT UV channel and the bone influences the Data Asset was set up for
		for (int32 Channel = 0; Channel <= MaxVatUVChannel; ++Channel)
		{
			Writer.Switch(VatParams::UseUV(Channel), Channel == DataAsset->UVChannel);
		}
		Writer.Scalar(VatParams::UVSetSelection, DataAsset->UVChannel);
		if (DataAsset->Mode == EAnimToTextureMode::Bone)
		{
			Writer.Switch(VatParams::UseTwoInfluences, DataAsset->NumBoneInfluences == EAnimToTextureNumBoneInfluences::Two);
			Writer.Switch(VatParams::UseFourInfluences, DataAsset->NumBoneInfluences == EAnimToTextureNumBoneInfluences::Four);

			// The master's bone lookup assumes the bone textures hold one row per frame
			if (DataAsset->BoneRowsPerFrame > 1)
			{
				Notes.Add(FText::Format(LOCTEXT("BoneRowsPerFrame", "The bone textures need {0} rows per frame; the VAT material reads one. Raise Max texture size and Create again."),
					DataAsset->BoneRowsPerFrame));
			}
		}

		// 5. Required parameters the master doesn't have
		TArray<FString> Missing;
		for (const FName& Name : VatParams::Required(DataAsset->Mode))
		{
			if (!Params.Has(Name))
			{
				Missing.Add(Name.ToString());
			}
		}
		if (!Missing.IsEmpty())
		{
			Notes.Add(FText::Format(LOCTEXT("MissingParams", "{0} is missing: {1}."), FText::FromString(Master->GetName()), FText::FromString(FString::Join(Missing, TEXT(", ")))));
		}

		UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
		OutNote = FText::Join(FText::FromString(TEXT(" ")), Notes);
		return Instance;
	}

	/** MI_<Name>_<Slot>_LOD#: child of the base instance with its LOD's textures and per-LOD values. */
	UMaterialInstanceConstant* CreateSlotMaterial(const FVatAssetSet& Set, UMaterialInstanceConstant* BaseMaterial, const FMaterialParams& Params,
		const FString& SourceSlot, int32 LOD, const UAnimToTextureDataAsset* DataAsset, FText& OutError)
	{
		const FString Name = Set.GetBaseMaterialName() + TEXT("_") + ObjectTools::SanitizeObjectName(SourceSlot) + LODSuffix(LOD);
		UMaterialInstanceConstant* Instance = FindOrCreateInstance(Set.Folder, Name, BaseMaterial, OutError);
		if (Instance)
		{
			WriteLODParams(FParamWriter{ Instance, Params }, DataAsset);
			UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
		}
		return Instance;
	}

	/** Sets a property by its (Blueprint variable) name from text, e.g. an asset path or a name. */
	bool SetPropertyFromText(UObject* Object, FName PropertyName, const FString& Value)
	{
		FProperty* Property = Object ? Object->GetClass()->FindPropertyByName(PropertyName) : nullptr;
		return Property && Property->ImportText_InContainer(*Value, Object, Object, PPF_None) != nullptr;
	}

	/**
	 * Places the preview actor for SM_<Name> at the level origin, or updates the one an earlier bake placed (found
	 * by its label). Its class, and an optional component driving "VAT Selection" so the animation can be switched on
	 * the actor, come from Project Settings > Plugins > TechArt Toolbox; without them it's a plain Static Mesh Actor.
	 */
	AActor* PlacePreviewActor(UStaticMesh* Mesh, bool bDriveSelection, FText& OutNote)
	{
		UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
		if (!World || !Mesh)
		{
			return nullptr;
		}

		const FScopedTransaction Transaction(LOCTEXT("PlacePreview", "Place VAT Preview Actor"));
		const FString Label = Mesh->GetName();
		const UTechArtToolboxSettings& ToolboxSettings = UTechArtToolboxSettings::Get();
		UClass* PreviewClass = ToolboxSettings.VatPreviewActorClass.LoadSynchronous();
		UClass* ActorClass = PreviewClass ? PreviewClass : AStaticMeshActor::StaticClass();

		// 1. The actor from an earlier bake (same label and class), or a new one at the origin
		AActor* Actor = nullptr;
		for (TActorIterator<AActor> It(World, ActorClass); It; ++It)
		{
			if (It->GetActorLabel() == Label)
			{
				Actor = *It;
				break;
			}
		}
		if (!Actor)
		{
			FActorSpawnParameters SpawnParams;
			SpawnParams.ObjectFlags = RF_Transactional;
			Actor = World->SpawnActor<AActor>(ActorClass, FTransform::Identity, SpawnParams);
			if (!Actor)
			{
				OutNote = LOCTEXT("PreviewFailed", "The preview actor couldn't be placed.");
				return nullptr;
			}
			Actor->SetActorLabel(Label);
		}
		Actor->Modify();

		// 2. Show the mesh
		if (AStaticMeshActor* MeshActor = Cast<AStaticMeshActor>(Actor))
		{
			MeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
		}
		else if (!SetPropertyFromText(Actor, ToolboxSettings.VatPreviewMeshVariable, Mesh->GetPathName()))
		{
			OutNote = FText::Format(LOCTEXT("PreviewNoMeshProperty", "The preview actor has no \"{0}\" variable; set its mesh by hand."),
				FText::FromName(ToolboxSettings.VatPreviewMeshVariable));
		}

		// 3. A component driving the animation selection (once per actor)
		UClass* ParameterClass = bDriveSelection ? ToolboxSettings.VatPreviewSelectionComponent.LoadSynchronous() : nullptr;
		if (ParameterClass && !Actor->FindComponentByClass(ParameterClass))
		{
			UActorComponent* Component = NewObject<UActorComponent>(Actor, ParameterClass, NAME_None, RF_Transactional);
			SetPropertyFromText(Component, ToolboxSettings.VatPreviewSelectionVariable, VatParams::AnimSelection.ToString());
			Actor->AddInstanceComponent(Component);
			Component->OnComponentCreated();
			Component->RegisterComponent();
		}

		Actor->RerunConstructionScripts();
		Actor->PostEditChange();

		GEditor->SelectNone(false, true);
		GEditor->SelectActor(Actor, true, true);
		return Actor;
	}

	/** The mesh's bounds without its bounds extensions. */
	FBox GetBaseBounds(const UStaticMesh* Mesh)
	{
		FBox Box = Mesh->GetBoundingBox();
		Box.Max -= Mesh->GetPositiveBoundsExtension();
		Box.Min += Mesh->GetNegativeBoundsExtension();
		return Box;
	}

	/**
	 * Copies the baked LOD meshes into one static mesh (created, or rebuilt in place on a re-bake).
	 * Each LOD keeps its geometry, VAT UVs and build settings, gets the skeletal mesh's LOD screen size,
	 * and its own material slots, since each LOD needs a material reading its own VAT textures.
	 * GetSlotMaterial gives each new slot its material (from the LOD's slot name); nullptr keeps the LOD mesh's material.
	 */
	UStaticMesh* CombineLODs(const FVatAssetSet& Set, const TArray<UStaticMesh*>& LODMeshes,
		TFunctionRef<UMaterialInterface*(const FString& SourceSlot, int32 LOD)> GetSlotMaterial, FText& OutError)
	{
		// 1. Find the mesh from an earlier bake, or create it
		const FString Name = Set.GetCombinedMeshName();
		const FString PackageName = Set.Folder / Name;
		bool bTaken = false;
		UStaticMesh* Combined = FindExistingAsset<UStaticMesh>(PackageName, bTaken);
		bool bCreated = false;
		if (!Combined)
		{
			if (bTaken)
			{
				OutError = FText::Format(LOCTEXT("CombinedTaken", "{0} already exists and isn't a static mesh."), FText::FromString(PackageName));
				return nullptr;
			}
			UPackage* Package = CreatePackage(*PackageName);
			Combined = NewObject<UStaticMesh>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);
			bCreated = true;
		}

		// The LOD meshes were just rebuilt by the plugin; their render data (sections) must be ready
		FStaticMeshCompilingManager::Get().FinishCompilation(LODMeshes);

		Combined->PreEditChange(nullptr);
		Combined->SetNumSourceModels(LODMeshes.Num());
		Combined->GetSectionInfoMap().Clear();
		Combined->GetOriginalSectionInfoMap().Clear();
#if UE_VERSION_OLDER_THAN(5, 7, 0)
		Combined->bAutoComputeLODScreenSize = false;
#else
		Combined->SetAutoComputeLODScreenSize(false);
#endif

		const USkeletalMesh* SkeletalMesh = Set.SkeletalMesh.Get();
		TArray<FStaticMaterial> Materials;
		FBox AnimatedBounds(ForceInit);

		for (int32 LOD = 0; LOD < LODMeshes.Num(); ++LOD)
		{
			UStaticMesh* LODMesh = LODMeshes[LOD];
			const FMeshDescription* MeshDescription = LODMesh->GetMeshDescription(0);
			const FStaticMeshRenderData* RenderData = LODMesh->GetRenderData();
			if (!MeshDescription || !RenderData || RenderData->LODResources.IsEmpty())
			{
				OutError = FText::Format(LOCTEXT("NoLODData", "{0} has no mesh data."), FText::FromString(LODMesh->GetName()));
				return nullptr;
			}

			// 2. Geometry (with the VAT UVs) and build settings
			Combined->CreateMeshDescription(LOD, *MeshDescription);
			FStaticMeshSourceModel& SourceModel = Combined->GetSourceModel(LOD);
			SourceModel.BuildSettings = LODMesh->GetSourceModel(0).BuildSettings;
			// No reduction: SetNumSourceModels gives new LODs auto-reduction (50%, 25%...), which would rebuild them
			// from LOD0's geometry (with LOD0's VAT UVs) and remap their sections onto LOD0's material slot
			SourceModel.ReductionSettings = FMeshReductionSettings();
			if (const FSkeletalMeshLODInfo* LODInfo = SkeletalMesh ? SkeletalMesh->GetLODInfo(LOD) : nullptr)
			{
				SourceModel.ScreenSize = LODInfo->ScreenSize;
			}
			Combined->CommitMeshDescription(LOD);

			// 3. A material slot per LOD and material, so each LOD can use its own VAT material instance
			const FStaticMeshSectionArray& Sections = RenderData->LODResources[0].Sections;
			for (int32 SectionIndex = 0; SectionIndex < Sections.Num(); ++SectionIndex)
			{
				const int32 SourceIndex = Sections[SectionIndex].MaterialIndex;
				const FStaticMaterial Source = LODMesh->GetStaticMaterials().IsValidIndex(SourceIndex) ? LODMesh->GetStaticMaterials()[SourceIndex] : FStaticMaterial();

				const FString SourceSlot = SourceSlotName(Source, SectionIndex);
				const FName Slot = CombinedSlotName(SourceSlot, LOD);
				int32 MaterialIndex = Materials.IndexOfByPredicate([Slot](const FStaticMaterial& M) { return M.MaterialSlotName == Slot; });
				if (MaterialIndex == INDEX_NONE)
				{
					UMaterialInterface* SlotMaterial = GetSlotMaterial(SourceSlot, LOD);
					MaterialIndex = Materials.Add(FStaticMaterial(SlotMaterial ? SlotMaterial : Source.MaterialInterface.Get(), Slot, Slot));
				}

				FMeshSectionInfo Info = LODMesh->GetSectionInfoMap().Get(0, SectionIndex);
				Info.MaterialIndex = MaterialIndex;
				Combined->GetSectionInfoMap().Set(LOD, SectionIndex, Info);
				Combined->GetOriginalSectionInfoMap().Set(LOD, SectionIndex, Info);
			}

			// The plugin extended each LOD mesh's bounds to cover its animation
			AnimatedBounds += LODMesh->GetBoundingBox();
		}

		Combined->SetStaticMaterials(Materials);
		Combined->SetLightMapCoordinateIndex(LODMeshes[0]->GetLightMapCoordinateIndex());

		// 4. Bounds: the combined mesh's own bounds come from LOD0, so extend them to cover every LOD's animation
		const FBox BaseBounds = GetBaseBounds(LODMeshes[0]);
		Combined->SetPositiveBoundsExtension((AnimatedBounds.Max - BaseBounds.Max).ComponentMax(FVector::ZeroVector));
		Combined->SetNegativeBoundsExtension((BaseBounds.Min - AnimatedBounds.Min).ComponentMax(FVector::ZeroVector));

		// 5. Build, and register a new mesh so the Content Browser shows it (unsaved)
		Combined->PostEditChange();
		Combined->MarkPackageDirty();
		if (bCreated)
		{
			FAssetRegistryModule::AssetCreated(Combined);
		}
		return Combined;
	}

	/**
	 * Gives every slot of each LOD mesh (SM_<Name>_LOD#) its VAT material instance, the same one the combined
	 * mesh uses for that slot and LOD, so the LOD meshes also show the animation on their own.
	 * Slots GetSlotMaterial has no instance for keep their material.
	 */
	void AssignLODMaterials(const TArray<UStaticMesh*>& LODMeshes, TFunctionRef<UMaterialInterface*(const FString& SourceSlot, int32 LOD)> GetSlotMaterial)
	{
		for (int32 LOD = 0; LOD < LODMeshes.Num(); ++LOD)
		{
			UStaticMesh* LODMesh = LODMeshes[LOD];
			TArray<FStaticMaterial> Materials = LODMesh->GetStaticMaterials();
			bool bChanged = false;
			for (int32 Index = 0; Index < Materials.Num(); ++Index)
			{
				UMaterialInterface* SlotMaterial = GetSlotMaterial(SourceSlotName(Materials[Index], Index), LOD);
				if (SlotMaterial && Materials[Index].MaterialInterface != SlotMaterial)
				{
					Materials[Index].MaterialInterface = SlotMaterial;
					bChanged = true;
				}
			}
			if (bChanged)
			{
				LODMesh->Modify();
				LODMesh->SetStaticMaterials(Materials);
				LODMesh->PostEditChange();
				LODMesh->MarkPackageDirty();
			}
		}
	}
}

namespace VatBaker
{
	using namespace VatBakerImpl;

	TArray<FAssetData> FindAnimations(const USkeletalMesh* Mesh)
	{
		TArray<FAssetData> Result;
		const USkeleton* Skeleton = Mesh ? Mesh->GetSkeleton() : nullptr;
		if (!Skeleton)
		{
			return Result;
		}

		// The Data Asset takes Anim Sequences only (no montages or composites)
		TArray<FAssetData> Sequences;
		IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
		AssetRegistry.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), Sequences, true);

		// Read the skeleton from the asset tags, so nothing has to load
		for (const FAssetData& Sequence : Sequences)
		{
			if (Skeleton->IsCompatibleForEditor(Sequence))
			{
				Result.Add(Sequence);
			}
		}

		Result.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		return Result;
	}

	FString SuggestName(const USkeletalMesh* Mesh)
	{
		return Mesh ? VrrCore::GetBaseAssetName(Mesh) + TEXT("_VAT") : FString();
	}

	int32 GetNumLODs(const USkeletalMesh* Mesh)
	{
		return Mesh ? Mesh->GetLODNum() : 0;
	}

	FVatResult CreateAssets(USkeletalMesh* Mesh, const TArray<UAnimSequence*>& Animations, const FVatBakeSettings& Settings, FVatAssetSet& OutSet)
	{
		FVatResult Result;

		// 1. Check the inputs
		if (!Mesh || !Mesh->GetSkeleton())
		{
			Result.Message = LOCTEXT("NoMesh", "Pick a skeletal mesh with a skeleton first.");
			return Result;
		}
		if (Animations.IsEmpty())
		{
			Result.Message = LOCTEXT("NoAnims", "Pick at least one animation.");
			return Result;
		}

		// 2. One free base name for the whole set
		const FString Folder = VrrCore::NormalizeContentFolder(Settings.OutputFolder);
		FSetNames Names;
		Names.NumLODs = GetNumLODs(Mesh);
		Names.Mode = Settings.Mode;
		if (!PickFreeBaseName(Folder, Settings.OutputName, Names))
		{
			Result.Message = FText::Format(LOCTEXT("BadPath", "Invalid output path: {0}"), FText::FromString(Folder / Settings.OutputName));
			return Result;
		}

		FScopedSlowTask Progress(Names.NumLODs + 1, LOCTEXT("Creating", "Creating VAT assets..."));
		Progress.MakeDialog();

		// 3. A static mesh copy of each skeletal LOD
		TArray<UStaticMesh*> LODMeshes;
		for (int32 LOD = 0; LOD < Names.NumLODs; ++LOD)
		{
			Progress.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("ConvertingLOD", "Converting LOD {0} to a static mesh..."), LOD));
			UStaticMesh* LODMesh = UAnimToTextureBPLibrary::ConvertSkeletalMeshToStaticMesh(Mesh, Folder / Names.LODMesh(LOD), LOD);
			if (!LODMesh)
			{
				Result.Message = FText::Format(LOCTEXT("ConvertFailed", "Could not convert LOD {0} to a static mesh (see the Output Log)."), LOD);
				return Result;
			}
			LODMeshes.Add(LODMesh);
			Result.Assets.Add(LODMesh);
		}

		// 4. The VAT UVs go in the first channel no LOD uses, the same on every LOD so one material setup fits all
		int32 VatUVChannel = 0;
		for (const UStaticMesh* LODMesh : LODMeshes)
		{
			VatUVChannel = FMath::Max(VatUVChannel, LODMesh->GetNumUVChannels(0));
		}
		if (VatUVChannel > MaxVatUVChannel)
		{
			Result.Message = FText::Format(LOCTEXT("TooManyUVs", "The mesh already uses {0} UV channels; the VAT UVs need a free channel between 0 and {1}."), VatUVChannel, MaxVatUVChannel);
			return Result;
		}
		for (UStaticMesh* LODMesh : LODMeshes)
		{
			PrepareLODMesh(LODMesh, VatUVChannel);
		}

		// 5. Textures and Data Assets
		Progress.EnterProgressFrame(1.0f, LOCTEXT("CreatingDataAssets", "Creating textures and Data Assets..."));

		// 8-bit weight textures can only index 256 bones
		const bool bHighPrecision = Settings.Mode == EAnimToTextureMode::Bone && Mesh->GetRefSkeleton().GetRawBoneNum() > MaxBonesEightBit
			&& Settings.Precision == EAnimToTexturePrecision::EightBits;

		UTexture2D* BonePosition = nullptr;
		UTexture2D* BoneRotation = nullptr;
		if (Settings.Mode == EAnimToTextureMode::Bone)
		{
			BonePosition = CreateVatTexture(Folder, Names.BonePosition());
			BoneRotation = CreateVatTexture(Folder, Names.BoneRotation());
			Result.Assets.Append({ BonePosition, BoneRotation });
		}

		OutSet = FVatAssetSet();
		OutSet.SkeletalMesh = Mesh;
		OutSet.Mode = Settings.Mode;
		OutSet.Folder = Folder;
		OutSet.BaseName = Names.BaseName;

		for (int32 LOD = 0; LOD < Names.NumLODs; ++LOD)
		{
			const FString Name = Names.DataAsset(LOD);
			UPackage* Package = CreatePackage(*(Folder / Name));
			UAnimToTextureDataAsset* DataAsset = NewObject<UAnimToTextureDataAsset>(Package, *Name, RF_Public | RF_Standalone | RF_Transactional);

			DataAsset->SkeletalMesh = Mesh;
			DataAsset->SkeletalLODIndex = LOD;
			DataAsset->StaticMesh = LODMeshes[LOD];
			DataAsset->StaticLODIndex = 0;
			DataAsset->UVChannel = VatUVChannel;
			DataAsset->Mode = Settings.Mode;
			DataAsset->Precision = bHighPrecision ? EAnimToTexturePrecision::SixteenBits : Settings.Precision;
			DataAsset->MaxWidth = Settings.MaxTextureSize;
			DataAsset->MaxHeight = Settings.MaxTextureSize;
			DataAsset->bEnforcePowerOfTwo = Settings.bEnforcePowerOfTwo;
			DataAsset->NumBoneInfluences = Settings.NumBoneInfluences;

			if (Settings.Mode == EAnimToTextureMode::Bone)
			{
				// Bone motion is the same for every LOD; only the skin weights differ
				DataAsset->BonePositionTexture = BonePosition;
				DataAsset->BoneRotationTexture = BoneRotation;
				UTexture2D* Weights = CreateVatTexture(Folder, Names.BoneWeights(LOD));
				DataAsset->BoneWeightTexture = Weights;
				Result.Assets.Add(Weights);
			}
			else
			{
				UTexture2D* Position = CreateVatTexture(Folder, Names.VertexPosition(LOD));
				UTexture2D* Normal = CreateVatTexture(Folder, Names.VertexNormal(LOD));
				DataAsset->VertexPositionTexture = Position;
				DataAsset->VertexNormalTexture = Normal;
				Result.Assets.Append({ Position, Normal });
			}

			for (UAnimSequence* Animation : Animations)
			{
				if (!Animation)
				{
					continue;
				}
				FAnimToTextureAnimSequenceInfo& Info = DataAsset->AnimSequences.AddDefaulted_GetRef();
				Info.AnimSequence = Animation;

				// The plugin bakes keys 0..N-1 by default; in a looping animation key N-1 repeats key 0, so drop it
				const int32 NumKeys = Animation->GetNumberOfSampledKeys();
				if (Settings.bSeamlessLoops && NumKeys > 2)
				{
					Info.bUseCustomRange = true;
					Info.StartFrame = 0;
					Info.EndFrame = NumKeys - 2;
				}
			}

			FAssetRegistryModule::AssetCreated(DataAsset);
			Package->MarkPackageDirty();
			OutSet.DataAssets.Add(DataAsset);
			Result.Assets.Add(DataAsset);
		}

		Result.bSuccess = true;
		Result.Message = FText::Format(LOCTEXT("Created", "Created {0} Data Asset(s) ({1}_LOD0-{2}) with {3} animation(s). VAT UVs go in UV{4}.{5}"),
			Names.NumLODs, FText::FromString(Names.DataAsset(0).LeftChop(5)), Names.NumLODs - 1, Animations.Num(), VatUVChannel,
			bHighPrecision ? LOCTEXT("HighPrecisionNote", " 16-bit textures (the skeleton has more than 256 bones).") : FText::GetEmpty());
		return Result;
	}

	TArray<FName> FindMissingParameters(const UMaterialInterface* Material, EAnimToTextureMode Mode)
	{
		const FMaterialParams Params(Material);
		TArray<FName> Missing = VatParams::Required(Mode);
		Missing.RemoveAll([&Params](FName Name) { return Params.Has(Name); });
		return Missing;
	}

	int32 GetNumAnimationSlots(const UMaterialInterface* Material)
	{
		return FMaterialParams(Material).NumAnimationSlots();
	}

	FVatResult Bake(FVatAssetSet& Set, const FVatBakeSettings& Settings)
	{
		FVatResult Result;

		// 1. Every Data Asset and its static mesh must still exist
		TArray<UAnimToTextureDataAsset*> DataAssets;
		TArray<UStaticMesh*> LODMeshes;
		for (int32 LOD = 0; LOD < Set.DataAssets.Num(); ++LOD)
		{
			UAnimToTextureDataAsset* DataAsset = Set.DataAssets[LOD].Get();
			UStaticMesh* LODMesh = DataAsset ? DataAsset->GetStaticMesh() : nullptr;
			if (!LODMesh)
			{
				Result.Message = FText::Format(LOCTEXT("MissingLOD", "The Data Asset or static mesh for LOD {0} is missing. Create the assets again."), LOD);
				return Result;
			}
			DataAssets.Add(DataAsset);
			LODMeshes.Add(LODMesh);
		}
		if (DataAssets.IsEmpty())
		{
			Result.Message = LOCTEXT("NothingToBake", "Create the Data Assets first.");
			return Result;
		}

		// The master material (empty skips the instances)
		const FSoftObjectPath& MasterPath = Settings.Material;
		UMaterialInterface* Master = MasterPath.IsNull() ? nullptr : Cast<UMaterialInterface>(MasterPath.TryLoad());
		if (!MasterPath.IsNull() && !Master)
		{
			Result.Message = FText::Format(LOCTEXT("NoMaster", "The master material {0} wasn't found."), FText::FromString(MasterPath.ToString()));
			return Result;
		}

		FScopedSlowTask Progress(DataAssets.Num() + 3, LOCTEXT("Baking", "Baking Vertex Animation Textures..."));
		Progress.MakeDialog();

		// 2. Bake each LOD (the plugin samples every animation, writes the textures and the VAT UVs)
		for (int32 LOD = 0; LOD < DataAssets.Num(); ++LOD)
		{
			Progress.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("BakingLOD", "Baking LOD {0} of {1}..."), LOD, DataAssets.Num() - 1));
			if (!UAnimToTextureBPLibrary::AnimationToTexture(DataAssets[LOD]))
			{
				Result.Message = FText::Format(LOCTEXT("BakeFailed", "Baking LOD {0} failed. LogAnimToTextureEditor in the Output Log says why."), LOD);
				return Result;
			}
			UseHDRCompression(DataAssets[LOD]);
			Result.Assets.Add(DataAssets[LOD]);
		}

		// 3. The base material instance with the baked settings
		Progress.EnterProgressFrame(1.0f, LOCTEXT("CreatingMaterial", "Creating the VAT material instance..."));
		FText Error, MaterialNote;
		UMaterialInstanceConstant* BaseMaterial = nullptr;
		const FMaterialParams MasterParams(Master);
		FText MaterialError;
		if (Master)
		{
			// A failure here still lets the mesh be built (its slots keep the LOD meshes' materials); it's reported at the end
			BaseMaterial = CreateBaseMaterial(Set, Master, DataAssets[0], MaterialError, MaterialNote);
			if (BaseMaterial)
			{
				Result.Assets.Add(BaseMaterial);
			}
		}

		// 4. One static mesh with every LOD; each slot gets a child instance with its LOD's textures
		Progress.EnterProgressFrame(1.0f, LOCTEXT("Combining", "Combining LODs into one static mesh..."));
		int32 NumSlotMaterials = 0;
		FText SlotError;
		// Each slot's instance is made once and shared by the combined mesh and the LOD mesh (key: "<slot>|<LOD>")
		TMap<FString, UMaterialInstanceConstant*> SlotMaterials;
		auto GetSlotMaterial = [&](const FString& SourceSlot, int32 LOD) -> UMaterialInterface*
		{
			const FString Key = FString::Printf(TEXT("%s|%d"), *SourceSlot, LOD);
			if (UMaterialInstanceConstant** Found = SlotMaterials.Find(Key))
			{
				return *Found;
			}
			if (!BaseMaterial || !SlotError.IsEmpty())
			{
				return nullptr;
			}
			UMaterialInstanceConstant* Instance = CreateSlotMaterial(Set, BaseMaterial, MasterParams, SourceSlot, LOD, DataAssets[LOD], SlotError);
			if (Instance)
			{
				SlotMaterials.Add(Key, Instance);
				Result.Assets.Add(Instance);
				++NumSlotMaterials;
			}
			return Instance;
		};

		UStaticMesh* Combined = CombineLODs(Set, LODMeshes, GetSlotMaterial, Error);
		if (!Combined)
		{
			Result.Message = Error;
			return Result;
		}
		Set.CombinedMesh = Combined;
		Result.Assets.Insert(Combined, 0);

		// The LOD meshes get the same instances on their own slots
		AssignLODMaterials(LODMeshes, GetSlotMaterial);

		if (!MaterialError.IsEmpty() || !SlotError.IsEmpty())
		{
			Result.Message = FText::Format(LOCTEXT("MaterialFailed", "Baked {0}, but the material instances failed: {1}"),
				FText::FromString(Combined->GetName()), MaterialError.IsEmpty() ? SlotError : MaterialError);
			return Result;
		}

		// 5. The preview actor in the level
		FText PreviewNote;
		if (Settings.bSpawnPreviewActor)
		{
			Progress.EnterProgressFrame(1.0f, LOCTEXT("PlacingPreview", "Placing the preview actor..."));
			PlacePreviewActor(Combined, MasterParams.Scalars.Contains(VatParams::AnimSelection), PreviewNote);
		}

		Result.bSuccess = true;
		const FText Baked = FText::Format(LOCTEXT("Baked", "Baked {0} LOD(s) into {1}."), DataAssets.Num(), FText::FromString(Combined->GetName()));
		const FText Materials = BaseMaterial
			? FText::Format(LOCTEXT("BakedMaterials", "{0} ({1}) has the baked settings; {2} slot instance(s) of it (_LOD#) add each LOD's textures."),
				FText::FromString(BaseMaterial->GetName()), FText::FromString(Master->GetName()), NumSlotMaterials)
			: LOCTEXT("BakedNoMaterial", "No master material is set, so no material instances were made.");
		const FText Preview = Settings.bSpawnPreviewActor && PreviewNote.IsEmpty()
			? LOCTEXT("BakedPreview", "The preview actor is selected at the level origin.") : PreviewNote;
		TArray<FText> Parts = { Baked, Materials, MaterialNote, Preview };
		Parts.RemoveAll([](const FText& Part) { return Part.IsEmpty(); });
		Result.Message = FText::Join(FText::FromString(TEXT(" ")), Parts);
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
