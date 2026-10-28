# UE 5.8 TechArt Toolbox

1. Validate (Asset Validation)

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

2. Packer (Texture Packer)

- Combines channels from up to 4 textures into one RGBA output, e.g. an _ORM mask.
- You wire the channels in a material-graph-style node view.
- Each channel can be inverted, or set to a constant when no source is picked.
- Sources are read from their original import data, so values copy exactly.
- The live preview uses the same code as the final pack, so it matches the result.

3. Normals (Bake Smooth Normals)

- Bakes averaged normals so inverted-hull toon outlines don't split open at hard edges or UV seams.
- Vertex colors target: RGB holds the normal, A holds the thickness mask.
- Texture target: the same data baked into an uncompressed texture.
- The optional thickness mask makes outlines thinner on small features like noses and fingers.
- Normals can be stored in tangent space (follows skinning) or object space (static meshes only).

4. UV Layouts

- Builds a new, non-overlapping UV channel (usually UV1) from a mesh's existing islands.
- All islands get the same texel density, with optional rotation for the tightest fit.
- Each material slot gets its own region of the UV square, or its own UDIM tile. The Vrr UV Region material node reads these region numbers.
- Packing options: boxes on shelves, or shape packing where islands nest into each other's gaps. Optional stretch to fill.
- Balanced layout: one region per island. On static meshes it can be written as the lightmap UVs.
- Texel Density mode: rescales an existing channel in place to a target px/m, without repacking.
- If the lightmap already uses the target channel, it is moved up one channel automatically.

5. Rebake (Rebake Texture Map to UV Set)

- Moves a texture from one UV channel to another, e.g. from an overlapping UV0 to the unique UV1 built by UV Layouts.
- Uses supersampling for clean island edges.
- Normal maps are re-oriented, so rotated or mirrored islands still shade correctly.
- Pads the islands and fills empty space so mips stay clean.
- Shows the source and the result side by side.

6. Consolidate (Consolidate Materials Texture Sets)

- Bakes every material on a multi-material mesh into a single texture set through one UV channel (_D, _N, _R…).
- It renders each material's final output, not just copies of its textures.
- Reports places where two slots overlap in UV space.
- Skips outputs that are one value everywhere, like no metallic or no emissive.
- Split by Shading Model gives each shading model its own set, e.g. DefaultLit and PreintegratedSkin.

7. Mesh Maps (Bake Mesh Maps)

- Bakes Curvature, Cavity, Edge and AO from the mesh's own geometry. No high-poly source is needed.
- Output is either a packed _CCE / _CCEA mask or one grayscale texture per map.
- The mesh is welded by position first, so hard edges and UV seams don't break the results.
- Edge line width doesn't depend on vertex density.
- AO is ray-traced, run in parallel, and then smoothed.
- Also supports Split by Shading Model.

8. Edges (Edge Mesh)

- Builds thin strips along a static mesh's sharp outer edges, as a separate mesh for edge-wear decal materials.
- Controls: Min Angle, convex only, Max Turn Angle (corners end a strip), Min Length, Width and Surface Offset.
- Each strip gets its own UVs: U runs along the edge, V across it. That lets one tiling decal work on any mesh at any size.

9. Rig (Control Rig Generator)

- Generates a UE 5.8 Modular Control Rig for a humanoid skeletal mesh, with no hand wiring.
- Bones are mapped from their names. Optionally, a Blender .rig.json (from vrr_rig_exporter.py) overrides the mapping.
- The page shows the bone map so you can check it before generating.
- Creates the modules (Root, Spine, Neck, Shoulder, Arm, Finger, Leg, Foot), plus a Chain module for each face or extra bone.
- Writes a report of what got connected and what's still open.

10. VAT (Vertex Animation Textures)

- Sets up and runs Unreal's AnimToTexture plugin for every LOD, in Bone or Vertex mode.
- You pick a skeletal mesh and the animations to bake. There's a seamless-loop option.
- It creates:
  - one static mesh and one Data Asset per LOD;
  - the textures;
  - MI_<Name> with all baked parameters filled in, plus a child instance per LOD;
  - a merged SM_<Name> with LOD screen sizes and bounds that cover the animation.
- Optionally places a preview actor.
- Re-baking updates the existing instances and the preview actor instead of replacing them.
