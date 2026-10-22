// Fill out your copyright notice in the Description page of Project Settings.

/*
 * Shared Slate widgets for the Toolbox pages (Menu_*.cpp), so every page looks and behaves the same.
 * Widgets that edit a value take a pointer to it (usually a member of the page or of its settings struct)
 * and read/write it through lambdas, so the widget always shows the current value.
 * Brushes (card, logo, tabs) live in FVRR_EditorToolsStyle.
 */

#include "MenuWidgets.h"

#include "Core_Common.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Styling/StyleColors.h"
#include "Widgets/SNullWidget.h"
#include "VRR_EditorToolsStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "VrrMenu"

namespace VrrMenu
{
	// TYPE SCALE
	FSlateFontInfo TitleFont()		{ return FCoreStyle::GetDefaultFontStyle("Bold", 15); }
	FSlateFontInfo SectionFont()	{ return FCoreStyle::GetDefaultFontStyle("Bold", 11); }
	FSlateFontInfo HelpFont()		{ return FCoreStyle::GetDefaultFontStyle("Regular", 9); }

	// STRUCTURE
	/** Each card in its own slot, CardGap between them (none after the last, so stacks can nest). */
	TSharedRef<SWidget> Stack(const TArray<TSharedRef<SWidget>>& Cards)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (int32 Index = 0; Index < Cards.Num(); ++Index)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, Index == 0 ? 0.0f : CardGap, 0.0f, 0.0f)[ Cards[Index] ];
		}
		return Box;
	}

	/** Rounded panel: title row (plus optional extra widget on the right), then the content. */
	TSharedRef<SWidget> Card(const FText& Title, const TSharedRef<SWidget>& Content, const TSharedPtr<SWidget>& HeaderExtra)
	{
		return SNew(SBorder)
			.BorderImage(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Card"))
			.Padding(FMargin(14.0f, 10.0f, 14.0f, 12.0f))
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(Title)
						.Font(SectionFont())
						.ColorAndOpacity(FStyleColors::ForegroundHeader)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						HeaderExtra.IsValid() ? HeaderExtra.ToSharedRef() : SNullWidget::NullWidget
					]
				]

				+ SVerticalBox::Slot().AutoHeight()
				[
					Content
				]
			];
	}

	/** Card whose title row ends with a "?" toggle; the help text sits above the content while it's on. */
	TSharedRef<SWidget> HelpCard(const FText& Title, const TAttribute<FText>& Help, const TSharedRef<SWidget>& Content, const TSharedPtr<SWidget>& HeaderExtra)
	{
		// Shared so the button and the text both outlive this function
		TSharedRef<bool> bShowHelp = MakeShared<bool>(false);

		TSharedRef<SWidget> HelpButton = SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ContentPadding(FMargin(2.0f))
			.ToolTipText_Lambda([bShowHelp]() { return *bShowHelp ? LOCTEXT("HideHelp", "Hide help") : LOCTEXT("ShowHelp", "Show help"); })
			.OnClicked_Lambda([bShowHelp]() { *bShowHelp = !*bShowHelp; return FReply::Handled(); })
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.Help"))
				.ColorAndOpacity_Lambda([bShowHelp]() { return *bShowHelp ? FSlateColor(FStyleColors::Primary) : FSlateColor::UseSubduedForeground(); })
			];

		TSharedRef<SHorizontalBox> Extra = SNew(SHorizontalBox);
		if (HeaderExtra.IsValid())
		{
			Extra->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)[ HeaderExtra.ToSharedRef() ];
		}
		Extra->AddSlot().AutoWidth().VAlign(VAlign_Center)[ HelpButton ];

		return Card(Title,
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([bShowHelp]() { return *bShowHelp ? EVisibility::Visible : EVisibility::Collapsed; })
				[
					HelpText(Help)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				Content
			],
			Extra);
	}

	/** One slot per widget; SVerticalBox skips collapsed slots (and their padding). */
	TSharedRef<SWidget> Rows(const TArray<TSharedRef<SWidget>>& Widgets)
	{
		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
		for (const TSharedRef<SWidget>& Widget : Widgets)
		{
			Box->AddSlot().AutoHeight().Padding(0.0f, RowPadding)[ Widget ];
		}
		return Box;
	}

	/** Fixed-width label column, then the field at its own size. */
	TSharedRef<SWidget> Row(const FText& Label, const TSharedRef<SWidget>& Content, const FText& ToolTip, const TAttribute<bool>& IsEnabled)
	{
		return SNew(SHorizontalBox)
			.IsEnabled(IsEnabled)
			.ToolTipText(ToolTip)

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(LabelWidth)
				[
					SNew(STextBlock).Text(Label)
				]
			]

			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center).HAlign(HAlign_Left)
			[
				Content
			];
	}

	/** Sub-heading in the card title font. */
	TSharedRef<SWidget> Header(const FText& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(SectionFont())
			.ColorAndOpacity(FStyleColors::ForegroundHeader);
	}

	/** Small dimmed text that wraps to the card width. */
	TSharedRef<SWidget> HelpText(const TAttribute<FText>& Text)
	{
		return SNew(STextBlock)
			.Text(Text)
			.Font(HelpFont())
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true);
	}

	/** HelpText indented past the label column; collapses while the text is empty. */
	TSharedRef<SWidget> Note(const TAttribute<FText>& Text)
	{
		return SNew(SBox)
			.Padding(FMargin(LabelWidth, 0.0f, 0.0f, 0.0f))
			.Visibility_Lambda([Text]() { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				HelpText(Text)
			];
	}

	/** Rounded pill tinted with Color; collapses while the text is empty. */
	TSharedRef<SWidget> Badge(const TAttribute<FText>& Text, const FLinearColor& Color)
	{
		return SNew(SBorder)
			.BorderImage(FVRR_EditorToolsStyle::Get().GetBrush("VrrTools.Badge"))
			.BorderBackgroundColor(Color.CopyWithNewOpacity(0.18f))
			.Padding(FMargin(8.0f, 1.0f, 8.0f, 2.0f))
			.Visibility_Lambda([Text]() { return Text.Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
				.ColorAndOpacity(Color)
			];
	}

	FLinearColor NeutralColor()	{ return FLinearColor(0.75f, 0.75f, 0.75f); }
	FLinearColor ErrorColor()	{ return FLinearColor(0.95f, 0.30f, 0.27f); }
	FLinearColor WarningColor()	{ return FLinearColor(1.0f, 0.73f, 0.20f); }
	FLinearColor SuccessColor()	{ return FLinearColor(0.35f, 0.82f, 0.40f); }

	// FIELDS
	/** Checkbox that reads and writes *bValue directly. */
	TSharedRef<SWidget> CheckBox(bool* bValue, const FText& Text, const FText& ToolTip)
	{
		return SNew(SCheckBox)
			.ToolTipText(ToolTip)
			.IsChecked_Lambda([bValue]() { return *bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([bValue](ECheckBoxState State) { *bValue = State == ECheckBoxState::Checked; })
			[
				SNew(STextBlock).Text(Text)
			];
	}

	/** Spin box at the standard number width. */
	TSharedRef<SWidget> FloatField(float* Value, float Min, float Max, float Delta)
	{
		return SNew(SBox)
			.WidthOverride(NumberWidth)
			[
				SNew(SSpinBox<float>)
				.MinValue(Min)
				.MaxValue(Max)
				.Delta(Delta)
				.Value_Lambda([Value]() { return *Value; })
				.OnValueChanged_Lambda([Value](float NewValue) { *Value = NewValue; })
			];
	}

	/** Spin box at the standard number width. */
	TSharedRef<SWidget> IntField(int32* Value, int32 Min, int32 Max)
	{
		return SNew(SBox)
			.WidthOverride(NumberWidth)
			[
				SNew(SSpinBox<int32>)
				.MinValue(Min)
				.MaxValue(Max)
				.Value_Lambda([Value]() { return *Value; })
				.OnValueChanged_Lambda([Value](int32 NewValue) { *Value = NewValue; })
			];
	}

	/** Asset name box; an empty commit keeps the previous name. */
	TSharedRef<SWidget> NameField(FString* Name)
	{
		return SNew(SBox)
			.WidthOverride(WideWidth)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([Name]() { return FText::FromString(*Name); })
				.OnTextCommitted_Lambda([Name](const FText& NewText, ETextCommit::Type)
				{
					const FString Trimmed = NewText.ToString().TrimStartAndEnd();
					if (!Trimmed.IsEmpty())
					{
						*Name = Trimmed;
					}
				})
			];
	}

	/** Text box for a content folder, plus a button that copies the Content Browser's current folder into it. */
	TSharedRef<SWidget> FolderPicker(FString* Folder, const FString& DefaultFolder, TFunction<void(const FText&)> OnError, float Width)
	{
		return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot().AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(SEditableTextBox)
				.ToolTipText(LOCTEXT("FolderTip", "Project content folder. /Game is the project's Content folder."))
				.Text_Lambda([Folder]() { return FText::FromString(*Folder); })
				// Typed paths are cleaned up ("Game/X/" -> "/Game/X"); an empty box falls back to DefaultFolder
				.OnTextCommitted_Lambda([Folder, DefaultFolder](const FText& NewText, ETextCommit::Type)
				{
					*Folder = VrrCore::NormalizeContentFolder(NewText.ToString(), DefaultFolder);
				})
			]
		]
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(4.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "SimpleButton")
			.ToolTipText(LOCTEXT("UseBrowserFolderTip", "Use the folder currently open in the Content Browser"))
			.ContentPadding(FMargin(2.0f))
			.OnClicked_Lambda([Folder, OnError]()
			{
				// Virtual folders ("All", collections) have no content path: report it instead of clearing the box
				const FString BrowserFolder = VrrCore::GetContentBrowserFolder();
				if (BrowserFolder.IsEmpty())
				{
					if (OnError)
					{
						OnError(LOCTEXT("NoBrowserFolder", "Open a content folder in the Content Browser first."));
					}
				}
				else
				{
					*Folder = BrowserFolder;
				}
				return FReply::Handled();
			})
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush("Icons.FolderOpen"))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
	}

	// ACTIONS
	/** Primary button; while GetBlocker returns text, the button is disabled and the text explains why. */
	TSharedRef<SWidget> ActionButton(const FText& Text, const FText& ToolTip, TFunction<FText()> GetBlocker, FOnClicked OnClicked)
	{
		auto IsBlocked = [GetBlocker]() { return GetBlocker && !GetBlocker().IsEmpty(); };

		return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), "PrimaryButton")
			.ContentPadding(FMargin(18.0f, 5.0f))
			.ToolTipText(ToolTip)
			.IsEnabled_Lambda([IsBlocked]() { return !IsBlocked(); })
			.OnClicked(OnClicked)
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
				.Justification(ETextJustify::Center)
			]
		]

		// Why the button is disabled
		+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(12.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(SImage)
			.Image(FAppStyle::GetBrush("Icons.Info"))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Visibility_Lambda([IsBlocked]() { return IsBlocked() ? EVisibility::Visible : EVisibility::Collapsed; })
		]
		+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Text_Lambda([GetBlocker]() { return GetBlocker ? GetBlocker() : FText::GetEmpty(); })
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.AutoWrapText(true)
		];
	}

	/** Icon + wrapping text for *Text, red with an error icon while *bError is set. Pages update both after each action. */
	TSharedRef<SWidget> StatusLine(const FText* Text, const bool* bError)
	{
		return SNew(SHorizontalBox)
			.Visibility_Lambda([Text]() { return Text->IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })

			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Top).Padding(0.0f, 1.0f, 8.0f, 0.0f)
			[
				SNew(SImage)
				.Image_Lambda([bError]() { return FAppStyle::GetBrush(*bError ? "Icons.ErrorWithColor" : "Icons.SuccessWithColor"); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([Text]() { return *Text; })
				.ColorAndOpacity_Lambda([bError]() { return *bError ? FSlateColor(FStyleColors::Error) : FSlateColor::UseForeground(); })
				.AutoWrapText(true)
			];
	}

	/** Toast in the editor corner with a success, warning or error icon. */
	void Notify(const FText& Text, ENotify Type, float Duration)
	{
		FNotificationInfo Info(Text);
		Info.ExpireDuration = Duration;
		Info.Image = FAppStyle::GetBrush(Type == ENotify::Success ? TEXT("Icons.SuccessWithColor")
			: Type == ENotify::Warning ? TEXT("Icons.WarningWithColor") : TEXT("Icons.ErrorWithColor"));
		FSlateNotificationManager::Get().AddNotification(Info);
	}
}

#undef LOCTEXT_NAMESPACE
