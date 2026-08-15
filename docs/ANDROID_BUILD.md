# Android / Qt build boundary

The application layer is authored in **C++20 + Qt Quick/QML**. There is no project-owned Kotlin or Java source tree.

Qt for Android still uses its own Android runtime bridge and packaging templates internally. That framework implementation is not part of the Mobile3D application architecture and should not be replaced with app-specific Kotlin unless a future Android API requirement cannot be reached through Qt or JNI from C++.

## Current target

`mobile3d_studio` is created only when:

```text
-DMOBILE3D_BUILD_QT_APP=ON
```

The platform-independent Core and Editor libraries remain buildable without Qt.

## Host editor-shell build

With a Qt 6.8+ desktop installation:

```bash
<qt-host>/bin/qt-cmake -S . -B build-qt \
  -DMOBILE3D_BUILD_QT_APP=ON \
  -DMOBILE3D_BUILD_TESTS=ON
cmake --build build-qt --parallel
ctest --test-dir build-qt --output-on-failure
```

## Android configuration

Use the `qt-cmake` wrapper from the installed Qt for Android ABI and the SDK/NDK versions supported by that Qt installation:

```bash
<Qt>/android_arm64_v8a/bin/qt-cmake \
  -S . -B build-android \
  -GNinja \
  -DANDROID_SDK_ROOT=<Android SDK> \
  -DANDROID_NDK_ROOT=<Android NDK> \
  -DMOBILE3D_BUILD_QT_APP=ON \
  -DMOBILE3D_BUILD_TESTS=OFF

cmake --build build-android --target mobile3d_studio
```

This configures and compiles the native target. The repository intentionally does **not** make APK generation part of the current milestone. Packaging will be enabled when the editor has a useful 3D vertical slice.

## Android rules

- Android UI logic must stay in QML/C++.
- Scene/document state must stay in `mobile3d_core` / `mobile3d_editor`.
- Android lifecycle events may request autosave, but may not mutate scene state directly.
- No Vulkan handles belong in QML or Scene objects.
- No custom Kotlin application layer is permitted by default.
- If a platform API eventually requires JNI, wrap it behind a C++ platform-service interface and document the exception.
