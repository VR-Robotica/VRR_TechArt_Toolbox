// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Core_AssetValidation.h"

/** Which material checks to run. */
struct FMaterialValidatorSettings
{
	bool bCheckNaming = true;		// M_ material, MI_ instance, MF_ or ML_ function, PPM_ post process material, PPMI_ its instance; clean name after the prefix
	bool bCheckInstSuffix = true;	// instances don't end with _Inst (the name Unreal gives a new instance)
};

/**
 * Checks Material, Material Instance and Material Function assets against the project's naming rules.
 * Post process materials (and instances whose root parent is one) get their own prefixes, PPM_ and PPMI_.
 * Checking works from the Asset Registry alone (nothing is loaded), so it's fast even on big folders.
 * Naming issues carry the fixed name (M_Rock_Inst -> MI_Rock) for Fix All (AssetValidation::FixIssues).
 * AssetValidation (Core_AssetValidation.h) finds the materials and runs this on each.
 */
namespace MaterialValidator
{
	/** "M_ (Material), MI_ (Material Instance), MF_ or ML_ (Material Function), PPM_ (Post Process Material), PPMI_ (...)" for tooltips. */
	FString GetPrefixList();

	/** Whether the asset is a material, material instance or material function (including layers and function instances). */
	bool IsMaterialAsset(const FAssetData& Asset);

	/** Checks one material asset, adding what it finds to OutIssues. */
	void ValidateMaterial(const FAssetData& Asset, const FMaterialValidatorSettings& Settings, TArray<FAssetIssue>& OutIssues);
}
