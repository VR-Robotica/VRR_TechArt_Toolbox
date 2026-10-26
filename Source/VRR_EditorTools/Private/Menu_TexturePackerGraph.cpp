// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Texture Packer graph (UI only), drawn like a small material graph.
 *
 * Layout: source texture nodes stacked on the left, each with a pin per channel on its right edge; the Output
 * node on the right with an R, G, B and A pin on its left edge. Wires are drawn over the gap in OnPaint, from
 * each output channel's FPackedChannel (SourceIndex, SourceChannel) - the settings are the only state, so the
 * wires can't disagree with what gets packed.
 *
 * Dragging: pressing a pin captures the mouse to the graph, which tracks the cursor, highlights the pin it
 * would connect to and connects on release. A source pin may feed many outputs; an output takes one wire, so
 * a new wire replaces the old one. Alt+click or right-click breaks a pin's wires.
 * Pins for channels a texture doesn't have (see TexturePacker::GetAvailableChannels) are dimmed and can't connect.
 */

#include "Menu_TexturePackerGraph.h"

#include "Engine/Texture2D.h"
#include "MenuWidgets.h"
#include "PropertyCustomizationHelpers.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "STexturePackerGraph"

namespace TexturePackerGraph
{
	/** Horizontal room between the source nodes and the output node, where the wires run. */
	constexpr float WireGap = 140.0f;
	constexpr float WireThickness = 2.0f;
	/** Gap between stacked source nodes. */
	constexpr float NodeGap = 8.0f;
	/** Width of the texture picker inside a source node. */
	constexpr float PickerWidth = 240.0f;
	/** Title bar tints, like the material editor's texture sample and result nodes. */
	const FLinearColor SourceTitleColor(0.05f, 0.22f, 0.32f);
	const FLinearColor OutputTitleColor(0.28f, 0.14f, 0.05f);
	/** A wire snaps to a pin when released this close to its dot (pixels). */
	constexpr float SnapDistance = 14.0f;
}

// PIN
/** One channel pin: a dot plus its letter. The graph owns all behavior; the pin just forwards clicks. */
class STexturePackerPin : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STexturePackerPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, const TSharedRef<STexturePackerGraph>& InGraph, const FTexturePackerPinId& InId)
	{
		Graph = InGraph;
		Id = InId;
		const int32 Channel = Id.Channel;

		Dot = SNew(SImage)
			.Image_Lambda([this]()
			{
				return FAppStyle::GetBrush(IsLinked() ? "Graph.Pin.Connected" : "Graph.Pin.Disconnected");
			})
			.ColorAndOpacity_Lambda([this, Channel]()
			{
				FLinearColor Color = STexturePackerGraph::GetChannelColor(Channel);
				Color.A = IsAvailable() ? 1.0f : 0.2f;
				return FSlateColor(Color);
			});

		const TSharedRef<SWidget> Label = SNew(STextBlock)
			.Text(STexturePackerGraph::GetChannelName(Channel))
			.Font(FAppStyle::GetFontStyle("BoldFont"))
			.ColorAndOpacity_Lambda([this, Channel]()
			{
				FLinearColor Color = STexturePackerGraph::GetChannelColor(Channel);
				Color.A = IsAvailable() ? 1.0f : 0.25f;
				return FSlateColor(Color);
			});

		// Output pins sit on the left edge of their node (dot first), source pins on the right edge (dot last)
		TSharedRef<SHorizontalBox> Box = SNew(SHorizontalBox);
		if (Id.bOutput)
		{
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 6.0f, 0.0f)[Dot.ToSharedRef()];
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[Label];
		}
		else
		{
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center)[Label];
			Box->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(6.0f, 0.0f, 0.0f, 0.0f)[Dot.ToSharedRef()];
		}

		SetCursor(TAttribute<TOptional<EMouseCursor::Type>>::CreateLambda([this]()
		{
			return TOptional<EMouseCursor::Type>(IsAvailable() ? EMouseCursor::Crosshairs : EMouseCursor::Default);
		}));

		ChildSlot
		[
			SNew(SBorder)
			.BorderImage_Lambda([this]()
			{
				const TSharedPtr<STexturePackerGraph> G = Graph.Pin();
				const bool bHighlight = G && (G->IsDropTarget(Id) || (IsHovered() && IsAvailable()));
				return FAppStyle::GetBrush(bHighlight ? "Brushes.Hover" : "NoBorder");
			})
			.Padding(FMargin(4.0f, 2.0f))
			.ToolTipText_Lambda([this]()
			{
				const TSharedPtr<STexturePackerGraph> G = Graph.Pin();
				return G ? G->GetPinToolTip(Id) : FText::GetEmpty();
			})
			[
				Box
			]
		];
	}

	const FTexturePackerPinId& GetId() const { return Id; }

	/**
	 * Center of the dot, where wires attach. Paint space (window-relative) is for drawing next to the graph's
	 * paint geometry; tick space (desktop) is what mouse events use. Mixing the two puts the wire off by the
	 * window's position.
	 */
	FVector2f GetPaintAnchor() const
	{
		return Dot->GetPaintSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
	}
	FVector2f GetTickAnchor() const
	{
		return Dot->GetTickSpaceGeometry().GetAbsolutePositionAtCoordinates(FVector2f(0.5f, 0.5f));
	}

	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override
	{
		const TSharedPtr<STexturePackerGraph> G = Graph.Pin();
		return G ? G->OnPinMouseDown(Id, MouseEvent) : FReply::Unhandled();
	}

private:
	TWeakPtr<STexturePackerGraph> Graph;
	FTexturePackerPinId Id;
	TSharedPtr<SImage> Dot;

	bool IsAvailable() const
	{
		const TSharedPtr<STexturePackerGraph> G = Graph.Pin();
		return G && G->IsPinAvailable(Id);
	}

	bool IsLinked() const
	{
		const TSharedPtr<STexturePackerGraph> G = Graph.Pin();
		return G && G->IsPinLinked(Id);
	}
};

// LAYOUT
/** Source nodes in a column on the left, the gap for wires, then the output node. */
void STexturePackerGraph::Construct(const FArguments& InArgs)
{
	Settings = InArgs._Settings;
	SourceChannels = InArgs._SourceChannels;
	SourceNotes = InArgs._SourceNotes;
	CompressionText = InArgs._CompressionText;
	OnSourcesChanged = InArgs._OnSourcesChanged;
	OnChannelsChanged = InArgs._OnChannelsChanged;
	check(Settings && SourceChannels && SourceNotes);

	TSharedRef<SVerticalBox> SourceColumn = SNew(SVerticalBox);
	for (int32 i = 0; i < FTexturePackSettings::NumSources; ++i)
	{
		SourceColumn->AddSlot()
			.AutoHeight()
			.Padding(0.0f, i > 0 ? TexturePackerGraph::NodeGap : 0.0f, 0.0f, 0.0f)
			[
				CreateSourceNode(i)
			];
	}

	ChildSlot
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SourceColumn
		]
		// Wire room: at least WireGap, growing so the Output node sits at the right edge (next to the preview)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.MinWidth(TexturePackerGraph::WireGap)
		[
			SNew(SSpacer).Size(FVector2D(TexturePackerGraph::WireGap, 0.0f))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			CreateOutputNode()
		]
	];
}

/** Node look: the graph node body, a title bar tinted like the material editor's, then the content. */
TSharedRef<SWidget> STexturePackerGraph::MakeNode(const FText& Title, const TSharedRef<SWidget>& TitleExtra, const FLinearColor& TitleColor, const TSharedRef<SWidget>& Content)
{
	return SNew(SBorder)
	.BorderImage(FAppStyle::GetBrush("Graph.Node.Body"))
	.Padding(0.0f)
	[
		SNew(SVerticalBox)

		// Title bar
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Graph.Node.ColorSpill"))
				.ColorAndOpacity(TitleColor)
			]
			+ SOverlay::Slot()
			[
				SNew(SImage).Image(FAppStyle::GetBrush("Graph.Node.TitleGloss"))
			]
			+ SOverlay::Slot()
			.Padding(FMargin(10.0f, 5.0f, 10.0f, 5.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(Title)
					.Font(FAppStyle::GetFontStyle("BoldFont"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(12.0f, 0.0f, 0.0f, 0.0f)
				[
					TitleExtra
				]
			]
		]

		// Content
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(FMargin(4.0f, 6.0f, 4.0f, 8.0f))
		[
			Content
		]
	];
}

/** "Texture N" node: title with a badge of the channels it has, the texture picker, and a pin per channel. */
TSharedRef<SWidget> STexturePackerGraph::CreateSourceNode(int32 SourceIndex)
{
	TSharedRef<SVerticalBox> PinColumn = SNew(SVerticalBox);
	for (int32 Channel = 0; Channel < FTexturePackSettings::NumChannels; ++Channel)
	{
		PinColumn->AddSlot().AutoHeight().HAlign(HAlign_Right)[CreatePin({ false, SourceIndex, Channel })];
	}

	const TSharedRef<SWidget> Badge = SNew(SBox)
		.ToolTipText_Lambda([this, SourceIndex]() { return SourceNotes[SourceIndex]; })
		[
			VrrMenu::Badge(TAttribute<FText>::CreateLambda([this, SourceIndex]()
			{
				return TexturePacker::DescribeChannels(SourceChannels[SourceIndex]);
			}), VrrMenu::NeutralColor())
		];

	return MakeNode(FText::Format(LOCTEXT("SourceNodeTitle", "Texture {0}"), SourceIndex + 1), Badge, TexturePackerGraph::SourceTitleColor,
		SNew(SHorizontalBox)

		// Texture picker
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(TexturePackerGraph::PickerWidth)
			[
				SNew(SObjectPropertyEntryBox)
				.AllowedClass(UTexture2D::StaticClass())
				.ObjectPath_Lambda([this, SourceIndex]()
				{
					return Settings->Sources[SourceIndex].IsValid() ? Settings->Sources[SourceIndex]->GetPathName() : FString();
				})
				.OnObjectChanged_Lambda([this, SourceIndex](const FAssetData& AssetData)
				{
					Settings->Sources[SourceIndex] = Cast<UTexture2D>(AssetData.GetAsset());
					if (OnSourcesChanged)
					{
						OnSourcesChanged();
					}
				})
				.AllowClear(true)
				.DisplayThumbnail(true)
				.ThumbnailPool(UThumbnailManager::Get().GetSharedThumbnailPool())
			]
		]

		// Channel pins on the right edge
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(10.0f, 0.0f, 0.0f, 0.0f)
		[
			PinColumn
		]);
}

/** "Output" node: title with the compression, then one row per channel. */
TSharedRef<SWidget> STexturePackerGraph::CreateOutputNode()
{
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	for (int32 Channel = 0; Channel < FTexturePackSettings::NumChannels; ++Channel)
	{
		Rows->AddSlot().AutoHeight().Padding(0.0f, 1.0f)[CreateOutputRow(Channel)];
	}

	const TSharedRef<SWidget> Compression = SNew(STextBlock)
		.Text(CompressionText)
		.Font(VrrMenu::HelpFont())
		.ColorAndOpacity(FSlateColor::UseSubduedForeground());

	return MakeNode(LOCTEXT("OutputNodeTitle", "Output"), Compression, TexturePackerGraph::OutputTitleColor, Rows);
}

/** Output channel: the pin, then where its value comes from (the wired channel, or a constant when unwired), then Invert. */
TSharedRef<SWidget> STexturePackerGraph::CreateOutputRow(int32 ChannelIndex)
{
	FPackedChannel* Channel = &Settings->Channels[ChannelIndex];
	auto IsEnabled = [this, ChannelIndex]() { return Settings->IsChannelEnabled(ChannelIndex); };

	return SNew(SHorizontalBox)

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 6.0f, 0.0f)
	[
		CreatePin({ true, 0, ChannelIndex })
	]

	// Value: what the wire brings in, or the constant used without one
	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	.Padding(0.0f, 0.0f, 8.0f, 0.0f)
	[
		SNew(SBox)
		.WidthOverride(VrrMenu::NumberWidth)
		.IsEnabled_Lambda(IsEnabled)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Visibility_Lambda([Channel]() { return Channel->SourceIndex != INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
				.Text_Lambda([Channel]()
				{
					return FText::Format(LOCTEXT("WiredFrom", "Texture {0} . {1}"), Channel->SourceIndex + 1, GetChannelName(Channel->SourceChannel));
				})
			]
			+ SOverlay::Slot()
			[
				SNew(SSpinBox<float>)
				.Visibility_Lambda([Channel]() { return Channel->SourceIndex == INDEX_NONE ? EVisibility::Visible : EVisibility::Collapsed; })
				.MinValue(0.0f)
				.MaxValue(1.0f)
				.Delta(0.01f)
				.ToolTipText(LOCTEXT("DefaultValueTip", "Constant value used while no texture channel is wired in"))
				.Value_Lambda([Channel]() { return Channel->DefaultValue; })
				.OnValueChanged_Lambda([this, Channel](float NewValue)
				{
					Channel->DefaultValue = NewValue;
					if (OnChannelsChanged)
					{
						OnChannelsChanged();
					}
				})
			]
		]
	]

	+ SHorizontalBox::Slot()
	.AutoWidth()
	.VAlign(VAlign_Center)
	[
		SNew(SCheckBox)
		.IsEnabled_Lambda(IsEnabled)
		.ToolTipText(LOCTEXT("InvertTip", "Invert this channel (1 - value)"))
		.IsChecked_Lambda([Channel]() { return Channel->bInvert ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
		.OnCheckStateChanged_Lambda([this, Channel](ECheckBoxState State)
		{
			Channel->bInvert = State == ECheckBoxState::Checked;
			if (OnChannelsChanged)
			{
				OnChannelsChanged();
			}
		})
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Invert", "Invert"))
			.Font(VrrMenu::HelpFont())
		]
	];
}

TSharedRef<STexturePackerPin> STexturePackerGraph::CreatePin(const FTexturePackerPinId& Id)
{
	TSharedRef<STexturePackerPin> Pin = SNew(STexturePackerPin, SharedThis(this), Id);
	Pins.Add(Pin);
	return Pin;
}

// LABELS
FText STexturePackerGraph::GetChannelName(int32 Channel)
{
	static const FText Names[4] = { INVTEXT("R"), INVTEXT("G"), INVTEXT("B"), INVTEXT("A") };
	return Names[FMath::Clamp(Channel, 0, 3)];
}

FLinearColor STexturePackerGraph::GetChannelColor(int32 Channel)
{
	static const FLinearColor Colors[4] =
	{
		FLinearColor(1.0f, 0.25f, 0.25f),
		FLinearColor(0.3f, 1.0f, 0.3f),
		FLinearColor(0.35f, 0.55f, 1.0f),
		FLinearColor(0.85f, 0.85f, 0.85f)
	};
	return Colors[FMath::Clamp(Channel, 0, 3)];
}

// PIN STATE
/** Output pins: kept by the compression. Source pins: the slot has a texture with that channel. */
bool STexturePackerGraph::IsPinAvailable(const FTexturePackerPinId& Pin) const
{
	if (Pin.bOutput)
	{
		return Settings->IsChannelEnabled(Pin.Channel);
	}
	return Settings->Sources[Pin.Node].IsValid() && (SourceChannels[Pin.Node] & TexturePacker::ChannelBit(Pin.Channel)) != 0;
}

bool STexturePackerGraph::IsPinLinked(const FTexturePackerPinId& Pin) const
{
	if (Pin.bOutput)
	{
		return Settings->Channels[Pin.Channel].SourceIndex != INDEX_NONE;
	}
	for (const FPackedChannel& Channel : Settings->Channels)
	{
		if (Channel.SourceIndex == Pin.Node && Channel.SourceChannel == Pin.Channel)
		{
			return true;
		}
	}
	return false;
}

bool STexturePackerGraph::IsDropTarget(const FTexturePackerPinId& Pin) const
{
	return DropTarget.IsSet() && *DropTarget == Pin;
}

FText STexturePackerGraph::GetPinToolTip(const FTexturePackerPinId& Pin) const
{
	const FText Channel = GetChannelName(Pin.Channel);

	if (Pin.bOutput)
	{
		if (!Settings->IsChannelEnabled(Pin.Channel))
		{
			return FText::Format(LOCTEXT("OutputPinDisabled", "{0} compression doesn't use the {1} channel"), CompressionText.Get(), Channel);
		}
		const FPackedChannel& Packed = Settings->Channels[Pin.Channel];
		if (Packed.SourceIndex != INDEX_NONE)
		{
			return FText::Format(LOCTEXT("OutputPinLinked", "{0} comes from {1} of Texture {2}.\nAlt+click or right-click to break the wire."),
				Channel, GetChannelName(Packed.SourceChannel), Packed.SourceIndex + 1);
		}
		return FText::Format(LOCTEXT("OutputPinFree", "Drag to a texture channel to pack it into {0}.\nWithout a wire, {0} uses the constant value."), Channel);
	}

	if (!Settings->Sources[Pin.Node].IsValid())
	{
		return LOCTEXT("SourcePinEmpty", "Pick a texture first");
	}
	if (!IsPinAvailable(Pin))
	{
		const FText& Note = SourceNotes[Pin.Node];
		return Note.IsEmpty()
			? FText::Format(LOCTEXT("SourcePinMissing", "This texture has no {0} channel"), Channel)
			: FText::Format(LOCTEXT("SourcePinMissingWhy", "This texture has no {0} channel:\n{1}"), Channel, Note);
	}
	return LOCTEXT("SourcePinFree", "Drag to an output channel.\nAlt+click or right-click to break its wires.");
}

// DRAGGING
/** Alt+click or right-click breaks wires; a left press on an available pin starts a wire. */
FReply STexturePackerGraph::OnPinMouseDown(const FTexturePackerPinId& Pin, const FPointerEvent& MouseEvent)
{
	const FKey Button = MouseEvent.GetEffectingButton();
	if (Button == EKeys::RightMouseButton || (Button == EKeys::LeftMouseButton && MouseEvent.IsAltDown()))
	{
		BreakLinks(Pin);
		return FReply::Handled();
	}

	if (Button == EKeys::LeftMouseButton && IsPinAvailable(Pin))
	{
		DragFrom = Pin;
		DropTarget.Reset();
		DragPosition = GetTickSpaceGeometry().AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}

	return FReply::Unhandled();
}

FReply STexturePackerGraph::OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!DragFrom.IsSet())
	{
		return SCompoundWidget::OnMouseMove(MyGeometry, MouseEvent);
	}

	DragPosition = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
	DropTarget = FindDropTarget(MouseEvent.GetScreenSpacePosition());
	return FReply::Handled();
}

FReply STexturePackerGraph::OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (!DragFrom.IsSet() || MouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return SCompoundWidget::OnMouseButtonUp(MyGeometry, MouseEvent);
	}

	const TOptional<FTexturePackerPinId> Target = FindDropTarget(MouseEvent.GetScreenSpacePosition());
	if (Target.IsSet())
	{
		Connect(*DragFrom, *Target);
	}
	CancelDrag();
	return FReply::Handled().ReleaseMouseCapture();
}

void STexturePackerGraph::OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent)
{
	CancelDrag();
	SCompoundWidget::OnMouseCaptureLost(CaptureLostEvent);
}

void STexturePackerGraph::CancelDrag()
{
	DragFrom.Reset();
	DropTarget.Reset();
}

TSharedPtr<STexturePackerPin> STexturePackerGraph::FindPin(const FTexturePackerPinId& Id) const
{
	for (const TSharedRef<STexturePackerPin>& Pin : Pins)
	{
		if (Pin->GetId() == Id)
		{
			return Pin;
		}
	}
	return nullptr;
}

/** The pin under the cursor (or whose dot is within SnapDistance) that the dragged wire can connect to. */
TOptional<FTexturePackerPinId> STexturePackerGraph::FindDropTarget(const FVector2f& ScreenPosition) const
{
	if (!DragFrom.IsSet())
	{
		return {};
	}

	for (const TSharedRef<STexturePackerPin>& Pin : Pins)
	{
		if (!CanConnect(*DragFrom, Pin->GetId()))
		{
			continue;
		}

		// Tick space, like the mouse position
		const FGeometry& Geometry = Pin->GetTickSpaceGeometry();
		const float SnapDistance = TexturePackerGraph::SnapDistance * Geometry.Scale;
		if (Geometry.IsUnderLocation(ScreenPosition) || FVector2f::Distance(Pin->GetTickAnchor(), ScreenPosition) <= SnapDistance)
		{
			return Pin->GetId();
		}
	}
	return {};
}

/** Wires only go between a source pin and an output pin, both available. */
bool STexturePackerGraph::CanConnect(const FTexturePackerPinId& A, const FTexturePackerPinId& B) const
{
	return A.bOutput != B.bOutput && IsPinAvailable(A) && IsPinAvailable(B);
}

/** Wires the source pin into the output pin, replacing the output's old wire. */
void STexturePackerGraph::Connect(const FTexturePackerPinId& A, const FTexturePackerPinId& B)
{
	const FTexturePackerPinId& Source = A.bOutput ? B : A;
	const FTexturePackerPinId& Output = A.bOutput ? A : B;

	FPackedChannel& Channel = Settings->Channels[Output.Channel];
	Channel.SourceIndex = Source.Node;
	Channel.SourceChannel = Source.Channel;

	if (OnChannelsChanged)
	{
		OnChannelsChanged();
	}
}

/** Output pin: its wire. Source pin: every wire leaving it. Unwired outputs fall back to their constant. */
void STexturePackerGraph::BreakLinks(const FTexturePackerPinId& Pin)
{
	bool bChanged = false;
	for (int32 Out = 0; Out < FTexturePackSettings::NumChannels; ++Out)
	{
		FPackedChannel& Channel = Settings->Channels[Out];
		const bool bMatches = Pin.bOutput
			? Out == Pin.Channel
			: Channel.SourceIndex == Pin.Node && Channel.SourceChannel == Pin.Channel;
		if (bMatches && Channel.SourceIndex != INDEX_NONE)
		{
			Channel.SourceIndex = INDEX_NONE;
			bChanged = true;
		}
	}

	if (bChanged && OnChannelsChanged)
	{
		OnChannelsChanged();
	}
}

// PAINT
/** Nodes first, then the wires on top (they run through the gap, so they only overlap the pin dots). */
int32 STexturePackerGraph::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
	FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 WireLayer = SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId, InWidgetStyle, bParentEnabled) + 1;

	// Bezier from a source pin (leaving right) to an output pin (entering from the left), like graph wires
	// Points are in the graph's local space; PinPosition converts a pin's paint-space anchor
	auto PinPosition = [&](const STexturePackerPin& Pin) { return FVector2f(AllottedGeometry.AbsoluteToLocal(Pin.GetPaintAnchor())); };
	auto DrawWire = [&](const FVector2f& Start, const FVector2f& End, const FLinearColor& Color)
	{
		const float Tangent = FMath::Max(FMath::Abs(End.X - Start.X) * 0.5f, 30.0f);
		FSlateDrawElement::MakeCubicBezierSpline(OutDrawElements, WireLayer, AllottedGeometry.ToPaintGeometry(),
			Start, Start + FVector2f(Tangent, 0.0f), End - FVector2f(Tangent, 0.0f), End,
			TexturePackerGraph::WireThickness, ESlateDrawEffect::None, Color);
	};

	// Wires from the settings; faded when the compression drops that output channel
	for (int32 Out = 0; Out < FTexturePackSettings::NumChannels; ++Out)
	{
		const FPackedChannel& Channel = Settings->Channels[Out];
		if (Channel.SourceIndex == INDEX_NONE)
		{
			continue;
		}

		const TSharedPtr<STexturePackerPin> From = FindPin({ false, Channel.SourceIndex, Channel.SourceChannel });
		const TSharedPtr<STexturePackerPin> To = FindPin({ true, 0, Out });
		if (From && To)
		{
			FLinearColor Color = GetChannelColor(Channel.SourceChannel);
			Color.A = Settings->IsChannelEnabled(Out) ? 1.0f : 0.25f;
			DrawWire(PinPosition(*From), PinPosition(*To), Color);
		}
	}

	// Wire being dragged: to the cursor, or snapped to the pin it would connect to
	if (DragFrom.IsSet())
	{
		if (const TSharedPtr<STexturePackerPin> Pin = FindPin(*DragFrom))
		{
			const TSharedPtr<STexturePackerPin> Target = DropTarget.IsSet() ? FindPin(*DropTarget) : nullptr;
			const FVector2f PinEnd = PinPosition(*Pin);
			const FVector2f OtherEnd = Target ? PinPosition(*Target) : DragPosition;
			const FLinearColor Color = GetChannelColor(DragFrom->Channel);

			if (DragFrom->bOutput)
			{
				DrawWire(OtherEnd, PinEnd, Color);
			}
			else
			{
				DrawWire(PinEnd, OtherEnd, Color);
			}
		}
	}

	return WireLayer;
}

#undef LOCTEXT_NAMESPACE
