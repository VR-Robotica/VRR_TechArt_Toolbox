// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Fonts/SlateFontInfo.h"
#include "Framework/SlateDelegates.h"
#include "Widgets/SWidget.h"

/**
 * Widgets the Toolbox pages share, so every page looks and behaves the same.
 *
 * A page is a Stack of Cards. Each Card holds Rows: a fixed-width label column on the left and the field on the
 * right, so fields line up across every card and page. Help text and notes use one dimmed style, and each page
 * ends with an ActionButton (primary style, says why it's disabled) and a StatusLine.
 */
namespace VrrMenu
{
	// LAYOUT METRICS (shared by every page, so spacing matches everywhere)

	/** Width of the label column in Row(). */
	inline constexpr float LabelWidth = 160.0f;
	/** Vertical padding above and below each row in Rows(). */
	inline constexpr float RowPadding = 3.0f;
	/** Gap between cards in Stack(). */
	inline constexpr float CardGap = 12.0f;
	/** Width of number fields. */
	inline constexpr float NumberWidth = 90.0f;
	/** Width of text boxes, folder pickers and asset pickers. */
	inline constexpr float WideWidth = 360.0f;

	// TYPE SCALE

	/** Page title in the window header. */
	FSlateFontInfo TitleFont();
	/** Card titles. */
	FSlateFontInfo SectionFont();
	/** Help text and notes (shown dimmed). */
	FSlateFontInfo HelpFont();

	// STRUCTURE

	/** Cards stacked top to bottom with a fixed gap. The usual root of a page. */
	TSharedRef<SWidget> Stack(const TArray<TSharedRef<SWidget>>& Cards);

	/** Rounded panel with a title. HeaderExtra (optional) sits at the right end of the title row. */
	TSharedRef<SWidget> Card(const FText& Title, const TSharedRef<SWidget>& Content, const TSharedPtr<SWidget>& HeaderExtra = nullptr);

	/**
	 * Card with help text hidden behind a "?" button in the title row, so long explanations don't push the
	 * fields down. Clicking the button shows or hides the help above the content.
	 */
	TSharedRef<SWidget> HelpCard(const FText& Title, const TAttribute<FText>& Help, const TSharedRef<SWidget>& Content, const TSharedPtr<SWidget>& HeaderExtra = nullptr);

	/** Widgets stacked top to bottom with row spacing. Collapsed widgets take no space. */
	TSharedRef<SWidget> Rows(const TArray<TSharedRef<SWidget>>& Widgets);

	/** Label in the label column, Content to its right (left-aligned, at its own size). */
	TSharedRef<SWidget> Row(const FText& Label, const TSharedRef<SWidget>& Content, const FText& ToolTip = FText::GetEmpty(), const TAttribute<bool>& IsEnabled = true);

	/** Section title (the same font as card titles), for sub-headings inside a card. */
	TSharedRef<SWidget> Header(const FText& Text);

	/** Dimmed, wrapping explanation text spanning the card. */
	TSharedRef<SWidget> HelpText(const TAttribute<FText>& Text);

	/** Dimmed, wrapping text lined up with the field column (a remark about the row above). */
	TSharedRef<SWidget> Note(const TAttribute<FText>& Text);

	/** Small rounded pill: Text in Color over a faint fill of the same color. Hidden while the text is empty. */
	TSharedRef<SWidget> Badge(const TAttribute<FText>& Text, const FLinearColor& Color);

	/** Badge colors: neutral, error (red), warning (yellow), success (green). */
	FLinearColor NeutralColor();
	FLinearColor ErrorColor();
	FLinearColor WarningColor();
	FLinearColor SuccessColor();

	// FIELDS

	/** Checkbox that reads and writes *bValue directly. */
	TSharedRef<SWidget> CheckBox(bool* bValue, const FText& Text, const FText& ToolTip);

	/** Float spin box bound to *Value. */
	TSharedRef<SWidget> FloatField(float* Value, float Min, float Max, float Delta);

	/** Int spin box bound to *Value. */
	TSharedRef<SWidget> IntField(int32* Value, int32 Min, int32 Max);

	/** Text box for an asset name bound to *Name. Commits trimmed text; an empty box keeps the old name. */
	TSharedRef<SWidget> NameField(FString* Name);

	/**
	 * Content folder text box plus a button that takes the folder open in the Content Browser.
	 * Typed paths are cleaned up (see VrrCore::NormalizeContentFolder); OnError gets a message when the
	 * Content Browser shows no real folder.
	 */
	TSharedRef<SWidget> FolderPicker(FString* Folder, const FString& DefaultFolder, TFunction<void(const FText&)> OnError, float Width = WideWidth);

	// ACTIONS

	/**
	 * A page's main button, in the editor's primary (blue) style.
	 * GetBlocker returns why the action can't run yet ("Pick a mesh first."), or empty when it can.
	 * While blocked the button is disabled and the reason shows next to it.
	 */
	TSharedRef<SWidget> ActionButton(const FText& Text, const FText& ToolTip, TFunction<FText()> GetBlocker, FOnClicked OnClicked);

	/** Result of the last action under the main button: a check or error icon, and red text for errors. Hidden while empty. */
	TSharedRef<SWidget> StatusLine(const FText* Text, const bool* bError);

	enum class ENotify : uint8 { Success, Warning, Error };

	/** Toast in the corner of the editor. */
	void Notify(const FText& Text, ENotify Type, float Duration = 5.0f);
}
