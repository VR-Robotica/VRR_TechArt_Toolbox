// Copyright VR Robotica. All Rights Reserved.

/*
 * Toolbox > Texture Packer page (UI only).
 *
 * The page edits an FTexturePackSettings (4 source slots, 4 output channels, output type) through the channel
 * graph (Menu_TexturePackerGraph.cpp) and shows a live preview; TexturePacker (Core_TexturePacker.cpp) does the
 * reading, packing and asset creation.
 * Preview: sources are shrunk once into PreviewSourceCache, so changing a channel only re-packs small images.
 */

#include "Menu_TexturePacker.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "Menu_TexturePackerGraph.h"
#include "TextureResource.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_TexturePacker"

/** Builds the page: the channel graph with the preview beside it, then the output settings (with the Pack button). */
void SMenu_TexturePacker::Construct(const FArguments& InArgs)
{
	CompressionOptions.Add(MakeShared<FString>(TEXT("Masks (Recommended)")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Grayscale")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Diffuse")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Normal")));

	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageSize = FVector2D(PreviewMaxSize, PreviewMaxSize);

	ChildSlot
	[
		VrrMenu::Stack({
			// Graph and preview side by side, so the Output node sits right next to the result
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(0.0f, 0.0f, VrrMenu::CardGap, 0.0f)
			[
				VrrMenu::HelpCard(LOCTEXT("ChannelsHeader", "Channels"),
					LOCTEXT("ChannelsHelp", "Pick up to four textures, then drag from a texture's channel pin to an Output channel pin. "
						"A texture only shows pins for the channels it has: grayscale images only R, color images R, G and B, and A when it has alpha data. "
						"One channel can feed several outputs. Alt+click or right-click a pin to break its wires; an output without a wire uses its constant value."),
					CreateGraph())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				VrrMenu::Card(LOCTEXT("PreviewHeader", "Preview"), CreatePreview())
			],
			VrrMenu::Card(LOCTEXT("TargetHeader", "Output"),
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[
					CreateOutputSettings()
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
				[
					VrrMenu::ActionButton(LOCTEXT("PackButton", "Create Packed Texture"),
						LOCTEXT("PackButtonTip", "Create a new texture asset from the wired channels"),
						[this]() { return GetPackBlocker(); },
						FOnClicked::CreateSP(this, &SMenu_TexturePacker::OnPackClicked))
				]),
		})
	];
}

// LAYOUT
/** Source texture nodes wired into the output node (STexturePackerGraph). */
TSharedRef<SWidget> SMenu_TexturePacker::CreateGraph()
{
	return SNew(STexturePackerGraph)
		.Settings(&Settings)
		.SourceChannels(SourceChannels)
		.SourceNotes(SourceNotes)
		.CompressionText_Lambda([this]() { return GetCompressionText(); })
		.OnSourcesChanged([this]() { OnSourcesChanged(); })
		.OnChannelsChanged([this]() { UpdatePreview(); });
}

/** Output name, folder and type (compression). */
TSharedRef<SWidget> SMenu_TexturePacker::CreateOutputSettings()
{
	return VrrMenu::Rows({
		VrrMenu::Row(LOCTEXT("NameLabel", "Name"),
			SNew(SBox)
			.WidthOverride(VrrMenu::WideWidth)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(OutputFileName); })
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type)
				{
					OutputFileName = NewText.ToString().TrimStartAndEnd();
					bFileNameManuallyEdited = !OutputFileName.IsEmpty();
				})
			],
			LOCTEXT("NameTip", "Suggested from the source names until you type your own")),
		VrrMenu::Row(LOCTEXT("FolderLabel", "Folder"),
			VrrMenu::FolderPicker(&OutputFolder, TEXT("/Game/Textures"), [](const FText& Error)
			{
				VrrMenu::Notify(Error, VrrMenu::ENotify::Warning, 4.0f);
			})),
		VrrMenu::Row(LOCTEXT("CompressionLabel", "Compression"),
			SNew(SComboBox<TSharedPtr<FString>>)
			.OptionsSource(&CompressionOptions)
			.InitiallySelectedItem(CompressionOptions[static_cast<int32>(Settings.Compression)])
			.OnGenerateWidget_Lambda([](TSharedPtr<FString> Option)
			{
				return SNew(STextBlock).Text(FText::FromString(*Option));
			})
			.OnSelectionChanged_Lambda([this](TSharedPtr<FString> Option, ESelectInfo::Type)
			{
				const int32 Index = CompressionOptions.IndexOfByKey(Option);
				if (Index != INDEX_NONE)
				{
					Settings.Compression = static_cast<ETexturePackCompression>(Index);
					UpdatePreview();
				}
			})
			[
				SNew(STextBlock)
				.Text_Lambda([this]() { return GetCompressionText(); })
			]),
	});
}

/** Preview image with RGB/R/G/B/A view buttons and the output size. */
TSharedRef<SWidget> SMenu_TexturePacker::CreatePreview()
{
	return SNew(SVerticalBox)

	// View mode: full color or a single channel
	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	.Padding(0.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(SSegmentedControl<int32>)
		.ToolTipText(LOCTEXT("PreviewModeTip", "Show the full color result or a single channel"))
		.Value_Lambda([this]() { return static_cast<int32>(PreviewMode); })
		.OnValueChanged_Lambda([this](int32 NewMode)
		{
			PreviewMode = static_cast<EPreviewMode>(NewMode);
			UpdatePreview();
		})
		+ SSegmentedControl<int32>::Slot(static_cast<int32>(EPreviewMode::RGB)).Text(INVTEXT("RGB"))
		+ SSegmentedControl<int32>::Slot(static_cast<int32>(EPreviewMode::R)).Text(INVTEXT("R"))
		+ SSegmentedControl<int32>::Slot(static_cast<int32>(EPreviewMode::G)).Text(INVTEXT("G"))
		+ SSegmentedControl<int32>::Slot(static_cast<int32>(EPreviewMode::B)).Text(INVTEXT("B"))
		+ SSegmentedControl<int32>::Slot(static_cast<int32>(EPreviewMode::A)).Text(INVTEXT("A"))
	]

	// Preview image
	+ SVerticalBox::Slot()
	.AutoHeight()
	.HAlign(HAlign_Left)
	[
		SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("Brushes.Recessed"))
		.Padding(2.0f)
		[
			SNew(SBox)
			.WidthOverride(static_cast<float>(PreviewMaxSize))
			.HeightOverride(static_cast<float>(PreviewMaxSize))
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()
				[
					SNew(SImage)
					.Image(&PreviewBrush)
					.Visibility_Lambda([this]() { return PreviewTexture.IsValid() ? EVisibility::Visible : EVisibility::Hidden; })
				]
				+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(LOCTEXT("NoPreview", "Pick a source texture\nfor a channel to preview"))
					.Justification(ETextJustify::Center)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Visibility_Lambda([this]() { return PreviewTexture.IsValid() ? EVisibility::Collapsed : EVisibility::Visible; })
				]
			]
		]
	]

	// Output resolution
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(0.0f, 4.0f, 0.0f, 0.0f)
	[
		SNew(STextBlock)
		.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		.Text_Lambda([this]()
		{
			return OutputSize.X > 0
				? FText::Format(LOCTEXT("OutputSize", "Output: {0} x {1}"), OutputSize.X, OutputSize.Y)
				: FText::GetEmpty();
		})
	];
}

// LABELS
/** Display name of the chosen output type. */
FText SMenu_TexturePacker::GetCompressionText() const
{
	const int32 Index = static_cast<int32>(Settings.Compression);
	return CompressionOptions.IsValidIndex(Index) ? FText::FromString(*CompressionOptions[Index]) : FText::GetEmpty();
}

// BEHAVIOR
/** Why Pack can't run yet (shown next to the button), or empty when it can. */
FText SMenu_TexturePacker::GetPackBlocker() const
{
	if (OutputSize.X <= 0)
	{
		return LOCTEXT("NeedSource", "Pick a source texture for at least one channel.");
	}
	if (OutputFileName.IsEmpty())
	{
		return LOCTEXT("NeedName", "Enter a name.");
	}
	if (OutputFolder.IsEmpty())
	{
		return LOCTEXT("NeedFolder", "Pick a folder.");
	}
	return FText::GetEmpty();
}

/** Creates the packed texture and shows it in the Content Browser, or shows the error. */
FReply SMenu_TexturePacker::OnPackClicked()
{
	FText Error;
	UTexture2D* NewTexture = TexturePacker::CreatePackedTexture(Settings, OutputFolder, OutputFileName, Error);

	if (NewTexture)
	{
		VrrMenu::Notify(FText::Format(LOCTEXT("PackSuccess", "Created {0}"), FText::FromString(NewTexture->GetName())), VrrMenu::ENotify::Success);
		VrrCore::ShowInContentBrowser({ NewTexture });
	}
	else
	{
		VrrMenu::Notify(Error, VrrMenu::ENotify::Error);
	}

	return FReply::Handled();
}

/** After a source changes: suggest a new name (unless the user typed one) and reload the preview copies. */
void SMenu_TexturePacker::OnSourcesChanged()
{
	if (!bFileNameManuallyEdited)
	{
		const FString Suggested = TexturePacker::SuggestName(Settings);
		if (!Suggested.IsEmpty())
		{
			OutputFileName = Suggested;
		}
	}
	RefreshPreviewSources();
}

// PREVIEW
/** Re-reads the small preview copy and the channels of every source, then updates the preview. */
void SMenu_TexturePacker::RefreshPreviewSources()
{
	// Shrink each source once so preview updates stay fast with large textures,
	// and see which channels it has (the small copy is enough to spot gray images and empty alpha)
	for (int32 i = 0; i < NumSources; ++i)
	{
		TexturePacker::ReadPreviewSource(Settings.Sources[i].Get(), PreviewMaxSize, PreviewSourceCache[i]);
		SourceChannels[i] = TexturePacker::GetAvailableChannels(Settings.Sources[i].Get(), PreviewSourceCache[i], SourceNotes[i]);
	}

	// Drop wires from channels a (new or cleared) texture doesn't have
	for (FPackedChannel& Channel : Settings.Channels)
	{
		if (Channel.SourceIndex != INDEX_NONE && (SourceChannels[Channel.SourceIndex] & TexturePacker::ChannelBit(Channel.SourceChannel)) == 0)
		{
			Channel.SourceIndex = INDEX_NONE;
		}
	}

	UpdatePreview();
}

/** Packs the cached sources and uploads the pixels into a transient texture shown by PreviewBrush. */
void SMenu_TexturePacker::UpdatePreview()
{
	OutputSize = Settings.GetOutputSize();

	TArray<uint8> Pixels;
	FIntPoint Size;
	const int32 ViewChannel = PreviewMode == EPreviewMode::RGB ? INDEX_NONE : static_cast<int32>(PreviewMode) - 1;
	if (!TexturePacker::BuildPreview(Settings, PreviewSourceCache, PreviewMaxSize, ViewChannel, Pixels, Size))
	{
		PreviewTexture.Reset();
		PreviewBrush.SetResourceObject(nullptr);
		return;
	}

	// Reuse the preview texture when the size hasn't changed
	if (!PreviewTexture.IsValid() || PreviewTexture->GetSizeX() != Size.X || PreviewTexture->GetSizeY() != Size.Y)
	{
		PreviewTexture.Reset(UTexture2D::CreateTransient(Size.X, Size.Y, PF_B8G8R8A8));
		if (!PreviewTexture.IsValid())
		{
			return;
		}
		// sRGB so stored values show on screen as-is
		PreviewTexture->SRGB = true;
		PreviewTexture->Filter = TF_Bilinear;
	}

	FTexture2DMipMap& Mip = PreviewTexture->GetPlatformData()->Mips[0];
	void* MipData = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(MipData, Pixels.GetData(), Pixels.Num());
	Mip.BulkData.Unlock();
	PreviewTexture->UpdateResource();

	PreviewBrush.SetResourceObject(PreviewTexture.Get());
	PreviewBrush.ImageSize = FVector2D(Size.X, Size.Y) * (static_cast<float>(PreviewMaxSize) / FMath::Max(Size.X, Size.Y));
}

#undef LOCTEXT_NAMESPACE
