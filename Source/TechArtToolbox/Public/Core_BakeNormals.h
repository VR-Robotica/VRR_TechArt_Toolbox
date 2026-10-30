// Copyright VR Robotica. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/** Space the smooth normal is stored in. */
enum class ESmoothNormalSpace : uint8
{
	Tangent,	// Relative to each vertex's tangent basis. Follows skinning, works on any mesh.
	Object		// Mesh local space. Static meshes only.
};

/** Where the baked normal goes. */
enum class ESmoothNormalTarget : uint8 { VertexColor, Texture };

struct FSmoothNormalBakeSettings
{
	// Smoothing
	float WeldDistance = 0.01f;
	bool bAllLODs = true;

	// Thickness mask (alpha): thin outlines where the surface curves tightly (faces, small details)
	bool bBakeThicknessMask = false;
	float MaskFullRadius = 4.0f;	// curvature radius (cm) at and above which the outline keeps full width
	float MaskMinimum = 0.15f;		// width multiplier on the tightest curves

	// Encoding
	ESmoothNormalSpace Space = ESmoothNormalSpace::Tangent;
	bool bLockTangents = true;

	// Output
	ESmoothNormalTarget Target = ESmoothNormalTarget::VertexColor;
	bool bKeepAlpha = true;			// vertex colors: keep the existing alpha (ignored with the mask on)
	int32 UVChannel = 0;			// texture only
	int32 Resolution = 2048;		// texture only
	int32 Padding = 4;				// texture only, pixels
	FString OutputFolder;			// texture only
	FString OutputName;				// texture only
};

struct FSmoothNormalBakeResult
{
	bool bSuccess = false;
	/** What was baked plus any warnings, or the error. */
	FText Message;
	/** The new texture (texture bakes only). */
	UTexture2D* Texture = nullptr;
};

/**
 * Bakes fully smooth normals for a Static or Skeletal Mesh, for toon outlines (inverted hull).
 * Normals are averaged across every vertex at the same position (ignoring hard edges and UV seams),
 * so pushing vertices along them never splits the hull open.
 * The mesh's own shading normals are not changed; the result goes into vertex colors or a texture.
 */
namespace SmoothNormalBaker
{
	/** Skeletal meshes: their flag. Static meshes: any LOD 0 vertex color that isn't the default white. */
	bool HasVertexColors(UObject* Mesh);

	/** T_<mesh name without SM_/SK_>_SmoothN */
	FString SuggestTextureName(const UObject* Mesh);

	/** Vertex color bakes are undoable. Texture bakes create a new asset (a free name in the output folder). */
	FSmoothNormalBakeResult Bake(UObject* Mesh, const FSmoothNormalBakeSettings& Settings);
}
