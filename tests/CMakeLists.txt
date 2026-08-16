add_executable(mobile3d_core_tests
    test_main.cpp
    test_scene.cpp
    test_commands.cpp
    test_organization.cpp
    test_selection.cpp
    test_project.cpp
    test_editor_session.cpp
    test_render_snapshot.cpp
    test_render_world_rotation.cpp
    test_viewport_camera.cpp
    test_transform_gizmo.cpp
    test_transform_manipulator.cpp
    test_editable_mesh.cpp
)

target_link_libraries(mobile3d_core_tests
    PRIVATE
        Mobile3D::Core
        Mobile3D::Editor
        Mobile3D::Render
)
target_include_directories(mobile3d_core_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
mobile3d_apply_compiler_options(mobile3d_core_tests)

add_test(NAME mobile3d_core_tests COMMAND mobile3d_core_tests)
