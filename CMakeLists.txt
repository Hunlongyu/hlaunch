cmake_minimum_required(VERSION 3.28)

project(
    HLaunch
    VERSION 0.1.0
    DESCRIPTION "Lightweight native Windows grid launcher"
    LANGUAGES CXX
)

if(NOT WIN32)
    message(FATAL_ERROR "HLaunch only supports Windows.")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    set(
        CMAKE_MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    if(
        CMAKE_GENERATOR MATCHES "Ninja"
        AND CMAKE_CXX_CL_SHOWINCLUDES_PREFIX STREQUAL "娉ㄦ剰: 鍖呭惈鏂囦欢:  "
    )
        # CMake 4.3 can misdecode localized cl.exe /showIncludes output while
        # detecting the compiler. Ninja then loses header dependencies and may
        # link ABI-incompatible stale objects after a class layout change.
        set(CMAKE_CXX_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
        set(CMAKE_CL_SHOWINCLUDES_PREFIX "注意: 包含文件:  ")
    endif()
endif()

include(CTest)
include(cmake/Dependencies.cmake)

add_library(hlaunch_project_options INTERFACE)
add_library(HLaunch::ProjectOptions ALIAS hlaunch_project_options)

target_compile_features(hlaunch_project_options INTERFACE cxx_std_23)
target_compile_definitions(
    hlaunch_project_options
    INTERFACE
        NOMINMAX
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(
        hlaunch_project_options
        INTERFACE
            /permissive-
            /utf-8
            /W4
            /EHsc
            /MP
    )
endif()

add_library(hlaunch_dependencies INTERFACE)
add_library(HLaunch::Dependencies ALIAS hlaunch_dependencies)
target_link_libraries(
    hlaunch_dependencies
    INTERFACE
        WIL::WIL
        glaze::glaze
)

add_subdirectory(src)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
