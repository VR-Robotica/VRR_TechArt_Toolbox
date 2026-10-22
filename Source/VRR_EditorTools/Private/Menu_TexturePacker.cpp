// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Toolbox > Texture Packer page (UI only).
 *
 * The page edits an FTexturePackSettings (4 source slots, 4 output channels, output type) and shows a live
 * preview; TexturePacker (Core_TexturePacker.cpp) does the reading, packing and asset creation.
 * Preview: sources are shrunk once into PreviewSourceCache, so changing a channel only re-packs small images.
 */

#include "Menu_TexturePacker.h"

#include "Core_Common.h"
#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "PropertyCustomizationHelpers.h"
#include "TextureResource.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSegmentedControl.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "SMenu_TexturePacker"

/** Tint per output channel (R, G, B, A) for the channel rows. */
namespace TexturePackerUI
{
	const FLinearColor ChannelColors[4] =
	{
		FLinearColor(1.0f, 0.25f, 0.25f),
		FLinearColor(0.3f, 1.0f, 0.3f),
		FLinearColor(0.35f, 0.55f, 1.0f),
		FLinearColor(0.85f, 0.85f, 0.85f)
	};

	// Channels grid columns, shared by the heading row and each channel row
	constexpr float ChannelLabelWidth = 24.0f;	// "R" plus its gap
	constexpr float ValueWidth = VrrMenu::NumberWidth;	// source channel combo or constant value
	constexpr float InvertWidth = 44.0f;
}

/** Builds the page: sources and channels cards side by side, then output (with the Pack button) and preview. */
void SMenu_TexturePacker::Construct(const FArguments& InArgs)
{
	SourceOptions.Add(MakeShared<int32>(INDEX_NONE));
	for (int32 i = 0; i < NumSources; ++i)
	{
		SourceOptions.Add(MakeShared<int32>(i));
	}
	for (int32 i = 0; i < NumChannels; ++i)
	{
		ChannelOptions.Add(MakeShared<int32>(i));
	}

	CompressionOptions.Add(MakeShared<FString>(TEXT("Masks (Recommended)")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Grayscale")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Diffuse")));
	CompressionOptions.Add(MakeShared<FString>(TEXT("Normal")));

	PreviewBrush.DrawAs = ESlateBrushDrawType::Image;
	PreviewBrush.ImageSize = FVector2D(PreviewMaxSize, PreviewMaxSize);

	// Source textures, one picker per slot
	TArray<TSharedRef<SWidget>> SourceRows;
	for (int32 i = 0; i < NumSources; ++i)
	{
		SourceRows.Add(CreateSourceRow(i));
	}

	// Output channels: a heading row, then one row per channel
	auto Heading = [](const FText& Text) { return SNew(STextBlock).Text(Text).Font(VrrMenu::HelpFont()).ColorAndOpacity(FSlateColor::UseSubduedForeground()); };
	TArray<TSharedRef<SWidget>> ChannelRows;
	ChannelRows.Add(SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(TexturePackerUI::ChannelLabelWidth)
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			Heading(LOCTEXT("SourceHeading", "Source"))
		]
		+ SHorizontalBox::Slot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(TexturePackerUI::ValueWidth)
			[
				Heading(LOCTEXT("ValueHeading", "Channel / Value"))
			]
		]
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox).WidthOverride(TexturePackerUI::InvertWidth).HAlign(HAlign_Center)
			[
				Heading(LOCTEXT("InvertHeading", "Invert"))
			]
		]);
	for (int32 i = 0; i < NumChannels; ++i)
	{
		ChannelRows.Add(CreateChannelRow(i));
	}

	// Two cards next to each other: the left one fills, the right one takes RightFill times as much (0 = its own width)
	auto SideBySide = [](const TSharedRef<SWidget>& Left, const TSharedRef<SWidget>& Right, float RightFill) -> TSharedRef<SWidget>
	{
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f).Padding(0.0f, 0.0f, VrrMenu::CardGap, 0.0f)
			[
				Left
			];
		if (RightFill > 0.0f)
		{
			Box->AddSlot().FillWidth(RightFill)[Right];
		}
		else
		{
			Box->AddSlot().AutoWidth()[Right];
		}
		return Box;
	};

	ChildSlot
	[
		VrrMenu::Stack({
			SideBySide(
				VrrMenu::Card(LOCTEXT("SourcesHeader", "Source Textures"), VrrMenu::Rows(SourceRows)),
				VrrMenu::Card(LOCTEXT("ChannelsHeader", "Channels"), VrrMenu::Rows(ChannelRows)),
				1.3f),
			SideBySide(
				VrrMenu::Card(LOCTEXT("TargetHeader", "Output"),
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						CreateOutputSettings()
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						VrrMenu::ActionButton(LOCTEXT("PackButton", "Create Packed Texture"),
							LOCTEXT("PackButtonTip", "Create a new texture asset from the selected channels"),
							[this]() { return GetPackBlocker(); },
							FOnClicked::CreateSP(this, &SMenu_TexturePacker::OnPackClicked))
					]),
				VrrMenu::Card(LOCTEXT("PreviewHeader", "Preview"), CreatePreview()),
				0.0f),
		})
	];
}

// LAYOUT
/** Texture picker for one source slot. */
TSharedRef<SWidget> SMenu_TexturePacker::CreateSourceRow(int32 SourceIndex)
{
	return SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 8.0f, 0.0f)
	[
		SNew(STextBlock)
		.Text(FText::AsNumber(SourceIndex + 1))
		.MinDesiredWidth(16.0f)
	]

	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	[
		SNew(SObjectPropertyEntryBox)
		.AllowedClass(UTexture2D::StaticClass())
		.ObjectPath_Lambda([this, SourceIndex]()
		{
			return Settings.Sources[SourceIndex].IsValid() ? Settings.Sources[SourceIndex]->GetPathName() : FString();
		})
		.OnObjectChanged_Lambda([this, SourceIndex](const FAssetData& AssetData)
		{
			Settings.Sources[SourceIndex] = Cast<UTexture2D>(AssetData.GetAsset());
			OnSourcesChanged();
		})
		.AllowClear(true)
		.DisplayThumbnail(true)
		.ThumbnailPool(UThumbnailManager::Get().GetSharedThumbnailPool())
	];
}

/** One output channel: which source and which of its channels, invert, and the constant used when no source is picked. */
TSharedRef<SWidget> SMenu_TexturePacker::CreateChannelRow(int32 ChannelIndex)
{
	FPackedChannel* Channel = &Settings.Channels[ChannelIndex];

	return SNew(SHorizontalBox)
	.IsEnabled_Lambda([this, ChannelIndex]() { return Settings.IsChannelEnabled(ChannelIndex); })
	.ToolTipText_Lambda([this, ChannelIndex]()
	{
		return Settings.IsChannelEnabled(ChannelIndex)
			? FText::GetEmpty()
			: FText::Format(LOCTEXT("ChannelDisabledTip", "{0} compression doesn't use this channel"), GetCompressionText());
	})

	// Output channel label
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(TexturePackerUI::ChannelLabelWidth)
		[
			SNew(STextBlock)
			.Text(GetChannelName(ChannelIndex))
			.ColorAndOpacity(TexturePackerUI::ChannelColors[ChannelIndex])
			.Font(FAppStyle::GetFontStyle("BoldFont"))
		]
	]

	// Source texture
	+ SHorizontalBox::Slot()
	.FillWidth(1.0f)
	.Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SNew(SComboBox<TSharedPtr<int32>>)
		.OptionsSource(&SourceOptions)
		.InitiallySelectedItem(SourceOptions[0])
		.OnGenerateWidget_Lambda([this](TSharedPtr<int32> Option)
		{
			return SNew(STextBlock).Text(GetSourceOptionText(*Option));
		})
		.OnSelectionChanged_Lambda([this, Channel](TSharedPtr<int32> Option, ESelectInfo::Type)
		{
			if (Option.IsValid())
			{
				Channel->SourceIndex = *Option;
				UpdatePreview();
			}
		})
		[
			SNew(STextBlock)
			.Text_Lambda([this, Channel]() { return GetSourceOptionText(Channel->SourceIndex); })
		]
	]

	// Source channel (only when a texture is picked)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(TexturePackerUI::ValueWidth)
		.Visibility_Lambda([Channel]() { return Channel->SourceIndex != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SComboBox<TSharedPtr<int32>>)
			.OptionsSource(&ChannelOptions)
			.InitiallySelectedItem(ChannelOptions[0])
			.ToolTipText(LOCTEXT("SourceChannelTip", "Which channel of the source texture to copy"))
			.OnGenerateWidget_Lambda([](TSharedPtr<int32> Option)
			{
				return SNew(STextBlock)
					.Text(GetChannelName(*Option))
					.ColorAndOpacity(TexturePackerUI::ChannelColors[*Option]);
			})
			.OnSelectionChanged_Lambda([this, Channel](TSharedPtr<int32> Option, ESelectInfo::Type)
			{
				if (Option.IsValid())
				{
					Channel->SourceChannel = *Option;
					UpdatePreview();
				}
			})
			[
				SNew(STextBlock)
				.Text_Lambda([Channel]() { return GetChannelName(Channel->SourceChannel); })
				.ColorAndOpacity_Lambda([Channel]() { return FSlateColor(TexturePackerUI::ChannelColors[Channel->SourceChannel]); })
				.MinDesiredWidth(16.0f)
			]
		]
	]

	// Constant value (only when no texture is picked)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(TexturePackerUI::ValueWidth)
		.Visibility_Lambda([Channel]() { return Channel->SourceIndex == INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SSpinBox<float>)
			.MinValue(0.0f)
			.MaxValue(1.0f)
			.Delta(0.01f)
			.ToolTipText(LOCTEXT("DefaultValueTip", "Constant value used when no source texture is picked"))
			.Value_Lambda([Channel]() { return Channel->DefaultValue; })
			.OnValueChanged_Lambda([this, Channel](float NewValue)
			{
				Channel->DefaultValue = NewValue;
				UpdatePreview();
			})
		]
	]

	// Invert (centered under its heading)
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SBox)
		.WidthOverride(TexturePackerUI::InvertWidth)
		.HAlign(HAlign_Center)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("InvertTip", "Invert this channel (1 - value)"))
			.IsChecked_Lambda([Channel]() { return Channel->bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this, Channel](ECheckBoxState State)
			{
				Channel->bInvert = State == ECheckBoxState::Checked;
				UpdatePreview();
			})
		]
	];
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
/** "2: T_Rock_R", or "None (constant value)". */
FText SMenu_TexturePacker::GetSourceOptionText(int32 SourceIndex) const
{
	if (SourceIndex == INDEX_NONE)
	{
		return LOCTEXT("NoSource", "None (constant value)");
	}

	const FString TextureName = Settings.Sources[SourceIndex].IsValid() ? Settings.Sources[SourceIndex]->GetName() : TEXT("(empty)");
	return FText::FromString(FString::Printf(TEXT("%d: %s"), SourceIndex + 1, *TextureName));
}

/** R, G, B or A. */
FText SMenu_TexturePacker::GetChannelName(int32 ChannelIndex)
{
	static const FText Names[4] = { INVTEXT("R"), INVTEXT("G"), INVTEXT("B"), INVTEXT("A") };
	return Names[FMath::Clamp(ChannelIndex, 0, 3)];
}

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
/** Re-reads the small preview copy of every source, then updates the preview. */
void SMenu_TexturePacker::RefreshPreviewSources()
{
	// Shrink each source once so preview updates stay fast with large textures
	for (int32 i = 0; i < NumSources; ++i)
	{
		TexturePacker::ReadPreviewSource(Settings.Sources[i].Get(), PreviewMaxSize, PreviewSourceCache[i]);
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
