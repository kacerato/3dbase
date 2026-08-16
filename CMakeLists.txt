cmake_minimum_required(VERSION 3.25)

project(Mobile3DStudio VERSION 0.1.0 LANGUAGES CXX)

option(MOBILE3D_BUILD_TESTS "Build Mobile3D tests" ON)
option(MOBILE3D_BUILD_QT_APP "Build the Qt Quick editor application" OFF)
option(MOBILE3D_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(Mobile3DCompilerOptions)

add_subdirectory(src/core)
add_subdirectory(src/editor)
add_subdirectory(src/render)

if(MOBILE3D_BUILD_QT_APP)
    add_subdirectory(src/app)
endif()

if(MOBILE3D_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
