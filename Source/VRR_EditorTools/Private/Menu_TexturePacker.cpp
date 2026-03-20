// Fill out your copyright notice in the Description page of Project Settings.


#include "Menu_TexturePacker.h"

#include "PropertyCustomizationHelpers.h"
#include "ThumbnailRendering/ThumbnailManager.h"


Menu_TexturePacker::Menu_TexturePacker()
{
	Initialize();
}

Menu_TexturePacker::~Menu_TexturePacker()
{
}


void Menu_TexturePacker::Initialize()
{
	// Initialize Compression Options
	CompressionOptions.Add(MakeShared<FString>("Masks (Recommended)"));
	CompressionOptions.Add(MakeShared<FString>("Grayscale"));
	CompressionOptions.Add(MakeShared<FString>("Default"));
	CurrentCompressionOption = CompressionOptions[0];
	
	//Menu = LayoutMenu();
}

TSharedRef<SWidget> Menu_TexturePacker::LayoutMenu()
{
	// Initialize Texture Input Widgets
	TSharedRef<SWidget> TextureWidget_Diffuse		= CreateTextureInputWidget("Diffuse",			InputTexture_Diffuse,		"Select your Albedo/Diffuse Source");
	TSharedRef<SWidget> TextureWidget_Metallic		= CreateTextureInputWidget("Metallic",			InputTexture_Metallic,			"Select your Metallic Source"			);
	TSharedRef<SWidget> TextureWidget_Specular		= CreateTextureInputWidget("Specular",			InputTexture_Specular, 			"Select your Specular Source"			);
	TSharedRef<SWidget> TextureWidget_Roughness		= CreateTextureInputWidget("Roughness",			InputTexture_Roughness,			"Select your Roughness Source"			);
	TSharedRef<SWidget> TextureWidget_Normal		= CreateTextureInputWidget("Normal",			InputTexture_Normal,			"Select your Normal Source"				);
	TSharedRef<SWidget> TextureWidget_AO			= CreateTextureInputWidget("Ambient Occlusion",	InputTexture_AO,				"Select your Ambient Occlusion Source"	);
	TSharedRef<SWidget> TextureWidget_Displacement	= CreateTextureInputWidget("Displacement",		InputTexture_Displacement,		"Select your Displacement Source"		);
	TSharedRef<SWidget> TextureWidget_Edges			= CreateTextureInputWidget("Edges",				InputTexture_Edges,				"Select your Edges Source"				);
	TSharedRef<SWidget> TextureWidget_Cavity		= CreateTextureInputWidget("Cavity",			InputTexture_Cavity,			"Select your Cavity Source"				);
	
	TSharedRef<SWidget> MenuLayout = SNew(SScrollBox)
	+ SScrollBox::Slot()
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f)
		[
			TextureWidget_Diffuse
		]
	];
	
	return MenuLayout;
}

TSharedRef<SWidget> Menu_TexturePacker::CreateTextureInputWidget(const FString& WidgetLabel, TWeakObjectPtr<UTexture2D>& TextureInputPtr, const FString& Tooltip)
{
	TWeakObjectPtr<UTexture2D>* SelectedTexture = &TextureInputPtr;
	
	TSharedRef<SWidget> TextureInputWidget = SNew(SVerticalBox)
	
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(4.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(STextBlock)
		.Text(FText::FromString(WidgetLabel))
		.Font(FAppStyle::GetFontStyle("PropertyWindow.NormalFont"))
	]
		
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(4.0f, 0.0f, 0.0f, 4.0f)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture2D::StaticClass())
			.ObjectPath_Lambda([SelectedTexture]()
			{
				return SelectedTexture->IsValid() ? SelectedTexture->Get()->GetPathName() : FString();
			})
			.OnObjectChanged_Lambda([this, SelectedTexture](const FAssetData& AssetData)
			{
				*SelectedTexture = Cast<UTexture2D>(AssetData.GetAsset());
				AutoGenerateFileName();
			})
			.AllowClear(true)
			.DisplayThumbnail(true)
			.ThumbnailPool(UThumbnailManager::Get().GetSharedThumbnailPool())
		]

		+ SHorizontalBox::Slot()
		[
			SNew(SSeparator)
		]
	]
		
	// Separator
	+ SVerticalBox::Slot()
	.AutoHeight()
	.Padding(10.0f, 5.0f)
	[
		SNew(SSeparator)
	];
	
	TextureInputWidget->SetToolTipText(FText::FromString(Tooltip));
	
	return TextureInputWidget;
}

void Menu_TexturePacker::AutoGenerateFileName()
{
	if (bFileNameManuallyEdited)
	{
		return;
	}

	TArray<FString> InputNames;
	if (InputTexture_Diffuse.IsValid()) InputNames.Add(InputTexture_Diffuse->GetName());
	// if (InputTextureR.IsValid()) InputNames.Add(InputTextureR->GetName());
	// if (InputTextureG.IsValid()) InputNames.Add(InputTextureG->GetName());
	// if (InputTextureB.IsValid()) InputNames.Add(InputTextureB->GetName());
	// if (InputTextureA.IsValid()) InputNames.Add(InputTextureA->GetName());

	if (InputNames.Num() == 0)
	{
		return;
	}

	// Find Common Prefix
	FString CommonPrefix = InputNames[0];
	for (int32 i = 1; i < InputNames.Num(); ++i)
	{
		const FString& CurrentName = InputNames[i];
		int32 CommonLen = 0;
		int32 MaxLen = FMath::Min(CommonPrefix.Len(), CurrentName.Len());
		for (int32 CharIdx = 0; CharIdx < MaxLen; ++CharIdx)
		{
			if (CommonPrefix[CharIdx] == CurrentName[CharIdx])
			{
				CommonLen++;
			}
			else
			{
				break;
			}
		}
		CommonPrefix = CommonPrefix.Left(CommonLen);
	}

	FString BaseName;
	if (CommonPrefix.Len() >= 3)
	{
		BaseName = CommonPrefix;
	}
	else
	{
		BaseName = InputNames[0]; // First valid input
	}

	// Enforce "T_" prefix
	if (!BaseName.StartsWith(TEXT("T_")))
	{
		BaseName = TEXT("T_") + BaseName;
	}

	// Remove trailing underscores
	while (BaseName.EndsWith(TEXT("_")))
	{
		BaseName.LeftChopInline(1);
	}

	// check for number of channels
	OutputFileName = BaseName + TEXT("_ORM");
}



