// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_TexturePacker.h"
#include "Widgets/SCompoundWidget.h"

class STexturePackerPin;

/** One pin of the packer graph: a channel of a source texture node, or a channel of the output node. */
struct FTexturePackerPinId
{
	bool bOutput = false;
	/** Source slot (0-3); unused for output pins. */
	int32 Node = 0;
	/** 0 = R, 1 = G, 2 = B, 3 = A. */
	int32 Channel = 0;

	bool operator==(const FTexturePackerPinId& Other) const
	{
		return bOutput == Other.bOutput && Node == Other.Node && Channel == Other.Channel;
	}
};

/**
 * Texture Packer graph, laid out like a material graph: a node per source texture with a pin for each channel
 * it has, and an Output node with R, G, B and A pins. Drag a wire between a source pin and an output pin to
 * pack that channel; Alt+click or right-click a pin to break its wires. Unwired outputs use their constant.
 * Edits the page's FTexturePackSettings directly and reports changes through the callbacks.
 */
class STexturePackerGraph : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STexturePackerGraph) {}
		/** Settings being edited (owned by the page). */
		SLATE_ARGUMENT(FTexturePackSettings*, Settings)
		/** Per source slot: which channels it has (TexturePacker::ChannelBit flags). */
		SLATE_ARGUMENT(const uint8*, SourceChannels)
		/** Per source slot: why channels are missing (tooltip). */
		SLATE_ARGUMENT(const FText*, SourceNotes)
		/** Display name of the output compression, for disabled-channel tooltips. */
		SLATE_ATTRIBUTE(FText, CompressionText)
		/** A source slot got a new texture (or was cleared). */
		SLATE_ARGUMENT(TFunction<void()>, OnSourcesChanged)
		/** Wires, constants or invert changed. */
		SLATE_ARGUMENT(TFunction<void()>, OnChannelsChanged)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Used by the pins
	bool IsPinAvailable(const FTexturePackerPinId& Pin) const;
	bool IsPinLinked(const FTexturePackerPinId& Pin) const;
	bool IsDropTarget(const FTexturePackerPinId& Pin) const;
	FText GetPinToolTip(const FTexturePackerPinId& Pin) const;
	FReply OnPinMouseDown(const FTexturePackerPinId& Pin, const FPointerEvent& MouseEvent);

	static FText GetChannelName(int32 Channel);
	static FLinearColor GetChannelColor(int32 Channel);

	// SWidget
	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;
	virtual FReply OnMouseMove(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual FReply OnMouseButtonUp(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	virtual void OnMouseCaptureLost(const FCaptureLostEvent& CaptureLostEvent) override;

private:
	FTexturePackSettings* Settings = nullptr;
	const uint8* SourceChannels = nullptr;
	const FText* SourceNotes = nullptr;
	TAttribute<FText> CompressionText;
	TFunction<void()> OnSourcesChanged;
	TFunction<void()> OnChannelsChanged;

	TArray<TSharedRef<STexturePackerPin>> Pins;

	// Wire being dragged
	TOptional<FTexturePackerPinId> DragFrom;
	TOptional<FTexturePackerPinId> DropTarget;
	FVector2f DragPosition = FVector2f::ZeroVector;	// cursor, in the graph's local space

	/** Material-graph style node: body with a tinted title bar (Title plus TitleExtra) above Content. */
	static TSharedRef<SWidget> MakeNode(const FText& Title, const TSharedRef<SWidget>& TitleExtra, const FLinearColor& TitleColor, const TSharedRef<SWidget>& Content);
	TSharedRef<SWidget> CreateSourceNode(int32 SourceIndex);
	TSharedRef<SWidget> CreateOutputNode();
	TSharedRef<SWidget> CreateOutputRow(int32 Channel);
	TSharedRef<STexturePackerPin> CreatePin(const FTexturePackerPinId& Id);

	TSharedPtr<STexturePackerPin> FindPin(const FTexturePackerPinId& Id) const;
	/** Available pin under a screen position that a wire from DragFrom may connect to. */
	TOptional<FTexturePackerPinId> FindDropTarget(const FVector2f& ScreenPosition) const;
	bool CanConnect(const FTexturePackerPinId& A, const FTexturePackerPinId& B) const;
	void Connect(const FTexturePackerPinId& A, const FTexturePackerPinId& B);
	void BreakLinks(const FTexturePackerPinId& Pin);
	void CancelDrag();
};
