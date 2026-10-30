# UE 5.8 TechArt Toolbox

This is a growing collection of tools created from previous projects working with Unreal. I have migrated them from UE 4.26 and 5.2 Editor Utility Widgets into C++ for UE 5.8 with the assistance of Claude Code to maintain formatting and consistency. They are free to use under the MIT License (see [LICENSE](LICENSE)). New tools will be added sporadically as my projects deem their usefulness.

Editor only: nothing ships with your game. Supported engines: **5.7, 5.8**. Platforms: Windows, macOS, Linux (editor).

Open it from the **TechArt** menu in the main menu bar, from **Window > TechArt Toolbox**, or with the toolbar button next to Play. Each tool is a page in the window's sidebar.

## 1. Validate (Asset Validation)
<img width="1920" height="1080" alt="UE5_TAT_01_AssetValidation" src="https://github.com/user-attachments/assets/31ec74b5-213d-4a8f-8f54-a45f85f29c84" />

Checks a folder or the Content Browser selection against the project's naming and asset rules.
- Textures:
  - The suffix decides the expected settings (_D, _N, _ORM, _H…): sRGB, compression, size, mips and streaming.
  - Curve atlases (CA_), VATs (VAT_) and virtual textures (VT_) each get their own rules.
  - _H expects Displacementmap or Grayscale compression, depending on whether the height is stored in A or R.
- Meshes:
  - Prefixes: SM_, SK_, SKEL_, PHYS_.
  - Flags missing or badly named materials, no collision, dense meshes without LODs, bounds that suggest a unit-scale mistake, and a missing lightmap channel.
- Materials: M_ / MI_ / MF_ / ML_ prefixes, and the leftover _Inst suffix.
- Fix All applies the automatic fixes: renames and texture settings. For an unknown prefix, you choose whether to keep it or replace it.
- Export: writes the results to a JSON report.

## 2. Packer (Texture Packer)
<img width="1920" height="1080" alt="UE5_TAT_02_TexturePacker" src="https://github.com/user-attachments/assets/ea79984c-8323-436b-8599-421c898507da" />

- Combines channels from up to 4 textures into one RGBA output, e.g. an _ORM mask.
- You wire the channels in a material-graph-style node view.
- Each channel can be inverted, or set to a constant when no source is picked.
- Sources are read from their original import data, so values copy exactly.
- The live preview uses the same code as the final pack, so it matches the result.

## 3. Normals (Bake Smooth Normals)
<img width="1920" height="1080" alt="UE5_TAT_03_BakeSmooth" src="https://github.com/user-attachments/assets/ba342871-716f-4e54-ba72-61ef8da24ea9" />

- Bakes averaged normals so inverted-hull toon outlines don't split open at hard edges or UV seams.
- Vertex colors target: RGB holds the normal, A holds the thickness mask.
- Texture target: the same data baked into an uncompressed texture.
- The optional thickness mask makes outlines thinner on small features like noses and fingers.
- Normals can be stored in tangent space (follows skinning) or object space (static meshes only).

## 4. UV Layouts
<img width="1920" height="1080" alt="UE5_TAT_04_UVLayout" src="https://github.com/user-attachments/assets/ac4e5c98-c0a9-4c26-abd9-e4551ec3a92f" />

- Builds a new, non-overlapping UV channel (usually UV1) from a mesh's existing islands.
- All islands get the same texel density, with optional rotation for the tightest fit.
- Each material slot gets its own region of the UV square, or its own UDIM tile. A material can read these region numbers to address each region.
- Packing options: boxes on shelves, or shape packing where islands nest into each other's gaps. Optional stretch to fill.
- Balanced layout: one region per island. On static meshes it can be written as the lightmap UVs.
- Texel Density mode: rescales an existing channel in place to a target px/m, without repacking.
- If the lightmap already uses the target channel, it is moved up one channel automatically.

## 5. Rebake (Rebake Texture Map to UV Set)
<img width="1920" height="1080" alt="UE5_TAT_05_RebakeTexture" src="https://github.com/user-attachments/assets/6d84a348-80b7-46ba-8187-90c7f26da5b0" />

- Moves a texture from one UV channel to another, e.g. from an overlapping UV0 to the unique UV1 built by UV Layouts.
- Uses supersampling for clean island edges.
- Normal maps are re-oriented, so rotated or mirrored islands still shade correctly.
- Pads the islands and fills empty space so mips stay clean.
- Shows the source and the result side by side.

## 6. Consolidate (Consolidate Materials Texture Sets)
<img width="1920" height="1080" alt="UE5_TAT_06_Consolidate" src="https://github.com/user-attachments/assets/4ce8e2bc-ae9c-4994-9395-44e5640e24f3" />

- Bakes every material on a multi-material mesh into a single texture set through one UV channel (_D, _N, _R…).
- It renders each material's final output, not just copies of its textures.
- Reports places where two slots overlap in UV space.
- Skips outputs that are one value everywhere, like no metallic or no emissive.
- Split by Shading Model gives each shading model its own set, e.g. DefaultLit and PreintegratedSkin.

## 7. Mesh Maps (Bake Mesh Maps)
<img width="1920" height="1080" alt="UE5_TAT_07_BakeMeshMaps" src="https://github.com/user-attachments/assets/4be633ea-7604-4df1-a056-9d75f378fb22" />

- Bakes Curvature, Cavity, Edge and AO from the mesh's own geometry. No high-poly source is needed.
- Output is either a packed _CCE / _CCEA mask or one grayscale texture per map.
- The mesh is welded by position first, so hard edges and UV seams don't break the results.
- Edge line width doesn't depend on vertex density.
- AO is ray-traced, run in parallel, and then smoothed.
- Also supports Split by Shading Model.

## 8. Edges (Edge Mesh)
<img width="1920" height="1080" alt="UE5_TAT_08_EdgeMesh" src="https://github.com/user-attachments/assets/a2281cbc-f955-47ca-8b21-46e10403efdd" />

- Builds thin strips along a static mesh's sharp outer edges, as a separate mesh for edge-wear decal materials.
- Controls: Min Angle, convex only, Max Turn Angle (corners end a strip), Min Length, Width and Surface Offset.
- Each strip gets its own UVs: U runs along the edge, V across it. That lets one tiling decal work on any mesh at any size.

## 9. Rig (Control Rig Generator)
<img width="1920" height="1080" alt="UE5_TAT_09_CRSetup" src="https://github.com/user-attachments/assets/8b8cb833-a03a-4ee1-8f12-fb6e097c6640" />

- Generates a UE 5.8 Modular Control Rig for a humanoid skeletal mesh, with no hand wiring.
- Bones are mapped from their names. Optionally, a Blender .rig.json overrides the mapping (see [Blender rig exporter](#blender-rig-exporter) below).
- The page shows the bone map so you can check it before generating.
- Creates the modules (Root, Spine, Neck, Shoulder, Arm, Finger, Leg, Foot), plus a Chain module for each face or extra bone.
- Writes a report of what got connected and what's still open.

## 10. VAT (Vertex Animation Textures)
<img width="1920" height="1080" alt="UE5_TAT_10_VATBaker" src="https://github.com/user-attachments/assets/209ebad8-fc05-44fa-8e4e-8e08ec65c776" />

- Sets up and runs Unreal's AnimToTexture plugin for every LOD, in Bone mode.
- You pick a skeletal mesh and the animations to bake. There's a seamless-loop option.
- It creates:
  - one static mesh and one Data Asset per LOD;
  - the textures;
  - MI_<Name> with all baked parameters filled in, plus a child instance per LOD;
  - a merged SM_<Name> with LOD screen sizes and bounds that cover the animation.
- Optionally places a preview actor.
- Re-baking updates the existing instances and the preview actor instead of replacing them.
- The master material is yours to pick (see [VAT master material](#vat-master-material) below). A default master
  and the preview actor's class can be set in Project Settings.

## Project Settings

**Project Settings > Plugins > TechArt Toolbox** (saved to `DefaultEditor.ini`, so a team shares them). All optional:

| Setting | Used for |
|---|---|
| Default VAT Master Material | The VAT page starts with it, and its **Default** button picks it. |
| Preview Actor Class | The actor placed to preview a bake. Empty: a plain Static Mesh Actor. |
| Preview Actor Mesh Variable | Static mesh variable the baked mesh goes into, when the preview actor isn't a Static Mesh Actor. |
| Preview Selection Component | Component added to the preview actor to switch animations, when the master has a `VAT Selection` scalar. |
| Preview Selection Component Variable | Name variable on that component that is set to `VAT Selection`. |

## VAT master material

The VAT page fills an instance of any master material that has these parameters (names must match):

- Required: `Rows Per Frame`, `Sample Rate`, `Atlas - MinBBox`, `Atlas - SizeBBox`, `Anim0 - Start Frame`,
  `Anim0 - End Frame`, `Number of Bones`, `Atlas - Bone Position`, `Atlas - Bone Rotation`, `Atlas - Bone Weights`.
- Optional: `Anim1..N - Start Frame` / `End Frame` (one pair per extra animation), `VAT Selection`, `Play Rate`,
  `Time Offset`, `VAT Height Offset`, `VAT Rotation Offset`, `VAT UV Set Selection`, the static switches `UseUV0`-`UseUV3`,
  `Use Two Influences` and `Use Four Influences`.

In Bone mode, `Rows Per Frame` receives the bone **weight** rows per frame (per LOD). The page's note lists any required
parameter the picked master is missing.

## Blender rig exporter

`Extras/Blender/vrr_rig_exporter.py` writes a `<name>.rig.json` description of a hand-built Deform + Control rig, which
the Rig page reads to map limbs, fingers and extra controls. Install it in Blender as an add-on (Edit > Preferences >
Add-ons > Install from Disk; it appears in the 3D view sidebar's **VRR** tab), or run it headless:

```
blender -b file.blend --python vrr_rig_exporter.py -- [--deform RIG_DEFORM] [--control RIG_CONTROL] [--out path.json]
```

Put the .rig.json next to the skeletal mesh (or one folder up) and the Rig page picks it up.

## Installation

Copy the `TechArtToolbox` folder into your project's `Plugins` folder (or install from Fab), then enable
**TechArt Toolbox** in Edit > Plugins and restart the editor. It turns on the engine's AnimToTexture, Control Rig and
Control Rig Modules plugins, which the VAT and Rig pages use.

## Support

Report issues at https://github.com/VR-Robotica/TechArtToolbox/issues.

Copyright (c) 2026 VR Robotica. Licensed under the MIT License.
