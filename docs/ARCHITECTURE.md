# Mobile3D Architecture

## Non-negotiable rules

1. **Core is platform independent.** Scene, commands, serialization, geometry and document logic must compile without Qt, Android or Vulkan.
2. **UI never owns scene truth.** QML will issue editor commands and observe models; it will not become the data model.
3. **Renderer never owns authoring data.** Vulkan consumes evaluated render data produced from the scene.
4. **Importers do not write directly into the live scene.** They will decode into an intermediate scene, validate it, then commit through editor/document services.
5. **Every destructive editor action is command-backed.** This keeps Undo/Redo deterministic.
6. **File formats are versioned.** Readers may migrate older versions; writers emit the current version.
7. **Mobile failure modes are first-class.** Atomic writes and recovery autosaves exist before the Android UI.
8. **No Kotlin application architecture.** Android-specific needs will be handled through the NDK/Qt Android integration unless a hard platform limitation is discovered and documented.

## Layering

```text
Qt Quick / QML Editor UI        (future)
          |
Editor services / Commands
          |
Scene + Project document        (implemented foundation)
          |
Geometry / Animation / Physics  (future)
          |
Evaluated Scene                 (future)
          |
Vulkan Renderer                 (future)
```

Platform services sit beside the core rather than inside it:

```text
Android NDK services -> platform interfaces <- desktop test host
```

## Current core

### Object identity

`ObjectId` is a randomly generated 128-bit ID serialized as 32 hexadecimal characters. Names are presentation data and can change without breaking references.

### Scene hierarchy

`SceneObject` currently stores the authoring properties required by the foundation: type, name, local transform, parent, visibility and lock state. Parenting rejects missing parents, self-parenting and descendant cycles.

The hierarchy intentionally stores only the parent on each object. Child lists are derived in the foundation. A later performance stage may add indexed adjacency caches without changing document semantics.

### Commands

All editor mutations intended for user interaction are represented by `EditorCommand` subclasses and executed through `CommandStack`.

Implemented commands:

- Create Object
- Delete Object (including subtree snapshot/restore)
- Rename Object
- Transform Object
- Reparent Object

The next editor layer should never bypass these for interactive operations. Multi-object edits can be grouped into `CompositeCommand`, producing one deterministic Undo/Redo step. `CommandStack` also tracks save checkpoints without confusing an undone branch with a newly edited branch.

### Selection

`SelectionModel` owns ordered selected IDs plus the active object. It supports replace/add/toggle semantics and can prune IDs removed by scene edits. Selection is intentionally editor state and is not serialized into the authored scene.

### Persistence

`project.m3dproj` points at a versioned scene file. Scene and manifest writes use temporary files followed by replacement, reducing the chance of a partially written document after process termination.

The current serialization format is deliberately small and dependency-free. It is an internal foundation format, not the future interchange format. glTF/GLB and other interchange formats belong to the import/export pipeline, not to the editor's internal scene identity model.

### Autosave

Autosave is stored independently under `autosave/`. Saving an autosave does not overwrite the user's primary scene. The application shell will later compare timestamps/session markers and offer recovery after abnormal termination.

## Planned next boundaries

### Editor application shell

Qt/QML will provide:

- workspace frame;
- viewport host;
- Outliner model;
- Inspector model;
- top command bar;
- bottom context area;
- touch-first responsive panels.

### Render interface

Before Vulkan code enters the scene module, a render-facing snapshot/API will be introduced. This avoids making `SceneObject` carry Vulkan handles or GPU resources.

### Geometry

Mesh data will be a dedicated asset/resource referenced by scene objects. Geometry editing will not be embedded directly inside `SceneObject`.
