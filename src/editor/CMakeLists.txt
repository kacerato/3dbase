add_library(mobile3d_editor STATIC
    src/editor_session.cpp
    src/editor_mesh_edit.cpp
    src/editor_mesh_operators.cpp
    src/transform_gizmo.cpp
    src/transform_manipulator.cpp
    src/mesh_selection.cpp
    src/mesh_edit_snapshot.cpp
)

add_library(Mobile3D::Editor ALIAS mobile3d_editor)

target_include_directories(mobile3d_editor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mobile3d_editor PUBLIC Mobile3D::Core)
mobile3d_apply_compiler_options(mobile3d_editor)
