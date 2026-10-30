// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

/*
 * Toolbox > Rebake Texture page (UI only).
 *
 * The page edits an FRebakeTextureSettings and hands it to TextureRebaker::Rebake (Core_RebakeTexture.cpp),
 * which samples the texture through the source UVs and writes it through the target UVs.
 */

#include "Menu_RebakeTexture.h"

#include "Core_Common.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "SEnumCombo.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_RebakeTexture"

namespace
{
	FText GetFilterLabel(ERebakeFilter Filter)
	{
		return Filter == ERebakeFilter::Nearest ? LOCTEXT("Nearest", "Nearest") : LOCTEXT("Bilinear", "Bilinear");
	}

	FText GetSupersamplingLabel(int32 Samples)
	{
		return Samples <= 1 ? LOCTEXT("SupersampleOff", "Off (1 sample)") : FText::Format(LOCTEXT("SupersampleN", "{0} x {0} ({1} samples)"), Samples, Samples * Samples);
	}

	/** A combo over a UENUM, writing into *Value. */
	template <typename EnumType>
	TSharedRef<SWidget> MakeEnumCombo(TEnumAsByte<EnumType>* Value)
	{
		return SNew(SEnumComboBox, StaticEnum<EnumType>())
			.CurrentValue_Lambda([Value]() { return static_cast<int32>(Value->GetValue()); })
			.OnEnumSelectionChanged_Lambda([Value](int32 NewValue, ESelectInfo::Type) { *Value = static_cast<EnumType>(NewValue); });
	}

}

/** Builds the page: source (settings | texture), target (settings | bake), compression, then save and rebake. */
void SMenu_RebakeTexture::Construct(const FArguments& InArgs)
{
	FilterOptions.Add(MakeShared<ERebakeFilter>(ERebakeFilter::Bilinear));
	FilterOptions.Add(MakeShared<ERebakeFilter>(ERebakeFilter::Nearest));

	ChildSlot
	[
		VrrMenu::Stack({
			CreateSourceSection(),
			CreateTargetSection(),
			CreateCompressionSection(),
			CreateSaveSection(),
		})
	];
}

// LAYOUT
/** A preview square (VrrMenu::TexturePreview) with a caption under it. */
TSharedRef<SWidget> SMenu_RebakeTexture::CreateTexturePreview(const FSlateBrush* Brush, TAttribute<bool> HasTexture, TAttribute<FText> Caption)
{
	return SNew(SBox)
		.WidthOverride(PreviewSize)
		[
			VrrMenu::Rows({
				VrrMenu::TexturePreview(Brush, PreviewSize, HasTexture),
				VrrMenu::HelpText(Caption),
			})
		];
}

void SMenu_RebakeTexture::SetPreviewTexture(FSlateBrush& Brush, TObjectPtr<UTexture2D>& OutHeld, UTexture2D* NewTexture)
{
	OutHeld = NewTexture;
	VrrMenu::SetBrushTexture(Brush, NewTexture);
}

void SMenu_RebakeTexture::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(SourcePreviewTexture);
	Collector.AddReferencedObject(ResultPreviewTexture);
}

/** "T_Rock_UV1  |  2048 x 2048", or a hint before the first bake. */
FText SMenu_RebakeTexture::GetResultCaption() const
{
	if (!ResultPreviewTexture)
	{
		return LOCTEXT("NoResult", "The finished bake shows here.");
	}
	return FText::Format(LOCTEXT("ResultCaption", "{0}  |  {1} x {2}"), FText::FromString(ResultPreviewTexture->GetName()),
		FText::AsNumber(ResultPreviewTexture->Source.GetSizeX(), &FNumberFormattingOptions::DefaultNoGrouping()),
		FText::AsNumber(ResultPreviewTexture->Source.GetSizeY(), &FNumberFormattingOptions::DefaultNoGrouping()));
}

/** Mesh, texture and the source UVs | the source texture. */
TSharedRef<SWidget> SMenu_RebakeTexture::CreateSourceSection()
{
	TSharedRef<SWidget> SettingsColumn = VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("MeshLabel", "Mesh"),
			VrrMenu::MeshPicker(VrrMenu::EMeshTypes::StaticOrSkeletal,
				[this]() { return Mesh.Get(); },
				[this](UObject* NewMesh) { SetMesh(NewMesh); },
				[this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_RebakeTexture::GetMeshInfoText)),
		VrrMenu::Row(LOCTEXT("TextureLabel", "Texture"),
			VrrMenu::AssetPicker(UTexture2D::StaticClass(), nullptr,
				[this]() { return Texture.Get(); },
				[this](UObject* NewTexture) { SetTexture(Cast<UTexture2D>(NewTexture)); },
				LOCTEXT("UseSelectedTextureTip", "Use the texture selected in the Content Browser"),
				LOCTEXT("NoTextureSelected", "Select a texture in the Content Browser first."),
				[this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Note(TAttribute<FText>::CreateSP(this, &SMenu_RebakeTexture::GetTextureInfoText)),
		VrrMenu::Row(LOCTEXT("SourceUVs", "Source UVs"),
			VrrMenu::UVChannelCombo([this]() { return NumUVChannels; }, &Settings.SourceChannel),
			LOCTEXT("SourceUVsTip", "The channel the texture was made for (usually UV0)")),
	});

	TSharedRef<SWidget> PreviewColumn = CreateTexturePreview(&SourceBrush,
		TAttribute<bool>::CreateLambda([this]() { return SourcePreviewTexture != nullptr; }),
		TAttribute<FText>::CreateLambda([this]() { return SourcePreviewTexture ? FText::FromString(SourcePreviewTexture->GetName()) : LOCTEXT("PickTexture", "Pick the texture to rebake."); }));

	return VrrMenu::HelpCard(LOCTEXT("SourceHeader", "Source"),
		LOCTEXT("SourceHelp", "Each texel of the new texture finds the triangle covering it in the target UVs, takes the same point on the surface in the source UVs, "
			"and samples the texture there. Use it to move a texture made for overlapping or tiled UV0 onto a unique channel (such as one from UV Layouts), or between any two layouts."),
		VrrMenu::SettingsAndPreview(SettingsColumn, PreviewColumn));
}

/** Target UVs, resolution and sampling | the finished bake. */
TSharedRef<SWidget> SMenu_RebakeTexture::CreateTargetSection()
{
	TSharedRef<SWidget> SettingsColumn = VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("TargetUVs", "Target UVs"),
			VrrMenu::UVChannelCombo([this]() { return NumUVChannels; }, &Settings.TargetChannel, [this](int32) { RefreshOutputName(); }),
			LOCTEXT("TargetUVsTip", "The channel the new texture is laid out in. It should be unique (no overlapping islands), or overlapping parts share texels")),
		VrrMenu::Row(LOCTEXT("Resolution", "Resolution"),
			VrrMenu::ResolutionCombo(&Settings.Resolution),
			LOCTEXT("ResolutionTip", "Width and height of the new texture. Starts at the source texture's size")),
		VrrMenu::Row(LOCTEXT("Filter", "Filter"),
			SNew(SComboBox<TSharedPtr<ERebakeFilter>>)
			.OptionsSource(&FilterOptions)
			.OnGenerateWidget_Lambda([](TSharedPtr<ERebakeFilter> Option) { return SNew(STextBlock).Text(GetFilterLabel(*Option)); })
			.OnSelectionChanged_Lambda([this](TSharedPtr<ERebakeFilter> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Settings.Filter = *Option;
				}
			})
			[
				SNew(STextBlock).Text_Lambda([this]() { return GetFilterLabel(Settings.Filter); })
			],
			LOCTEXT("FilterTip", "Bilinear blends the four nearest source texels (smooth). Nearest takes the closest one (crisp pixel art, ID masks)")),
		VrrMenu::Row(LOCTEXT("Supersampling", "Supersampling"),
			VrrMenu::IntCombo([]() { return TArray<int32>{ 1, 2, 3, 4 }; }, &Settings.Supersampling, &GetSupersamplingLabel),
			LOCTEXT("SupersamplingTip", "Samples per texel. More smooths island edges and keeps detail when the new layout is smaller than the source; time grows with the sample count")),
		VrrMenu::Row(LOCTEXT("NormalMap", "Normal map"),
			VrrMenu::CheckBox(&Settings.bNormalMap, LOCTEXT("NormalMapCheck", "Rotate normals to the target UVs"),
				LOCTEXT("NormalMapTip", "For tangent-space normal maps: each normal is turned from the source UVs' tangent frame into the target UVs' frame, "
					"so islands that are rotated or mirrored in the new layout still shade the same. Turned on for textures with Normalmap compression"))),
		VrrMenu::Row(LOCTEXT("Padding", "Padding (px)"),
			VrrMenu::IntField(&Settings.Padding, 0, 64),
			LOCTEXT("PaddingTip", "Grow each island by this many pixels so filtering and mips at the edges don't pick up empty texels")),
	});

	TSharedRef<SWidget> PreviewColumn = CreateTexturePreview(&ResultBrush,
		TAttribute<bool>::CreateLambda([this]() { return ResultPreviewTexture != nullptr; }),
		TAttribute<FText>::CreateSP(this, &SMenu_RebakeTexture::GetResultCaption));

	return VrrMenu::Card(LOCTEXT("TargetHeader", "Target"), VrrMenu::SettingsAndPreview(SettingsColumn, PreviewColumn));
}

/** Compression, sRGB, alpha, texture group and mips: start as the source texture's. */
TSharedRef<SWidget> SMenu_RebakeTexture::CreateCompressionSection()
{
	TSharedRef<SWidget> CopyButton = SNew(SButton)
		.Text(LOCTEXT("CopyFromSource", "Copy From Source"))
		.ToolTipText(LOCTEXT("CopyFromSourceTip", "Reset these settings, the resolution and the normal map option to match the source texture"))
		.IsEnabled_Lambda([this]() { return Texture.IsValid(); })
		.OnClicked(this, &SMenu_RebakeTexture::OnCopySettingsClicked);

	return VrrMenu::Card(LOCTEXT("CompressionHeader", "Compression"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Compression", "Compression"),
			MakeEnumCombo(&Settings.CompressionSettings),
			LOCTEXT("CompressionTip", "How the texture is compressed on the GPU (Default for color, Normalmap, Masks for packed linear data...)")),
		VrrMenu::Row(LOCTEXT("SRGB", "Color space"),
			VrrMenu::CheckBox(&Settings.bSRGB, LOCTEXT("SRGBCheck", "sRGB"),
				LOCTEXT("SRGBTip", "On for color (albedo, emissive); off for linear data (normals, masks, roughness). The pixels are encoded to match, so the texture looks the same either way"))),
		VrrMenu::Row(LOCTEXT("Alpha", "Alpha"),
			VrrMenu::CheckBox(&Settings.bCompressionNoAlpha, LOCTEXT("NoAlpha", "Compress without alpha"),
				LOCTEXT("NoAlphaTip", "Drop the alpha channel when compressing (DXT1/BC1 instead of DXT5/BC3: half the memory)"))),
		VrrMenu::Row(LOCTEXT("LODGroup", "Texture group"),
			MakeEnumCombo(&Settings.LODGroup),
			LOCTEXT("LODGroupTip", "The texture group (LOD group): sets its streaming, filtering and size limits")),
		VrrMenu::Row(LOCTEXT("MipGen", "Mip generation"),
			MakeEnumCombo(&Settings.MipGenSettings),
			LOCTEXT("MipGenTip", "How mips are made. From Texture Group is the usual choice")),
	}), CopyButton);
}

/** Folder, name, overwrite and save to disk, then the Rebake button. */
TSharedRef<SWidget> SMenu_RebakeTexture::CreateSaveSection()
{
	return VrrMenu::Card(LOCTEXT("SaveHeader", "Save"), VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("Folder", "Folder"),
			VrrMenu::FolderPicker(&Settings.OutputFolder, TEXT("/Game"), [this](const FText& Error) { SetStatus(Error, true); })),
		VrrMenu::Row(LOCTEXT("Name", "Name"),
			VrrMenu::NameField(&Settings.OutputName),
			LOCTEXT("NameTip", "The new texture's name. Starts as <texture>_UV<target channel>")),
		VrrMenu::Row(LOCTEXT("Existing", "Existing texture"),
			VrrMenu::CheckBox(&Settings.bOverwrite, LOCTEXT("Overwrite", "Overwrite"),
				LOCTEXT("OverwriteTip", "Rebaking updates a texture with the same name in place, so materials using it pick up the change. Off creates numbered copies"))),
		VrrMenu::Row(LOCTEXT("SaveToDisk", "Save"),
			VrrMenu::CheckBox(&Settings.bSaveToDisk, LOCTEXT("SaveToDiskCheck", "Save to disk when done"),
				LOCTEXT("SaveToDiskTip", "Save the texture's package right away. Off leaves it unsaved (marked dirty) in the Content Browser"))),
		VrrMenu::Note(TAttribute<FText>::CreateLambda([this]()
		{
			if (Settings.OutputName.IsEmpty())
			{
				return FText::GetEmpty();
			}
			return FText::Format(LOCTEXT("OutputPath", "Creates {0}/{1}"), FText::FromString(VrrCore::NormalizeContentFolder(Settings.OutputFolder)), FText::FromString(Settings.OutputName));
		})),
		VrrMenu::ActionButton(LOCTEXT("Rebake", "Rebake Texture"),
			LOCTEXT("RebakeTip", "Sample the texture through the source UVs and write it through the target UVs"),
			[this]() { return GetRebakeBlocker(); },
			FOnClicked::CreateSP(this, &SMenu_RebakeTexture::OnRebakeClicked)),
		VrrMenu::StatusLine(&StatusText, &bStatusIsError),
	}));
}

// BEHAVIOR
/** Switches to a new mesh: reads its channels and picks UV0 -> UV1. */
void SMenu_RebakeTexture::SetMesh(UObject* NewMesh)
{
	Mesh = VrrCore::IsStaticOrSkeletalMesh(NewMesh) ? NewMesh : nullptr;
	NumUVChannels = Mesh.IsValid() ? VrrCore::GetNumUVChannels(Mesh.Get()) : 0;
	StatusText = FText::GetEmpty();

	Settings.SourceChannel = 0;
	Settings.TargetChannel = FMath::Min(1, FMath::Max(NumUVChannels - 1, 0));
	RefreshOutputName();
}

/** <texture>_UV<target channel>, once there is a texture. */
void SMenu_RebakeTexture::RefreshOutputName()
{
	if (Texture.IsValid())
	{
		Settings.OutputName = TextureRebaker::SuggestName(Texture.Get(), Settings.TargetChannel);
	}
}

/** Switches to a new texture: copies its compression settings and size, and suggests a folder and name. */
void SMenu_RebakeTexture::SetTexture(UTexture2D* NewTexture)
{
	Texture = NewTexture;
	StatusText = FText::GetEmpty();
	SetPreviewTexture(SourceBrush, SourcePreviewTexture, NewTexture);
	if (NewTexture)
	{
		TextureRebaker::CopyTextureSettings(NewTexture, Settings);
		Settings.OutputFolder = FPackageName::GetLongPackagePath(NewTexture->GetOutermost()->GetName());
		RefreshOutputName();
	}
}

/** "Static Mesh | N triangles | N UV channels" or why the mesh can't be used. */
FText SMenu_RebakeTexture::GetMeshInfoText() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NoMesh", "Pick a Static Mesh or Skeletal Mesh.");
	}
	const FMeshDescription* MeshDescription = VrrCore::GetMeshDescription(Mesh.Get(), 0);
	if (!MeshDescription)
	{
		return LOCTEXT("NoMeshData", "This mesh has no source mesh data for LOD 0.");
	}
	return FText::Format(LOCTEXT("MeshInfo", "{0}  |  {1} triangles  |  {2} UV channels"),
		VrrCore::IsSkeletalMesh(Mesh.Get()) ? LOCTEXT("Skeletal", "Skeletal Mesh") : LOCTEXT("Static", "Static Mesh"),
		MeshDescription->Triangles().Num(), NumUVChannels);
}

/** "2048 x 2048 | BGRA8 | Default, sRGB" or why the texture can't be read. */
FText SMenu_RebakeTexture::GetTextureInfoText() const
{
	if (!Texture.IsValid())
	{
		return LOCTEXT("NoTexture", "Pick the texture to rebake.");
	}
	const FText Blocker = TextureRebaker::GetTextureBlocker(Texture.Get());
	if (!Blocker.IsEmpty())
	{
		return Blocker;
	}
	const UEnum* FormatEnum = StaticEnum<ETextureSourceFormat>();
	const UEnum* CompressionEnum = StaticEnum<TextureCompressionSettings>();
	return FText::Format(LOCTEXT("TextureInfo", "{0} x {1}  |  {2}  |  {3}, {4}"),
		FText::AsNumber(Texture->Source.GetSizeX(), &FNumberFormattingOptions::DefaultNoGrouping()),
		FText::AsNumber(Texture->Source.GetSizeY(), &FNumberFormattingOptions::DefaultNoGrouping()),
		FormatEnum ? FormatEnum->GetDisplayNameTextByValue(Texture->Source.GetFormat()) : FText::GetEmpty(),
		CompressionEnum ? CompressionEnum->GetDisplayNameTextByValue(Texture->CompressionSettings) : FText::GetEmpty(),
		Texture->SRGB ? LOCTEXT("InfoSRGB", "sRGB") : LOCTEXT("InfoLinear", "linear"));
}

FText SMenu_RebakeTexture::GetRebakeBlocker() const
{
	if (!Mesh.IsValid())
	{
		return LOCTEXT("NeedMesh", "Pick a mesh first.");
	}
	if (!VrrCore::GetMeshDescription(Mesh.Get(), 0))
	{
		return LOCTEXT("NeedMeshData", "This mesh has no LOD 0 source data.");
	}
	if (NumUVChannels < 2)
	{
		return LOCTEXT("NeedTwoChannels", "The mesh needs at least two UV channels (create one in UV Layouts).");
	}
	const FText TextureBlocker = TextureRebaker::GetTextureBlocker(Texture.Get());
	if (!TextureBlocker.IsEmpty())
	{
		return TextureBlocker;
	}
	if (Settings.SourceChannel == Settings.TargetChannel)
	{
		return LOCTEXT("SameChannel", "Pick different source and target UVs.");
	}
	if (Settings.OutputName.IsEmpty())
	{
		return LOCTEXT("NeedName", "Enter a name for the new texture.");
	}
	return FText::GetEmpty();
}

/** Copies the source texture's compression, sRGB and LOD group settings into the output settings. */
FReply SMenu_RebakeTexture::OnCopySettingsClicked()
{
	TextureRebaker::CopyTextureSettings(Texture.Get(), Settings);
	return FReply::Handled();
}

/** Rebakes, then shows the result and selects the new texture in the Content Browser. */
FReply SMenu_RebakeTexture::OnRebakeClicked()
{
	if (!GetRebakeBlocker().IsEmpty())
	{
		return FReply::Handled();
	}
	const FRebakeTextureResult Result = TextureRebaker::Rebake(Mesh.Get(), Texture.Get(), Settings);
	SetStatus(Result.Message, !Result.bSuccess);
	VrrMenu::Notify(Result.bSuccess ? LOCTEXT("RebakeDone", "Texture rebaked") : Result.Message,
		Result.bSuccess ? VrrMenu::ENotify::Success : VrrMenu::ENotify::Error);
	if (Result.Texture)
	{
		SetPreviewTexture(ResultBrush, ResultPreviewTexture, Result.Texture);
		VrrCore::ShowInContentBrowser({ Result.Texture });
	}
	return FReply::Handled();
}

void SMenu_RebakeTexture::SetStatus(const FText& Text, bool bError)
{
	StatusText = Text;
	bStatusIsError = bError;
}

#undef LOCTEXT_NAMESPACE
