find_package(Qt6 6.8 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 ShaderTools)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(mobile3d_studio
    main.cpp
    qt/editor_controller.cpp
    qt/editor_controller.hpp
    qt/outliner_model.cpp
    qt/outliner_model.hpp
    qt/vulkan_shader_library.cpp
    qt/vulkan_shader_library.hpp
    qt/vulkan_viewport.cpp
    qt/vulkan_viewport.hpp
)

qt_add_qml_module(mobile3d_studio
    URI Mobile3D
    VERSION 1.0
    DEPENDENCIES
        QtQuick
    QML_FILES
        qml/Main.qml
        qml/TopBar.qml
        qml/WorkspaceBar.qml
        qml/ProjectHome.qml
        qml/OutlinerPanel.qml
        qml/InspectorPanel.qml
        qml/ViewportPlaceholder.qml
)

qt_add_shaders(mobile3d_studio "mobile3d_viewport_shaders"
    PREFIX
        "/"
    NOGLSL
    NOHLSL
    NOMSL
    FILES
        shaders/viewport_line.vert
        shaders/viewport_line.frag
)

target_include_directories(mobile3d_studio PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/qt)
target_link_libraries(mobile3d_studio
    PRIVATE
        Mobile3D::Editor
        Mobile3D::Render
        Qt6::Core
        Qt6::Gui
        Qt6::GuiPrivate
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
)
mobile3d_apply_compiler_options(mobile3d_studio)

set_target_properties(mobile3d_studio PROPERTIES OUTPUT_NAME "Mobile3DStudio")

if(ANDROID)
    set_target_properties(mobile3d_studio PROPERTIES
        QT_ANDROID_PACKAGE_NAME "com.kacerato.mobile3dstudio"
        QT_ANDROID_MIN_SDK_VERSION 28
    )
endif()
