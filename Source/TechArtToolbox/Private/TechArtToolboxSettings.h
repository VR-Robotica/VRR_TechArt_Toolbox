// Copyright (c) 2026 VR Robotica. Licensed under the MIT License (see LICENSE).

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "TechArtToolboxSettings.generated.h"

class AActor;
class UActorComponent;
class UMaterialInterface;

/**
 * Project Settings > Plugins > TechArt Toolbox. Saved to the project's DefaultEditor.ini so a team shares them.
 * Everything here is optional: the tools work with every setting left empty.
 */
UCLASS(config = Editor, defaultconfig, meta = (DisplayName = "TechArt Toolbox"))
class UTechArtToolboxSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UTechArtToolboxSettings& Get() { return *GetDefault<UTechArtToolboxSettings>(); }

	/** VAT page: the master material it starts with and its Default button picks. Empty: pick one on the page each time. */
	UPROPERTY(config, EditAnywhere, Category = "Vertex Animation", meta = (DisplayName = "Default VAT Master Material"))
	TSoftObjectPtr<UMaterialInterface> DefaultVatMaterial;

	/**
	 * VAT page: the actor class placed to preview a bake. Empty: a plain Static Mesh Actor.
	 * The baked mesh goes into a Static Mesh Actor's mesh component, or else into the variable named below.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Vertex Animation", meta = (DisplayName = "Preview Actor Class"))
	TSoftClassPtr<AActor> VatPreviewActorClass;

	/** Static mesh variable the baked mesh is written to, when the preview actor isn't a Static Mesh Actor. */
	UPROPERTY(config, EditAnywhere, Category = "Vertex Animation", meta = (DisplayName = "Preview Actor Mesh Variable"))
	FName VatPreviewMeshVariable = TEXT("Static Mesh Asset");

	/**
	 * Optional component added to the preview actor when the master material has a "VAT Selection" scalar, so the
	 * animation can be switched on the placed actor. Its name variable (below) is set to "VAT Selection".
	 */
	UPROPERTY(config, EditAnywhere, Category = "Vertex Animation", meta = (DisplayName = "Preview Selection Component"))
	TSoftClassPtr<UActorComponent> VatPreviewSelectionComponent;

	/** Name variable on the selection component that gets the material parameter's name. */
	UPROPERTY(config, EditAnywhere, Category = "Vertex Animation", meta = (DisplayName = "Preview Selection Component Variable"))
	FName VatPreviewSelectionVariable = TEXT("Parameter");
};
