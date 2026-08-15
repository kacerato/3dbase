# Mobile3D Studio — Execution Roadmap

This roadmap is the implementation contract for the repository. Stages are intentionally ordered so advanced features are not built on unstable editor foundations.

## Stage 0 — Project and document foundation

**Status: implemented in the first foundation commit.**

- [x] CMake C++20 project with strict warnings.
- [x] Platform-independent `mobile3d_core` library.
- [x] Stable 128-bit object identity.
- [x] Versioned project manifest.
- [x] Standard project directories.
- [x] Scene document serialization/deserialization.
- [x] Atomic primary saves.
- [x] Independent autosave/recovery file.
- [x] Host-side automated tests.

Exit criterion: a project can be created, saved, closed, reopened and recovered without any UI dependency.

## Stage 1 — Scene and editor command foundation

**Status: implemented in the first foundation commit.**

- [x] Object types and local transform.
- [x] Parent/child hierarchy.
- [x] Cycle-safe reparenting.
- [x] Subtree delete/restore.
- [x] Command stack.
- [x] Undo/Redo.
- [x] Create/Delete/Rename/Transform/Reparent commands.
- [x] Selection model.
- [x] Multi-selection.
- [x] Editor transaction/composite command support.
- [x] Dirty-state tracking and save checkpoints.

Exit criterion: all basic scene edits can be represented as deterministic commands and reversed without UI-specific state.

## Stage 2 — Native mobile editor shell (Qt Quick/QML)

- [ ] Qt 6 application target built from C++/QML.
- [ ] Android NDK toolchain configuration without Kotlin application code.
- [ ] Landscape/portrait responsive editor frame.
- [ ] Workspace manager.
- [ ] Outliner backed by the real Scene document.
- [ ] Inspector backed by selection.
- [ ] Command toolbar wired to Undo/Redo.
- [ ] Project create/open/recent flows.
- [ ] Autosave scheduler and crash-recovery prompt.
- [ ] Touch-safe panel resizing/collapsing.

Exit criterion: a user can create a project and manipulate scene hierarchy/properties on-device even before 3D rendering is connected.

## Stage 3 — Vulkan viewport foundation

- [ ] Vulkan instance/device/surface lifecycle.
- [ ] Android surface integration through Qt/NDK boundary.
- [ ] Swapchain recreation and suspend/resume handling.
- [ ] GPU memory allocator abstraction.
- [ ] Command pools/buffers and frame synchronization.
- [ ] Pipeline cache.
- [ ] Depth buffer and MSAA policy.
- [ ] Basic unlit mesh rendering.
- [ ] Grid and XYZ axes.
- [ ] Perspective and orthographic camera.
- [ ] Touch orbit/pan/zoom.
- [ ] Object ID picking.
- [ ] Selection outline.

Exit criterion: the real scene is visible/selectable in a stable Vulkan viewport across Android lifecycle changes.

## Stage 4 — Transform and scene editing vertical slice

- [ ] Move/Rotate/Scale gizmos.
- [ ] Local/global orientation.
- [ ] Axis/plane constraints.
- [ ] Pivot modes.
- [ ] Grid/increment snapping.
- [ ] Object duplication and deletion.
- [ ] Visibility/lock toggles.
- [ ] Collections and layers.
- [ ] Multi-object transform.
- [ ] Touch gesture conflict resolver.

At this point the first APK may become useful enough for internal testing, but packaging remains optional until requested.

## Stage 5 — Geometry resource and Edit Mode

- [ ] Mesh asset/resource model separated from SceneObject.
- [ ] Vertex/half-edge(or equivalent topology) representation.
- [ ] Vertex/Edge/Face selection.
- [ ] Mesh edit transaction system.
- [ ] Extrude.
- [ ] Inset.
- [ ] Loop cut.
- [ ] Bevel.
- [ ] Knife.
- [ ] Merge/Weld.
- [ ] Subdivide.
- [ ] Fill/Grid Fill.
- [ ] Bridge loops.
- [ ] Normals tools.

Exit criterion: useful polygon modeling can be completed entirely on mobile.

## Stage 6 — Primitive and modifier system

- [ ] Parameterized primitives.
- [ ] Non-destructive modifier stack.
- [ ] Mirror.
- [ ] Subdivision Surface.
- [ ] Boolean.
- [ ] Bevel modifier.
- [ ] Solidify.
- [ ] Array.
- [ ] Decimate.
- [ ] Weld.
- [ ] Triangulate.
- [ ] Weighted normals.
- [ ] Modifier apply/copy/reorder/toggle.

## Stage 7 — Asset import/export pipeline

- [ ] IntermediateScene representation.
- [ ] Background import jobs.
- [ ] Validation before live-scene commit.
- [ ] GLB/glTF first-class import/export.
- [ ] OBJ.
- [ ] STL.
- [ ] PLY.
- [ ] DAE.
- [ ] FBX import.
- [ ] Image import and texture transcoding policy.
- [ ] Import diagnostics instead of silent loss.

## Stage 8 — PBR materials and shading

- [ ] Material resources and slots.
- [ ] Metallic/roughness PBR.
- [ ] Base color, normal, AO, emission and opacity textures.
- [ ] Environment/HDRI.
- [ ] Material preview mode.
- [ ] Shader graph data model.
- [ ] Initial material nodes.

## Stage 9 — UV and texture painting

- [ ] UV editor workspace.
- [ ] Seam tools.
- [ ] Unwrap projections.
- [ ] Island transform/pack.
- [ ] Texture paint brushes.
- [ ] Channel painting.
- [ ] Layers/masks.
- [ ] Stylus pressure integration where available.

## Stage 10 — Sculpting

- [ ] Sculpt workspace.
- [ ] Brush engine.
- [ ] Draw/Clay/Smooth/Grab/Crease/Inflate.
- [ ] Symmetry.
- [ ] Masking.
- [ ] Voxel remesh.
- [ ] Dynamic topology strategy appropriate for mobile.

## Stage 11 — Animation and rigging

- [ ] Timeline and keyframes.
- [ ] Dope sheet.
- [ ] Graph editor.
- [ ] Animation clips/NLA-like layer.
- [ ] Armatures and bones.
- [ ] IK/FK constraints.
- [ ] Skinning.
- [ ] Automatic weights.
- [ ] Weight paint.
- [ ] Shape keys/morph targets.

## Stage 12 — Curves and procedural geometry

- [ ] Bézier/Poly/NURBS-style curve resources.
- [ ] Curve editing.
- [ ] Bevel/extrusion along curves.
- [ ] Road/pipe/cable/fence generators.
- [ ] Geometry graph runtime.
- [ ] Geometry node editor.
- [ ] Instances and realization.

## Stage 13 — Physics and simulation

- [ ] Rigid bodies.
- [ ] Collision shapes.
- [ ] Constraints.
- [ ] Cloth.
- [ ] Soft body.
- [ ] Particle system.
- [ ] GPU particle path where appropriate.

## Stage 14 — Production renderer

- [ ] Forward+/clustered lighting.
- [ ] Directional/point/spot/area lights.
- [ ] Cascaded shadows.
- [ ] IBL.
- [ ] SSAO.
- [ ] Bloom.
- [ ] Tone mapping/color grading.
- [ ] Fog.
- [ ] Render-to-image.
- [ ] Render animation.
- [ ] Device quality tiers and memory budget manager.

## Stage 15 — Scripting and plugin API

- [ ] Stable editor API boundary.
- [ ] Embedded Python for editor automation/add-ons.
- [ ] Lua for lightweight scripts/tools.
- [ ] Sandboxing/permissions.
- [ ] Plugin manifest.
- [ ] Custom tool registration.
- [ ] Custom nodes.
- [ ] Importer/exporter plugins.
- [ ] Brush/modifier extension points.

## Stage 16 — Large scenes and advanced interchange

- [ ] Asset browser.
- [ ] Instancing and linked assets.
- [ ] Scene streaming.
- [ ] Proxy/LOD workflow.
- [ ] OpenUSD/USDZ pipeline.
- [ ] Background thumbnail generation.
- [ ] Memory pressure handling.

## Stage 17 — Optional production tools

Only after the 3D authoring stack is mature:

- [ ] Node compositor.
- [ ] Video sequencer/editor.
- [ ] Advanced render passes.
- [ ] Batch processing/export presets.

## APK policy for this repository

An APK is not a milestone by itself. Packaging should happen when there is a coherent on-device vertical slice to test. Until then, host builds/tests validate the platform-independent foundation and Android work is kept modular.
