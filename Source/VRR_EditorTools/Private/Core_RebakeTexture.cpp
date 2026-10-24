// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Rebake Texture: moves a texture from one UV channel of a mesh to another.
 *
 * Process (LOD 0)
 *  1. Read the source texture's mip 0 (its editor source data) as linear floats.
 *  2. For every triangle, rasterize its target-channel UVs: each texel is split into Supersampling x Supersampling
 *     samples, and each sample inside the triangle gets the same surface point's source-channel UV
 *     (same barycentric weights), where the source texture is sampled (bilinear or nearest, wrapping like the texture).
 *     Normal maps: the sampled normal is turned from the source UVs' tangent frame into the target UVs' frame,
 *     so islands that are rotated or mirrored in the new layout still shade the same.
 *  3. Average the samples per texel, so island edges are anti-aliased and never mix in empty space.
 *  4. Pad the islands outward (each pass fills empty texels next to filled ones with their average),
 *     then fill what is left with the average color (a flat normal for normal maps), so mips stay clean.
 *  5. Write a new texture in the source's pixel format with the chosen compression settings; optionally save it.
 */

#include "Core_RebakeTexture.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"
#include "ImageCore.h"
#include "ImageCoreUtils.h"
#include "MeshDescription.h"
#include "Misc/ScopedSlowTask.h"
#include "StaticMeshAttributes.h"

#define LOCTEXT_NAMESPACE "TextureRebaker"

namespace RebakeImpl
{
	/** A texel index along one axis, wrapped, clamped or mirrored like the texture's address mode. */
	int32 Address(int32 Index, int32 Size, TextureAddress Mode)
	{
		switch (Mode)
		{
		case TA_Clamp:
			return FMath::Clamp(Index, 0, Size - 1);
		case TA_Mirror:
		{
			const int32 Period = Size * 2;
			const int32 M = ((Index % Period) + Period) % Period;
			return M < Size ? M : Period - 1 - M;
		}
		default:
			return ((Index % Size) + Size) % Size;
		}
	}

	/** The source texture's mip 0 as linear RGBA floats. */
	struct FSourceImage
	{
		FImage Image;
		int32 Width = 0;
		int32 Height = 0;
		TextureAddress AddressX = TA_Wrap;
		TextureAddress AddressY = TA_Wrap;

		const FLinearColor& Texel(int32 X, int32 Y) const
		{
			return Image.AsRGBA32F()[static_cast<int64>(Address(Y, Height, AddressY)) * Width + Address(X, Width, AddressX)];
		}

		FLinearColor Sample(const FVector2f& UV, ERebakeFilter Filter) const
		{
			if (Filter == ERebakeFilter::Nearest)
			{
				return Texel(FMath::FloorToInt32(UV.X * Width), FMath::FloorToInt32(UV.Y * Height));
			}
			// Bilinear between the four nearest texel centers
			const float PX = UV.X * Width - 0.5f;
			const float PY = UV.Y * Height - 0.5f;
			const int32 X0 = FMath::FloorToInt32(PX);
			const int32 Y0 = FMath::FloorToInt32(PY);
			const float FX = PX - X0;
			const float FY = PY - Y0;
			const FLinearColor Top = FMath::Lerp(Texel(X0, Y0), Texel(X0 + 1, Y0), FX);
			const FLinearColor Bottom = FMath::Lerp(Texel(X0, Y0 + 1), Texel(X0 + 1, Y0 + 1), FX);
			return FMath::Lerp(Top, Bottom, FY);
		}
	};

	/** dP/dU and dP/dV of a triangle for the given UVs (zero when its UVs have no area). */
	void TriangleTangents(const FVector3f P[3], const FVector2f UV[3], FVector3f& OutT, FVector3f& OutB)
	{
		const FVector3f E1 = P[1] - P[0];
		const FVector3f E2 = P[2] - P[0];
		const FVector2f D1 = UV[1] - UV[0];
		const FVector2f D2 = UV[2] - UV[0];
		const float R = D1.X * D2.Y - D2.X * D1.Y;
		if (FMath::Abs(R) < 1e-12f)
		{
			OutT = OutB = FVector3f::ZeroVector;
			return;
		}
		OutT = (E1 * D2.Y - E2 * D1.Y) / R;
		OutB = (E2 * D1.X - E1 * D2.X) / R;
	}

	/** V made perpendicular to N and unit length (Fallback when it is parallel or zero). */
	FVector3f Orthonormal(const FVector3f& V, const FVector3f& N, const FVector3f& Fallback)
	{
		const FVector3f Result = (V - N * FVector3f::DotProduct(N, V)).GetSafeNormal();
		return Result.IsNearlyZero() ? Fallback : Result;
	}

	/**
	 * A tangent-space normal (encoded 0-1) turned from one tangent frame to another around the surface normal N.
	 * Green follows +V (dP/dV) in Unreal's UV space, as in Unreal's tangent basis; both frames use it, so only
	 * the rotation and mirroring between the two UV layouts change the result.
	 */
	FLinearColor TransferNormal(const FLinearColor& Encoded, const FVector3f& N, const FVector3f& SourceT, const FVector3f& SourceB, const FVector3f& TargetT, const FVector3f& TargetB)
	{
		const float X = Encoded.R * 2.0f - 1.0f;
		const float Y = Encoded.G * 2.0f - 1.0f;
		// Two-channel sources (no blue) get Z rebuilt
		const float Z = Encoded.B > 0.004f ? Encoded.B * 2.0f - 1.0f : FMath::Sqrt(FMath::Max(0.0f, 1.0f - X * X - Y * Y));

		const FVector3f World = SourceT * X + SourceB * Y + N * Z;
		const FVector3f Local = FVector3f(FVector3f::DotProduct(World, TargetT), FVector3f::DotProduct(World, TargetB), FVector3f::DotProduct(World, N)).GetSafeNormal();
		return FLinearColor(Local.X * 0.5f + 0.5f, Local.Y * 0.5f + 0.5f, Local.Z * 0.5f + 0.5f, Encoded.A);
	}

	/**
	 * The texture asset to write: with Overwrite on, the existing one of that name (materials keep their reference),
	 * otherwise a new one, skipping names that are taken.
	 */
	UTexture2D* FindOrCreateTexture(const FString& Folder, const FString& Name, bool bOverwrite, const UTexture2D* Source, FText& OutError)
	{
		// The source is read while the output is written, so they must not be the same asset
		if ((Folder / Name) == Source->GetOutermost()->GetName())
		{
			OutError = LOCTEXT("SameAsSource", "The output name is the source texture. Pick another name, so the source isn't overwritten while it is read.");
			return nullptr;
		}
		return VrrCore::FindOrCreateTexture(Folder, Name, bOverwrite, OutError);
	}
}

namespace TextureRebaker
{
	using namespace RebakeImpl;

	void CopyTextureSettings(const UTexture2D* Texture, FRebakeTextureSettings& InOutSettings)
	{
		if (!Texture)
		{
			return;
		}
		InOutSettings.CompressionSettings = Texture->CompressionSettings;
		InOutSettings.bSRGB = Texture->SRGB;
		InOutSettings.bCompressionNoAlpha = Texture->CompressionNoAlpha;
		InOutSettings.LODGroup = Texture->LODGroup;
		InOutSettings.MipGenSettings = Texture->MipGenSettings;
		InOutSettings.bNormalMap = Texture->CompressionSettings == TC_Normalmap;
		const int64 Largest = FMath::Max(Texture->Source.GetSizeX(), Texture->Source.GetSizeY());
		InOutSettings.Resolution = Largest > 0
			? FMath::Clamp(static_cast<int32>(FMath::RoundUpToPowerOfTwo64(static_cast<uint64>(Largest))), 256, 8192)
			: 2048;
	}

	FString SuggestName(const UTexture2D* Texture, int32 TargetChannel)
	{
		return Texture ? FString::Printf(TEXT("%s_UV%d"), *Texture->GetName(), TargetChannel) : FString();
	}

	FText GetTextureBlocker(const UTexture2D* Texture)
	{
		if (!Texture)
		{
			return LOCTEXT("NeedTexture", "Pick a texture.");
		}
		if (!Texture->Source.IsValid())
		{
			return LOCTEXT("NoSource", "This texture has no source data to read (it may be cooked or generated).");
		}
		if (Texture->Source.GetNumBlocks() > 1)
		{
			return LOCTEXT("UDIMSource", "UDIM textures can't be used as the source yet.");
		}
		return FText::GetEmpty();
	}

	FRebakeTextureResult Rebake(UObject* Mesh, UTexture2D* Texture, const FRebakeTextureSettings& Settings)
	{
		FRebakeTextureResult Result;
		const FText CancelledMessage = LOCTEXT("Cancelled", "Rebake cancelled. No texture was changed.");

		const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh, 0);
		if (!MeshDescription)
		{
			Result.Message = LOCTEXT("NoMeshData", "The mesh has no LOD 0 source data.");
			return Result;
		}
		const FText TextureBlocker = GetTextureBlocker(Texture);
		if (!TextureBlocker.IsEmpty())
		{
			Result.Message = TextureBlocker;
			return Result;
		}
		FStaticMeshConstAttributes Attributes(*MeshDescription);
		TVertexInstanceAttributesConstRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
		TVertexInstanceAttributesConstRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
		TVertexAttributesConstRef<FVector3f> Positions = Attributes.GetVertexPositions();
		const int32 NumChannels = UVs.GetNumChannels();
		if (Settings.SourceChannel < 0 || Settings.SourceChannel >= NumChannels || Settings.TargetChannel < 0 || Settings.TargetChannel >= NumChannels)
		{
			Result.Message = FText::Format(LOCTEXT("BadChannels", "The mesh has {0} UV channels; UV{1} or UV{2} isn't one of them."), NumChannels, Settings.SourceChannel, Settings.TargetChannel);
			return Result;
		}

		FScopedSlowTask SlowTask(4.0f, FText::Format(LOCTEXT("Rebaking", "Rebaking {0}..."), FText::FromString(Texture->GetName())));
		SlowTask.MakeDialog(true);

		// 1. Source pixels, linear
		SlowTask.EnterProgressFrame(0.5f, LOCTEXT("Reading", "Reading the source texture..."));
		FSourceImage Source;
		{
			FImage Mip;
			if (!Texture->Source.GetMipImage(Mip, 0))
			{
				Result.Message = LOCTEXT("ReadFailed", "Couldn't read the source texture's pixels.");
				return Result;
			}
			Mip.CopyTo(Source.Image, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		}
		Source.Width = Source.Image.SizeX;
		Source.Height = Source.Image.SizeY;
		Source.AddressX = Texture->AddressX;
		Source.AddressY = Texture->AddressY;

		// 2. Rasterize the target UVs, sampling the source through the source UVs
		const int32 Size = FMath::Clamp(Settings.Resolution, 16, 8192);
		const int32 NumTexels = Size * Size;
		const int32 Samples = FMath::Clamp(Settings.Supersampling, 1, 8);
		const float SampleWeight = 1.0f / (Samples * Samples);
		TArray<FLinearColor> Sum;
		Sum.Init(FLinearColor::Transparent, NumTexels);
		TArray<float> Weight;
		Weight.Init(0.0f, NumTexels);
		TArray<int32> Owner;
		Owner.Init(INDEX_NONE, NumTexels);
		TBitArray<> Conflicted(false, NumTexels);

		TArray<FTriangleID> Triangles;
		for (const FTriangleID TriangleID : MeshDescription->Triangles().GetElementIDs())
		{
			Triangles.Add(TriangleID);
		}
		const int32 NumTriangles = Triangles.Num();
		TArray<FIntVector> TriangleVertices;
		TriangleVertices.SetNumUninitialized(NumTriangles);
		int32 NumOutOfRange = 0;
		int32 NumConflicts = 0;

		SlowTask.EnterProgressFrame(2.5f, LOCTEXT("Sampling", "Sampling..."));
		constexpr int32 Steps = 100;
		const int32 TrianglesPerStep = FMath::Max(FMath::DivideAndRoundUp(NumTriangles, Steps), 1);
		FScopedSlowTask RasterTask(static_cast<float>(FMath::DivideAndRoundUp(NumTriangles, TrianglesPerStep)), LOCTEXT("Sampling", "Sampling..."));

		for (int32 Triangle = 0; Triangle < NumTriangles; ++Triangle)
		{
			if (Triangle % TrianglesPerStep == 0)
			{
				if (RasterTask.ShouldCancel())
				{
					Result.Message = CancelledMessage;
					return Result;
				}
				RasterTask.EnterProgressFrame(1.0f, FText::Format(LOCTEXT("SamplingStep", "Sampling: {0}"), FText::AsPercent(static_cast<float>(Triangle) / FMath::Max(NumTriangles, 1))));
			}

			const TArrayView<const FVertexInstanceID> Corners = MeshDescription->GetTriangleVertexInstances(Triangles[Triangle]);
			FVector2f Target[3], SourceUV[3], Pixel[3];
			FVector3f P[3], N[3];
			bool bOutOfRange = false;
			for (int32 i = 0; i < 3; ++i)
			{
				Target[i] = UVs.Get(Corners[i], Settings.TargetChannel);
				SourceUV[i] = UVs.Get(Corners[i], Settings.SourceChannel);
				P[i] = Positions[MeshDescription->GetVertexInstanceVertex(Corners[i])];
				N[i] = Normals[Corners[i]];
				Pixel[i] = Target[i] * static_cast<float>(Size);
				bOutOfRange |= Target[i].X < -0.001f || Target[i].X > 1.001f || Target[i].Y < -0.001f || Target[i].Y > 1.001f;
			}
			TriangleVertices[Triangle] = FIntVector(
				MeshDescription->GetVertexInstanceVertex(Corners[0]).GetValue(),
				MeshDescription->GetVertexInstanceVertex(Corners[1]).GetValue(),
				MeshDescription->GetVertexInstanceVertex(Corners[2]).GetValue());
			NumOutOfRange += bOutOfRange ? 1 : 0;

			const float Area = VrrCore::EdgeFunction(Pixel[0], Pixel[1], Pixel[2]);
			if (FMath::Abs(Area) < UE_SMALL_NUMBER)
			{
				continue;
			}

			// Normal maps: each layout's tangent frame on this triangle
			FVector3f SourceT = FVector3f::ZeroVector, SourceB = FVector3f::ZeroVector, TargetT = FVector3f::ZeroVector, TargetB = FVector3f::ZeroVector;
			const FVector3f FaceNormal = FVector3f::CrossProduct(P[2] - P[0], P[1] - P[0]).GetSafeNormal();
			if (Settings.bNormalMap)
			{
				TriangleTangents(P, SourceUV, SourceT, SourceB);
				TriangleTangents(P, Target, TargetT, TargetB);
			}

			const int32 MinX = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
			const int32 MaxX = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].X, Pixel[1].X, Pixel[2].X)), 0, Size - 1);
			const int32 MinY = FMath::Clamp(FMath::FloorToInt32(FMath::Min3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);
			const int32 MaxY = FMath::Clamp(FMath::CeilToInt32(FMath::Max3(Pixel[0].Y, Pixel[1].Y, Pixel[2].Y)), 0, Size - 1);
			const FIntVector& Mine = TriangleVertices[Triangle];

			for (int32 Y = MinY; Y <= MaxY; ++Y)
			{
				for (int32 X = MinX; X <= MaxX; ++X)
				{
					const int32 Index = Y * Size + X;
					bool bTouched = false;
					for (int32 SY = 0; SY < Samples; ++SY)
					{
						for (int32 SX = 0; SX < Samples; ++SX)
						{
							const FVector2f Point(X + (SX + 0.5f) / Samples, Y + (SY + 0.5f) / Samples);
							const float W0 = VrrCore::EdgeFunction(Pixel[1], Pixel[2], Point) / Area;
							const float W1 = VrrCore::EdgeFunction(Pixel[2], Pixel[0], Point) / Area;
							const float W2 = 1.0f - W0 - W1;
							constexpr float Epsilon = -1e-4f;
							if (W0 < Epsilon || W1 < Epsilon || W2 < Epsilon)
							{
								continue;
							}

							const FVector2f UV = SourceUV[0] * W0 + SourceUV[1] * W1 + SourceUV[2] * W2;
							FLinearColor Color = Source.Sample(UV, Settings.Filter);
							if (Settings.bNormalMap)
							{
								FVector3f Normal = (N[0] * W0 + N[1] * W1 + N[2] * W2).GetSafeNormal();
								if (Normal.IsNearlyZero())
								{
									Normal = FaceNormal;
								}
								// Fallback axes for triangles whose UVs have no area
								FVector3f AxisA, AxisB;
								Normal.FindBestAxisVectors(AxisA, AxisB);
								Color = TransferNormal(Color, Normal,
									Orthonormal(SourceT, Normal, AxisA), Orthonormal(SourceB, Normal, AxisB),
									Orthonormal(TargetT, Normal, AxisA), Orthonormal(TargetB, Normal, AxisB));
							}
							Sum[Index] += Color;
							Weight[Index] += SampleWeight;
							bTouched = true;
						}
					}
					if (!bTouched)
					{
						continue;
					}

					// Overlapping target UVs: a part of the mesh not touching this triangle already wrote here
					const int32 Previous = Owner[Index];
					if (Previous != INDEX_NONE && Previous != Triangle && !Conflicted[Index])
					{
						const FIntVector& Other = TriangleVertices[Previous];
						bool bConnected = false;
						for (const int32 Vertex : { Mine.X, Mine.Y, Mine.Z })
						{
							bConnected |= Vertex == Other.X || Vertex == Other.Y || Vertex == Other.Z;
						}
						if (!bConnected)
						{
							Conflicted[Index] = true;
							++NumConflicts;
						}
					}
					Owner[Index] = Triangle;
				}
			}
		}

		// 3. Average the samples of each texel
		SlowTask.EnterProgressFrame(0.5f, LOCTEXT("Padding", "Padding islands..."));
		FImage Baked(Size, Size, ERawImageFormat::RGBA32F, EGammaSpace::Linear);
		TArrayView64<FLinearColor> Pixels = Baked.AsRGBA32F();
		TBitArray<> Filled(false, NumTexels);
		int32 NumCovered = 0;
		FLinearColor Mean = FLinearColor::Transparent;
		for (int32 Index = 0; Index < NumTexels; ++Index)
		{
			if (Weight[Index] > 0.0f)
			{
				FLinearColor Color = Sum[Index] / (Weight[Index] / SampleWeight);
				if (Settings.bNormalMap)
				{
					// Re-normalize the averaged normal
					const FVector3f Normal = FVector3f(Color.R * 2.0f - 1.0f, Color.G * 2.0f - 1.0f, Color.B * 2.0f - 1.0f).GetSafeNormal();
					Color = FLinearColor(Normal.X * 0.5f + 0.5f, Normal.Y * 0.5f + 0.5f, Normal.Z * 0.5f + 0.5f, Color.A);
				}
				Pixels[Index] = Color;
				Filled[Index] = true;
				Mean += Color;
				++NumCovered;
			}
		}
		if (NumCovered == 0)
		{
			Result.Message = FText::Format(LOCTEXT("NothingCovered", "No triangle covers the 0-1 square in UV{0}. Check the target channel."), Settings.TargetChannel);
			return Result;
		}
		Mean /= static_cast<float>(NumCovered);

		// 4. Padding: each pass fills empty texels next to filled ones with the average of those neighbors
		VrrCore::PadIslands(Size, Settings.Padding, Filled, [&Pixels](int32 Index, TConstArrayView<int32> Neighbors)
		{
			FLinearColor Total = FLinearColor::Transparent;
			for (const int32 Neighbor : Neighbors)
			{
				Total += Pixels[Neighbor];
			}
			Pixels[Index] = Total / static_cast<float>(Neighbors.Num());
		});
		// The rest: the average color, or a flat normal
		const FLinearColor Background = Settings.bNormalMap ? FLinearColor(0.5f, 0.5f, 1.0f, 1.0f) : Mean;
		for (int32 Index = 0; Index < NumTexels; ++Index)
		{
			if (!Filled[Index])
			{
				Pixels[Index] = Background;
			}
		}

		// 5. Write: the source's pixel format, encoded for the chosen sRGB setting
		SlowTask.EnterProgressFrame(0.5f, LOCTEXT("Writing", "Writing the texture..."));
		FText Error;
		UTexture2D* Output = FindOrCreateTexture(VrrCore::NormalizeContentFolder(Settings.OutputFolder), Settings.OutputName, Settings.bOverwrite, Texture, Error);
		if (!Output)
		{
			Result.Message = Error;
			return Result;
		}
		const ERawImageFormat::Type Format = FImageCoreUtils::ConvertToRawImageFormat(Texture->Source.GetFormat());
		const EGammaSpace Gamma = Settings.bSRGB && ERawImageFormat::GetFormatNeedsGammaSpace(Format) ? EGammaSpace::sRGB : EGammaSpace::Linear;
		FImage Final;
		Baked.CopyTo(Final, Format, Gamma);

		Output->Modify();
		Output->PreEditChange(nullptr);
		Output->Source.Init(Final);
		Output->SRGB = Settings.bSRGB;
		Output->CompressionSettings = Settings.CompressionSettings;
		Output->CompressionNoAlpha = Settings.bCompressionNoAlpha;
		Output->LODGroup = Settings.LODGroup;
		Output->MipGenSettings = Settings.MipGenSettings;
		Output->AddressX = Texture->AddressX;
		Output->AddressY = Texture->AddressY;
		Output->Filter = Texture->Filter;
		Output->PostEditChange();
		Output->MarkPackageDirty();
		Result.Texture = Output;

		bool bSaved = false;
		if (Settings.bSaveToDisk)
		{
			bSaved = VrrCore::SaveAssets({ Output });
		}

		// Result: what was made, coverage, then warnings
		FText Message = FText::Format(LOCTEXT("Done", "Rebaked {0} from UV{1} to UV{2}: {3} ({4} x {4}), {5} of the texture covered."),
			FText::FromString(Texture->GetName()), Settings.SourceChannel, Settings.TargetChannel, FText::FromString(Output->GetName()), Size,
			FText::AsPercent(static_cast<float>(NumCovered) / NumTexels));
		if (Settings.bSaveToDisk)
		{
			Message = FText::Format(bSaved ? LOCTEXT("Saved", "{0} Saved.") : LOCTEXT("SaveFailed", "{0} Saving failed: save it from the Content Browser."), Message);
		}
		else
		{
			Message = FText::Format(LOCTEXT("Unsaved", "{0} Not saved yet."), Message);
		}
		if (NumConflicts > 0)
		{
			Message = FText::Format(LOCTEXT("Conflicts", "{0} {1} texels are shared by separate parts of the mesh in UV{2} (overlapping or stacked islands), so they mix both. Use a unique channel, such as one from UV Layouts."),
				Message, NumConflicts, Settings.TargetChannel);
		}
		if (NumOutOfRange > 0)
		{
			Message = FText::Format(LOCTEXT("OutOfRange", "{0} {1} triangles reach outside 0-1 in UV{2}; only the part inside was baked."), Message, NumOutOfRange, Settings.TargetChannel);
		}
		Result.bSuccess = true;
		Result.Message = Message;
		return Result;
	}
}

#undef LOCTEXT_NAMESPACE
