# Mobile3D Studio — Execution Roadmap

This roadmap is the implementation contract for the repository. Stages are intentionally ordered so advanced features are not built on unstable editor foundations.

## Stage 0 — Project and document foundation

**Status: complete.**

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

**Status: complete.**

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

**Status: implemented and validated for the current no-APK milestone. Physical-device validation remains intentionally deferred.**

- [x] Qt 6.8 application target compiled from C++/QML with strict warnings.
- [x] Host runtime QML smoke test.
- [x] `qmllint` validation with zero warnings.
- [x] Android Qt/NDK arm64-v8a cross-build with Qt 6.8.3 + NDK r27c.
- [x] No project-owned Kotlin/Java application layer.
- [x] Landscape/portrait responsive editor frame.
- [x] Workspace manager backed by editor state.
- [x] Outliner backed by the real Scene document.
- [x] Inspector backed by active selection and command-based property edits.
- [x] Command toolbar wired to Undo/Redo.
- [x] Project create/open/recent flows.
- [x] Autosave scheduler, lifecycle autosave and crash-recovery prompt.
- [x] Touch-safe panel resizing/collapsing and compact drawers.
- [x] `EditorSession` boundary prevents QML from mutating Scene directly.
- [ ] Physical Android lifecycle/on-device validation. Deferred until the project reaches the agreed APK milestone.

The editor shell is no longer considered source-only: CI compiles it, loads QML at runtime and cross-builds the native Android arm64 target. The remaining physical-device item specifically requires an installable package and is therefore deferred by project policy rather than treated as an architectural blocker.

## Stage 3 — Vulkan viewport foundation

**Status: in progress. The native Vulkan path, render-data boundary and camera/navigation foundation are implemented and CI-validated; actual scene geometry rendering is next.**

- [x] Immutable `RenderSceneSnapshot` boundary; renderer never receives live `Scene`/selection pointers.
- [x] Android requests Vulkan before the first `QQuickWindow` is created.
- [x] Reuse of Qt Quick's Vulkan instance/device/surface/render-pass lifecycle rather than creating a conflicting second surface.
- [x] Native Vulkan commands recorded inside the Qt Quick render pass.
- [x] Native command state isolated with `beginExternalCommands()` / `endExternalCommands()`.
- [x] Viewport-native command recording clipped to the viewport rectangle.
- [x] Real Vulkan runtime smoke test using software Vulkan/Lavapipe; success requires at least one native Vulkan command-recorded frame.
- [x] Android arm64 cross-build of the Vulkan viewport path.
- [x] Scene-graph invalidation cleanup boundary for renderer-owned resources.
- [x] Perspective viewport camera.
- [x] Orthographic viewport camera.
- [x] Vulkan-compatible projection matrices (0..1 depth range and inverted framebuffer Y convention).
- [x] Orbit camera.
- [x] Pan camera.
- [x] Zoom camera.
- [x] One-finger orbit input.
- [x] Two-finger pan + pinch zoom input.
- [x] Host mouse/wheel navigation.
- [x] Perspective/Orthographic toggle and Reset View controls.
- [ ] Physical Android Vulkan lifecycle/suspend-resume validation. Deferred with APK/device testing.
- [ ] Dedicated GPU upload/memory allocator abstraction for authored geometry.
- [ ] Vulkan pipeline cache persisted across sessions/devices where valid.
- [ ] Depth buffer policy for custom 3D drawing.
- [ ] MSAA policy and device quality fallback.
- [ ] Grid.
- [ ] XYZ axes.
- [ ] Basic unlit authored mesh rendering.
- [ ] Object ID picking.
- [ ] Selection outline.

The next Stage 3 implementation order is intentionally:

1. shader/pipeline resource module;
2. grid + XYZ axes;
3. geometry upload boundary and unlit mesh path;
4. depth/MSAA policy;
5. ID picking;
6. selection outline;
7. Android physical lifecycle validation when APK testing is allowed.

Exit criterion: the real authored scene is visible/selectable in a stable Vulkan viewport across Android lifecycle changes.

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

An APK is not a milestone by itself. Packaging should happen when there is a coherent on-device vertical slice to test. Until then, host builds/tests validate the platform-independent foundation, Qt/QML runtime smoke tests validate the editor shell, Vulkan/Lavapipe smoke tests validate native command recording, and Android arm64 cross-builds validate the native target without producing an APK.
