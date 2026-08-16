add_library(mobile3d_core STATIC
    src/id.cpp
    src/mesh_resource.cpp
    src/editable_mesh.cpp
    src/scene.cpp
    src/command_stack.cpp
    src/composite_command.cpp
    src/selection_model.cpp
    src/commands/object_commands.cpp
    src/commands/organization_commands.cpp
    src/project_manifest.cpp
    src/scene_serializer.cpp
    src/project_repository.cpp
)

add_library(Mobile3D::Core ALIAS mobile3d_core)

target_include_directories(mobile3d_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(mobile3d_core PUBLIC cxx_std_20)
mobile3d_apply_compiler_options(mobile3d_core)
