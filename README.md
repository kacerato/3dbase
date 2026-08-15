# 3dbase — Mobile3D Studio

Native-first 3D content creation application for mobile devices, designed around a Blender-class workflow without copying a desktop UI onto a phone.

## Technology direction

- **C++20** for the application/core and performance-sensitive systems.
- **Qt Quick / QML** for the touch-first mobile editor UI.
- **Vulkan** for the 3D viewport and renderer after the editor shell is validated.
- **Android NDK + CMake/Qt CMake** for Android-native integration.
- No project-owned Kotlin application layer.

## Implemented foundation

The repository currently contains production-oriented foundations rather than a disconnected visual prototype:

- versioned project manifest and persistent scene document;
- stable 128-bit object IDs;
- scene hierarchy with cycle-safe parenting;
- typed scene objects and transforms;
- subtree deletion/restoration;
- command-based Undo/Redo with save checkpoints/dirty tracking;
- create/delete/rename/transform/reparent commands;
- composite transactions for multi-object edits;
- single/multi-selection model with active-object tracking;
- atomic project/scene saves and independent crash-recovery autosave;
- `EditorSession` as the single UI-facing mutation boundary;
- workspace state, project lifecycle and recovery handling;
- Qt/QML editor-shell source with responsive desktop/mobile layouts;
- real Scene-backed Outliner and selection-backed Inspector adapters;
- project create/open/recent flows and periodic/lifecycle autosave;
- dependency-free host C++ tests.

## Build the Core + Editor tests

```bash
cmake -S . -B build -G Ninja \
  -DMOBILE3D_BUILD_TESTS=ON \
  -DMOBILE3D_BUILD_QT_APP=OFF \
  -DMOBILE3D_WARNINGS_AS_ERRORS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

## Qt editor-shell target

The QML application is optional so missing Qt/Android SDKs never break the platform-independent foundation:

```bash
<Qt 6.8+>/bin/qt-cmake -S . -B build-qt \
  -DMOBILE3D_BUILD_QT_APP=ON \
  -DMOBILE3D_BUILD_TESTS=ON
cmake --build build-qt --parallel
```

See [`docs/ANDROID_BUILD.md`](docs/ANDROID_BUILD.md) for the Android boundary and configuration.

The Qt shell source is present, but Stage 2 is not considered complete until that target is compiled with a real Qt SDK and then validated with an Android Qt/NDK kit. The Vulkan viewport remains intentionally unimplemented until that validation is done.

No APK is produced at this stage. Packaging is deferred until there is a coherent on-device 3D vertical slice worth testing.

## Roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for the staged implementation plan and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for architectural rules that future features must follow.
