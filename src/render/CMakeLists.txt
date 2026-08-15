add_library(mobile3d_render STATIC
    src/render_snapshot.cpp
    src/viewport_camera.cpp
)

add_library(Mobile3D::Render ALIAS mobile3d_render)

target_include_directories(mobile3d_render
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(mobile3d_render PUBLIC Mobile3D::Core)
mobile3d_apply_compiler_options(mobile3d_render)
