// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Engine/Texture.h"

class UTexture2D;

struct FTextureData
{
	TArray<uint8> Data;
	
	FString TextureName;
	ETextureSourceFormat Format = TSF_Invalid; // TextureDefines.h
	int32 Width = 0;
	int32 Height = 0;
	
	bool bIsValid = false;
	
	FText ErrorMessage;
};

struct FChannelSelection
{
	bool bRedChannel	= false;
	bool bGreenChannel	= false;
	bool bBlueChannel	= false;
	bool bAlphaChannel	= false;
};

struct FTextureInput
{
	FString TextureType;
	TWeakObjectPtr<UTexture2D> TextureInput;
	FChannelSelection ChannelSelection;
};

class Menu_TexturePacker : SWidget
{
public:
	Menu_TexturePacker();
	~Menu_TexturePacker();
	
	//TSharedRef<SWidget> Menu;
	
	void Initialize();
	TSharedRef<SWidget> LayoutMenu();
protected:
	bool bFileNameManuallyEdited = false;
	
	// Texture Library to pick and choose channels from
	TWeakObjectPtr<UTexture2D> InputTexture_Diffuse;
	TWeakObjectPtr<UTexture2D> InputTexture_Metallic;
	TWeakObjectPtr<UTexture2D> InputTexture_Specular;
	TWeakObjectPtr<UTexture2D> InputTexture_Roughness;
	TWeakObjectPtr<UTexture2D> InputTexture_Opacity;
	TWeakObjectPtr<UTexture2D> InputTexture_Normal;
	TWeakObjectPtr<UTexture2D> InputTexture_AO;
	TWeakObjectPtr<UTexture2D> InputTexture_Displacement;
	TWeakObjectPtr<UTexture2D> InputTexture_Edges;
	TWeakObjectPtr<UTexture2D> InputTexture_Cavity;
	
	TArray<TSharedPtr<FString>> CompressionOptions;
	
	// Menu Creation
	
	TSharedRef<SWidget> CreateTextureInputWidget(const FString& WidgetLabel, TWeakObjectPtr<UTexture2D>& TextureInputPtr, const FString& Tooltip = "");
	
	private:
	// cache
	FString OutputFileName = "T_Packed_Texture";
	TSharedPtr<FString> CurrentCompressionOption;
	
	// helpers
	void AutoGenerateFileName();
};
