// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Consolidate Materials: combines the outputs of every material on a mesh into one texture set.
 *
 * Process (LOD 0)
 *  1. Find which polygon groups each material slot draws (the static mesh's section info, or slot names).
 *  2. For each output (Base Color, Normal...), and each slot: Unreal's MaterialBaking module renders the slot's
 *     material through the target UV channel, drawing only that slot's triangles. The material runs as it would
 *     on the mesh (its own UVs, vertex colors, parameters), so the result is its final output, not a texture copy.
 *     Texels no triangle covers keep the background color (magenta), which marks what each slot filled.
 *  3. Each slot's texels are copied into the output's canvas; texels two slots fill are counted as overlaps.
 *  4. Outputs that are one value everywhere (no metallic, no emissive...) can be skipped.
 *  5. The islands are padded outward, the rest filled with the average (a flat normal for normals),
 *     and each output is written as its own texture (<Base>_D, _N, _R...).
 */

#include "Core_ConsolidateMaterials.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "FileHelpers.h"
#include "IMaterialBakingModule.h"
#include "ImageCore.h"
#include "MaterialBakingStructures.h"
#include "Materials/Material.h"
#include "MeshDescription.h"
#include "MeshElementRemappings.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Misc/ScopedSlowTask.h"
#include "Modules/ModuleManager.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "UObject/Package.h"

#define LOCTEXT_NAMESPACE "MaterialConsolidator"

namespace ConsolidateImpl
{
	/** Texels no triangle covers keep this color, so a slot's coverage can be read from its bake. */
	const FColor Background = FColor::Magenta;

	EMaterialProperty GetProperty(EConsolidateMap Map)
	{
		switch (Map)
		{
		case EConsolidateMap::BaseColor:		return MP_BaseColor;
		case EConsolidateMap::Metallic:			return MP_Metallic;
		case EConsolidateMap::Specular:			return MP_Specular;
		case EConsolidateMap::Roughness:		return MP_Roughness;
		case EConsolidateMap::Normal:			return MP_Normal;
		case EConsolidateMap::Emissive:			return MP_EmissiveColor;
		case EConsolidateMap::Opacity:			return MP_Opacity;
		case EConsolidateMap::OpacityMask:		return MP_OpacityMask;
		case EConsolidateMap::AmbientOcclusion:	return MP_AmbientOcclusion;
		default:								return MP_MAX;
		}
	}

	/** One value per texel (Metallic, Roughness...): written as a single-channel texture. */
	bool IsScalar(EConsolidateMap Map)
	{
		return Map != EConsolidateMap::BaseColor && Map != EConsolidateMap::Normal && Map != EConsolidateMap::Emissive;
	}

	// Pixel helpers, so padding and flat checks work on 8-bit and half-float canvases alike (raw values, no gamma)
	FLinearColor ToRaw(const FColor& C) { return FLinearColor(C.R, C.G, C.B, C.A); }
	FLinearColor ToRaw(const FFloat16Color& C) { return C.GetFloats(); }
	void FromRaw(const FLinearColor& L, FColor& Out)
	{
		auto Byte = [](float V) { return static_cast<uint8>(FMath::Clamp(FMath::RoundToInt(V), 0, 255)); };
		Out = FColor(Byte(L.R), Byte(L.G), Byte(L.B), Byte(L.A));
	}
	void FromRaw(const FLinearColor& L, FFloat16Color& Out) { Out = FFloat16Color(L); }

	/** One output being assembled from every slot. 8-bit, except HDR emissive. */
	struct FMapCanvas
	{
		int32 Size = 0;
		bool bHDR = false;
		/** False when the baker wrote sRGB-encoded values (Base Color). */
		bool bLinear = true;
		TArray<FColor> Pixels;
		TArray<FFloat16Color> HDRPixels;
		TBitArray<> Covered;
		int32 NumCovered = 0;
	};

	/** True when every covered texel holds the same value (within Tolerance, in raw units). OutValue gets it. */
	template <typename PixelType>
	bool IsFlat(const TArray<PixelType>& Pixels, const TBitArray<>& Covered, float Tolerance, FLinearColor& OutValue)
	{
		bool bFirst = true;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			if (!Covered[Index])
			{
				continue;
			}
			const FLinearColor Value = ToRaw(Pixels[Index]);
			if (bFirst)
			{
				OutValue = Value;
				bFirst = false;
			}
			else if (FMath::Abs(Value.R - OutValue.R) > Tolerance || FMath::Abs(Value.G - OutValue.G) > Tolerance || FMath::Abs(Value.B - OutValue.B) > Tolerance)
			{
				return false;
			}
		}
		return !bFirst;
	}

	/**
	 * Grows the covered islands by Passes texels (each pass fills empty texels next to filled ones with their average),
	 * then fills the rest with Fill, or the average of the covered texels when Fill is unset.
	 */
	template <typename PixelType>
	void Pad(TArray<PixelType>& Pixels, TBitArray<> Filled, int32 Size, int32 Passes, const TOptional<PixelType>& Fill)
	{
		FLinearColor Mean = FLinearColor::Transparent;
		int32 NumFilled = 0;
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			if (Filled[Index])
			{
				Mean += ToRaw(Pixels[Index]);
				++NumFilled;
			}
		}
		PixelType Rest;
		if (Fill.IsSet())
		{
			Rest = Fill.GetValue();
		}
		else
		{
			FromRaw(NumFilled > 0 ? Mean / static_cast<float>(NumFilled) : FLinearColor::Transparent, Rest);
		}

		TArray<int32> NewlyFilled;
		for (int32 Pass = 0; Pass < Passes; ++Pass)
		{
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
					FLinearColor Total = FLinearColor::Transparent;
					int32 Count = 0;
					for (int32 DY = -1; DY <= 1; ++DY)
					{
						for (int32 DX = -1; DX <= 1; ++DX)
						{
							const int32 NX = X + DX;
							const int32 NY = Y + DY;
							if ((DX || DY) && NX >= 0 && NX < Size && NY >= 0 && NY < Size && Filled[NY * Size + NX])
							{
								Total += ToRaw(Pixels[NY * Size + NX]);
								++Count;
							}
						}
					}
					if (Count > 0)
					{
						FromRaw(Total / static_cast<float>(Count), Pixels[Index]);
						NewlyFilled.Add(Index);
					}
				}
			}
			if (NewlyFilled.IsEmpty())
			{
				break;
			}
			for (const int32 Index : NewlyFilled)
			{
				Filled[Index] = true;
			}
		}
		for (int32 Index = 0; Index < Pixels.Num(); ++Index)
		{
			if (!Filled[Index])
			{
				Pixels[Index] = Rest;
			}
		}
	}

	/** "0.5", "#B4A08C" or "(2.0, 1.0, 0.0)": a flat output's value for the result message. */
	FString DescribeFlatValue(EConsolidateMap Map, const FLinearColor& Raw, bool bLinear)
	{
		if (Map == EConsolidateMap::Emissive)
		{
			return FString::Printf(TEXT("(%.2f, %.2f, %.2f)"), Raw.R, Raw.G, Raw.B);
		}
		const FColor Bytes(static_cast<uint8>(Raw.R), static_cast<uint8>(Raw.G), static_cast<uint8>(Raw.B));
		if (IsScalar(Map))
		{
			const float Value = bLinear ? Raw.R / 255.0f : FLinearColor::FromSRGBColor(Bytes).R;
			return FString::Printf(TEXT("%.2f"), Value);
		}
		return FString::Printf(TEXT("#%02X%02X%02X"), Bytes.R, Bytes.G, Bytes.B);
	}

	/** The texture to write: with Overwrite on, the existing one of that name (materials keep their reference), otherwise a new one. */
	UTexture2D* FindOrCreateTexture(const FString& Folder, const FString& Name, bool bOverwrite, FText& OutError)
	{
		FString AssetName = Name;
		FString PackageName = Folder / AssetName;
		if (Name.IsEmpty() || !FPackageName::IsValidLongPackageName(PackageName))
		{
			OutError = FText::Format(LOCTEXT("BadPath", "Invalid output path: {0}"), FText::FromString(PackageName));
			return nullptr;
		}

		UTexture2D* Texture = nullptr;
		if (bOverwrite && (FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName)))
		{
			Texture = LoadObject<UTexture2D>(nullptr, *(PackageName + TEXT(".") + AssetName), nullptr, LOAD_NoWarn | LOAD_Quiet);
		}
		if (!Texture)
		{
			for (int32 Index = 1; FindPackage(nullptr, *PackageName) || FPackageName::DoesPackageExist(PackageName); ++Index)
			{
				AssetName = FString::Printf(TEXT("%s_%02d"), *Name, Index);
				PackageName = Folder / AssetName;
			}
			UPackage* Package = CreatePackage(*PackageName);
			Texture = NewObject<UTexture2D>(Package, *AssetName, RF_Public | RF_Standalone | RF_Transactional);
			FAssetRegistryModule::AssetCreated(Texture);
		}
		return Texture;
	}

	/** Writes a finished canvas into a texture with the settings its kind of data needs. */
	UTexture2D* WriteTexture(EConsolidateMap Map, const FMapCanvas& Canvas, const FString& Folder, const FString& Name, bool bOverwrite, FText& OutError)
	{
		UTexture2D* Texture = FindOrCreateTexture(Folder, Name, bOverwrite, OutError);
		if (!Texture)
		{
			return nullptr;
		}

		const int32 Size = Canvas.Size;
		const EGammaSpace Gamma = Canvas.bLinear ? EGammaSpace::Linear : EGammaSpace::sRGB;
		FImage Image;
		if (Canvas.bHDR)
		{
			Image.Init(Size, Size, ERawImageFormat::RGBA16F, EGammaSpace::Linear);
			TArrayView64<FFloat16Color> Out = Image.AsRGBA16F();
			for (int32 Index = 0; Index < Canvas.HDRPixels.Num(); ++Index)
			{
				Out[Index] = Canvas.HDRPixels[Index];
				Out[Index].A = FFloat16(1.0f);
			}
		}
		else if (IsScalar(Map))
		{
			Image.Init(Size, Size, ERawImageFormat::G8, Gamma);
			TArrayView64<uint8> Out = Image.AsG8();
			for (int32 Index = 0; Index < Canvas.Pixels.Num(); ++Index)
			{
				Out[Index] = Canvas.Pixels[Index].R;
			}
		}
		else
		{
			Image.Init(Size, Size, ERawImageFormat::BGRA8, Gamma);
			TArrayView64<FColor> Out = Image.AsBGRA8();
			for (int32 Index = 0; Index < Canvas.Pixels.Num(); ++Index)
			{
				Out[Index] = Canvas.Pixels[Index];
				Out[Index].A = 255;
			}
		}

		Texture->Modify();
		Texture->PreEditChange(nullptr);
		Texture->Source.Init(Image);
		Texture->SRGB = !Canvas.bHDR && !Canvas.bLinear;
		Texture->CompressionNoAlpha = true;
		Texture->LODGroup = TEXTUREGROUP_World;
		Texture->MipGenSettings = TMGS_FromTextureGroup;
		if (Canvas.bHDR)
		{
			Texture->CompressionSettings = TC_HDR;
		}
		else if (Map == EConsolidateMap::Normal)
		{
			Texture->CompressionSettings = TC_Normalmap;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
		}
		else if (IsScalar(Map))
		{
			Texture->CompressionSettings = TC_Grayscale;
		}
		else
		{
			Texture->CompressionSettings = TC_Default;
		}
		Texture->PostEditChange();
		Texture->MarkPackageDirty();
		return Texture;
	}

	/** Which slot a polygon group is drawn with: the static mesh's section info, else its slot name, else its order. */
	int32 GetGroupSlot(const UObject* Mesh, const TArray<FName>& ImportedNames, FName GroupSlotName, int32 GroupOrder)
	{
		if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			if (StaticMesh->GetSectionInfoMap().IsValidSection(0, GroupOrder))
			{
				return StaticMesh->GetSectionInfoMap().Get(0, GroupOrder).MaterialIndex;
			}
		}
		// Several slots can share a name (imported duplicates): prefer the one in the group's own position
		TArray<int32> Matches;
		for (int32 Slot = 0; Slot < ImportedNames.Num(); ++Slot)
		{
			if (ImportedNames[Slot] == GroupSlotName)
			{
				Matches.Add(Slot);
			}
		}
		if (Matches.Num() == 1)
		{
			return Matches[0];
		}
		if (Matches.Num() > 1)
		{
			return Matches.Contains(GroupOrder) ? GroupOrder : Matches[0];
		}
		return GroupOrder;
	}
}

namespace MaterialConsolidator
{
	using namespace ConsolidateImpl;

	FText GetMapLabel(EConsolidateMap Map)
	{
		switch (Map)
		{
		case EConsolidateMap::BaseColor:		return LOCTEXT("BaseColor", "Base Color");
		case EConsolidateMap::Metallic:			return LOCTEXT("Metallic", "Metallic");
		case EConsolidateMap::Specular:			return LOCTEXT("Specular", "Specular");
		case EConsolidateMap::Roughness:		return LOCTEXT("Roughness", "Roughness");
		case EConsolidateMap::Normal:			return LOCTEXT("Normal", "Normal");
		case EConsolidateMap::Emissive:			return LOCTEXT("Emissive", "Emissive");
		case EConsolidateMap::Opacity:			return LOCTEXT("Opacity", "Opacity");
		case EConsolidateMap::OpacityMask:		return LOCTEXT("OpacityMask", "Opacity Mask");
		case EConsolidateMap::AmbientOcclusion:	return LOCTEXT("AO", "Ambient Occlusion");
		default:								return FText::GetEmpty();
		}
	}

	const TCHAR* GetMapSuffix(EConsolidateMap Map)
	{
		switch (Map)
		{
		case EConsolidateMap::BaseColor:		return TEXT("D");
		case EConsolidateMap::Metallic:			return TEXT("M");
		case EConsolidateMap::Specular:			return TEXT("S");
		case EConsolidateMap::Roughness:		return TEXT("R");
		case EConsolidateMap::Normal:			return TEXT("N");
		case EConsolidateMap::Emissive:			return TEXT("E");
		case EConsolidateMap::Opacity:			return TEXT("O");
		case EConsolidateMap::OpacityMask:		return TEXT("OM");
		case EConsolidateMap::AmbientOcclusion:	return TEXT("AO");
		default:								return TEXT("");
		}
	}

	TArray<FConsolidateSlot> GetSlots(UObject* Mesh)
	{
		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			return {};
		}

		// The mesh's slots: display name, imported name (what polygon groups store) and material
		TArray<FConsolidateSlot> Slots;
		TArray<FName> ImportedNames;
		auto AddSlot = [&](FName SlotName, FName ImportedName, UMaterialInterface* Material)
		{
			FConsolidateSlot& Slot = Slots.AddDefaulted_GetRef();
			Slot.SlotIndex = Slots.Num() - 1;
			Slot.SlotName = SlotName.IsNone() ? ImportedName : SlotName;
			Slot.Material = Material;
			ImportedNames.Add(ImportedName);
		};
		if (const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh))
		{
			for (const FStaticMaterial& Material : StaticMesh->GetStaticMaterials())
			{
				AddSlot(Material.MaterialSlotName, Material.ImportedMaterialSlotName, Material.MaterialInterface);
			}
		}
		else if (const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh))
		{
			for (const FSkeletalMaterial& Material : SkeletalMesh->GetMaterials())
			{
				AddSlot(Material.MaterialSlotName, Material.ImportedMaterialSlotName, Material.MaterialInterface);
			}
		}

		// Each polygon group's triangles go to its slot
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TPolygonGroupAttributesConstRef<FName> GroupSlotNames = Attributes.GetPolygonGroupMaterialSlotNames();
		int32 GroupOrder = 0;
		for (const FPolygonGroupID Group : MeshDescription->PolygonGroups().GetElementIDs())
		{
			const int32 Slot = GetGroupSlot(Mesh, ImportedNames, GroupSlotNames.IsValid() ? GroupSlotNames[Group] : NAME_None, GroupOrder++);
			const int32 NumTriangles = MeshDescription->GetNumPolygonGroupTriangles(Group);
			if (NumTriangles > 0 && Slots.IsValidIndex(Slot))
			{
				Slots[Slot].PolygonGroups.Add(Group.GetValue());
				Slots[Slot].NumTriangles += NumTriangles;
			}
		}
		Slots.RemoveAll([](const FConsolidateSlot& Slot) { return Slot.NumTriangles == 0; });
		return Slots;
	}

	FString SuggestBaseName(const UObject* Mesh)
	{
		return Mesh ? TEXT("T_") + VrrCore::GetBaseAssetName(Mesh) : FString();
	}

	FConsolidateResult Consolidate(UObject* Mesh, const FConsolidateSettings& Settings)
	{
		FConsolidateResult Result;
		const FText CancelledMessage = LOCTEXT("Cancelled", "Consolidation cancelled. No texture was changed.");

		// 1. The slots, and the mesh data with normals and tangents (world-space normal outputs need them)
		TArray<FConsolidateSlot> Slots = GetSlots(Mesh);
		const FMeshDescription* SourceDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!SourceDescription)
		{
			Result.Message = LOCTEXT("NoMeshData", "The mesh has no LOD 0 source data.");
			return Result;
		}
		if (Slots.Num() < 2)
		{
			Result.Message = LOCTEXT("OneSlot", "The mesh draws with fewer than two material slots, so there is nothing to consolidate.");
			return Result;
		}
		const int32 NumChannels = VrrCore::GetNumUVChannels(Mesh);
		if (Settings.TargetChannel < 0 || Settings.TargetChannel >= NumChannels)
		{
			Result.Message = FText::Format(LOCTEXT("BadChannel", "The mesh has {0} UV channels; UV{1} isn't one of them."), NumChannels, Settings.TargetChannel);
			return Result;
		}
		if (!Settings.HasAnyMap())
		{
			Result.Message = LOCTEXT("NoMaps", "Tick at least one output.");
			return Result;
		}

		FMeshDescription RepairedDescription;
		const FMeshDescription* MeshDescription = SourceDescription;
		{
			bool bInvalidNormals = false, bInvalidTangents = false;
			FStaticMeshOperations::HasInvalidVertexInstanceNormalsOrTangents(*SourceDescription, bInvalidNormals, bInvalidTangents);
			if (bInvalidNormals || bInvalidTangents)
			{
				RepairedDescription = *SourceDescription;
				// The triangle normals/tangents must exist first; computing them compacts the mesh data, so do that
				// here and renumber each slot's polygon groups to match
				if (RepairedDescription.NeedsCompact())
				{
					FElementIDRemappings Remappings;
					RepairedDescription.Compact(Remappings);
					for (FConsolidateSlot& Slot : Slots)
					{
						for (int32& Group : Slot.PolygonGroups)
						{
							Group = Remappings.GetRemappedPolygonGroupID(FPolygonGroupID(Group)).GetValue();
						}
					}
				}
				FStaticMeshOperations::ComputeTriangleTangentsAndNormals(RepairedDescription);
				FStaticMeshOperations::RecomputeNormalsAndTangentsIfNeeded(RepairedDescription, EComputeNTBsFlags::UseMikkTSpace | EComputeNTBsFlags::WeightedNTBs);
				MeshDescription = &RepairedDescription;
			}
		}

		TArray<EConsolidateMap> Maps;
		for (int32 MapIndex = 0; MapIndex < static_cast<int32>(EConsolidateMap::Count); ++MapIndex)
		{
			if (Settings.bMaps[MapIndex])
			{
				Maps.Add(static_cast<EConsolidateMap>(MapIndex));
			}
		}

		const int32 Size = FMath::Clamp(Settings.Resolution, 16, 8192);
		const int32 NumTexels = Size * Size;
		const FString Folder = VrrCore::NormalizeContentFolder(Settings.OutputFolder);
		const UStaticMesh* StaticMesh = Cast<UStaticMesh>(Mesh);
		const USkeletalMesh* SkeletalMesh = Cast<USkeletalMesh>(Mesh);

		// The baker's color spaces: Base Color in sRGB, everything else linear; emissive kept in HDR
		IMaterialBakingModule& Baker = FModuleManager::Get().LoadModuleChecked<IMaterialBakingModule>("MaterialBaking");
		Baker.SetLinearBake(true);
		Baker.SetEmissiveHDR(true);
		ON_SCOPE_EXIT
		{
			Baker.SetLinearBake(false);
			Baker.SetEmissiveHDR(false);
		};

		FScopedSlowTask SlowTask(static_cast<float>(Maps.Num() * (Slots.Num() + 1)),
			FText::Format(LOCTEXT("Consolidating", "Consolidating {0} materials..."), Slots.Num()));
		SlowTask.MakeDialog(true);

		// Who filled each texel (from the first output), to find slots overlapping in the target UVs
		TArray<int16> Owner;
		Owner.Init(INDEX_NONE, NumTexels);
		int32 NumOverlaps = 0;
		int32 NumCovered = 0;
		TArray<FString> FlatMaps;
		TArray<FString> Written;

		// 2. One output at a time (only one canvas in memory), every slot into it
		for (int32 MapOrder = 0; MapOrder < Maps.Num(); ++MapOrder)
		{
			const EConsolidateMap Map = Maps[MapOrder];
			const FMaterialPropertyEx Property(GetProperty(Map));
			const bool bTrackOwners = MapOrder == 0;

			FMapCanvas Canvas;
			Canvas.Size = Size;
			Canvas.bHDR = Map == EConsolidateMap::Emissive;
			Canvas.Covered.Init(false, NumTexels);
			if (Canvas.bHDR)
			{
				Canvas.HDRPixels.SetNumZeroed(NumTexels);
			}
			else
			{
				Canvas.Pixels.SetNumZeroed(NumTexels);
			}

			for (const FConsolidateSlot& Slot : Slots)
			{
				if (SlowTask.ShouldCancel())
				{
					Result.Message = Written.IsEmpty() ? CancelledMessage
						: FText::Format(LOCTEXT("CancelledPartly", "Consolidation cancelled after writing {0}."), FText::FromString(FString::Join(Written, TEXT(", "))));
					return Result;
				}
				SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("BakingSlot", "{0}: baking {1}..."), GetMapLabel(Map), FText::FromName(Slot.SlotName)));

				FMaterialDataEx MaterialData;
				MaterialData.Material = Slot.Material ? Slot.Material : UMaterial::GetDefaultMaterial(MD_Surface);
				MaterialData.PropertySizes.Add(Property, FIntPoint(Size, Size));
				MaterialData.bPerformBorderSmear = false;
				MaterialData.bPerformShrinking = false;
				MaterialData.bTangentSpaceNormal = true;
				MaterialData.BackgroundColor = Background;

				FMeshData MeshData;
				MeshData.MeshDescription = MeshDescription;
				MeshData.Mesh = StaticMesh;
				MeshData.MaterialIndices = Slot.PolygonGroups;
				MeshData.TextureCoordinateIndex = Settings.TargetChannel;
				MeshData.PrimitiveData = StaticMesh ? FPrimitiveData(StaticMesh) : FPrimitiveData(SkeletalMesh);

				TArray<FMaterialDataEx*> MaterialSettings = { &MaterialData };
				TArray<FMeshData*> MeshSettings = { &MeshData };
				TArray<FBakeOutputEx> Outputs;
				Baker.BakeMaterials(MaterialSettings, MeshSettings, Outputs);

				const FBakeOutputEx* Output = Outputs.IsEmpty() ? nullptr : &Outputs[0];
				const FIntPoint* OutputSize = Output ? Output->PropertySizes.Find(Property) : nullptr;
				const TArray<FColor>* LDR = Output ? Output->PropertyData.Find(Property) : nullptr;
				const TArray<FFloat16Color>* HDR = Output ? Output->HDRPropertyData.Find(Property) : nullptr;
				if (!OutputSize || *OutputSize != FIntPoint(Size, Size) || (Canvas.bHDR ? !HDR || HDR->Num() != NumTexels : !LDR || LDR->Num() != NumTexels))
				{
					Result.Message = FText::Format(LOCTEXT("BakeFailed", "Baking {0} of {1} failed. The Output Log may say why."), GetMapLabel(Map), FText::FromName(Slot.SlotName));
					return Result;
				}
				Canvas.bLinear = Output->PropertyIsLinearColor.FindRef(Property);

				// 3. Copy the texels this slot covered (the rest kept the background)
				const FFloat16Color Background16 = FFloat16Color(FLinearColor(Background));
				for (int32 Index = 0; Index < NumTexels; ++Index)
				{
					if (Canvas.bHDR)
					{
						const FFloat16Color& Pixel = (*HDR)[Index];
						if (Pixel == Background16)
						{
							continue;
						}
						Canvas.HDRPixels[Index] = Pixel;
					}
					else
					{
						const FColor& Pixel = (*LDR)[Index];
						if (Pixel.R == Background.R && Pixel.G == Background.G && Pixel.B == Background.B)
						{
							continue;
						}
						Canvas.Pixels[Index] = Pixel;
					}
					if (!Canvas.Covered[Index])
					{
						Canvas.Covered[Index] = true;
						++Canvas.NumCovered;
					}
					if (bTrackOwners)
					{
						if (Owner[Index] != INDEX_NONE && Owner[Index] != Slot.SlotIndex)
						{
							++NumOverlaps;
						}
						Owner[Index] = static_cast<int16>(Slot.SlotIndex);
					}
				}
			}

			SlowTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("Writing", "{0}: padding and writing..."), GetMapLabel(Map)));
			NumCovered = FMath::Max(NumCovered, Canvas.NumCovered);
			if (Canvas.NumCovered == 0)
			{
				Result.Message = FText::Format(LOCTEXT("NothingCovered", "No triangle covers the 0-1 square in UV{0}. Check the target channel."), Settings.TargetChannel);
				return Result;
			}

			// 4. Skip an output that is one value everywhere
			FLinearColor FlatValue;
			const bool bFlat = Canvas.bHDR ? IsFlat(Canvas.HDRPixels, Canvas.Covered, 0.001f, FlatValue) : IsFlat(Canvas.Pixels, Canvas.Covered, 1.0f, FlatValue);
			if (Settings.bSkipFlatMaps && bFlat)
			{
				FlatMaps.Add(FString::Printf(TEXT("%s %s"), *GetMapLabel(Map).ToString(), *DescribeFlatValue(Map, FlatValue, Canvas.bLinear)));
				continue;
			}

			// 5. Pad, fill the rest (flat normal for normals) and write
			if (Canvas.bHDR)
			{
				Pad(Canvas.HDRPixels, Canvas.Covered, Size, Settings.Padding, TOptional<FFloat16Color>());
			}
			else
			{
				const TOptional<FColor> Fill = Map == EConsolidateMap::Normal ? TOptional<FColor>(FColor(128, 128, 255, 255)) : TOptional<FColor>();
				Pad(Canvas.Pixels, Canvas.Covered, Size, Settings.Padding, Fill);
			}

			FText Error;
			UTexture2D* Texture = WriteTexture(Map, Canvas, Folder, Settings.OutputBaseName + TEXT("_") + GetMapSuffix(Map), Settings.bOverwrite, Error);
			if (!Texture)
			{
				Result.Message = Error;
				return Result;
			}
			Result.Textures.Add(Texture);
			Written.Add(Texture->GetName());
		}

		// Save
		bool bSaved = false;
		if (Settings.bSaveToDisk && !Result.Textures.IsEmpty())
		{
			TArray<UPackage*> Packages;
			for (UTexture2D* Texture : Result.Textures)
			{
				Packages.Add(Texture->GetOutermost());
			}
			bSaved = UEditorLoadingAndSavingUtils::SavePackages(Packages, false);
		}

		// Result: what was written, coverage, flat outputs, then warnings
		FText Message = Written.IsEmpty()
			? FText::Format(LOCTEXT("NoneWritten", "Consolidated {0} materials into UV{1}, but every output was flat, so no texture was written."), Slots.Num(), Settings.TargetChannel)
			: FText::Format(LOCTEXT("Done", "Consolidated {0} materials into UV{1} ({2} x {2}): {3}. {4} of the texture covered."),
				Slots.Num(), Settings.TargetChannel, Size, FText::FromString(FString::Join(Written, TEXT(", "))),
				FText::AsPercent(static_cast<float>(NumCovered) / NumTexels));
		if (!FlatMaps.IsEmpty())
		{
			Message = FText::Format(LOCTEXT("Flat", "{0} Flat, not written (use a constant in the material): {1}."), Message, FText::FromString(FString::Join(FlatMaps, TEXT(", "))));
		}
		if (!Written.IsEmpty())
		{
			if (Settings.bSaveToDisk)
			{
				Message = FText::Format(bSaved ? LOCTEXT("Saved", "{0} Saved.") : LOCTEXT("SaveFailed", "{0} Saving failed: save them from the Content Browser."), Message);
			}
			else
			{
				Message = FText::Format(LOCTEXT("Unsaved", "{0} Not saved yet."), Message);
			}
		}
		if (NumOverlaps > 0)
		{
			Message = FText::Format(LOCTEXT("Overlaps", "{0} {1} texels are covered by more than one material in UV{2}, so the last slot wins there. Use a unique channel, such as one from UV Layouts."),
				Message, NumOverlaps, Settings.TargetChannel);
		}
		Result.bSuccess = true;
		Result.Message = Message;
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
