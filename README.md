# 3dbase — Mobile3D Studio

Native-first 3D content creation application for mobile devices, designed around a Blender-class workflow without copying a desktop UI onto a phone.

## Technology direction

- **C++20** for the application/core and performance-sensitive systems.
- **Qt Quick / QML** for the mobile editor UI (next implementation stage).
- **Vulkan** for the 3D viewport and renderer (after the editor shell is established).
- **Android NDK + CMake** for Android-native integration.
- No Kotlin application layer is planned.

## Current implemented foundation

The repository currently contains the first production foundation rather than a visual prototype:

- versioned project manifest;
- persistent scene document;
- stable 128-bit object IDs;
- scene hierarchy with cycle-safe parenting;
- typed scene objects and transforms;
- subtree deletion/restoration;
- command-based Undo/Redo with save checkpoints/dirty tracking;
- create/delete/rename/transform/reparent commands;
- composite transactions for multi-object edits;
- single/multi-selection model with active-object tracking;
- atomic project/scene saves;
- independent crash-recovery autosave;
- dependency-free C++ tests.

## Build the core tests

```bash
cmake -S . -B build -G Ninja -DMOBILE3D_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

No APK is produced at this stage. Android packaging is intentionally deferred until the editor has a useful, testable vertical slice.

## Roadmap

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for the staged implementation plan and [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) for architectural rules that future features must follow.
