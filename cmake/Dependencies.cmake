include_guard(GLOBAL)

include(FetchContent)
include(${CMAKE_CURRENT_LIST_DIR}/DependencyVersions.cmake)

# WIL v1.0.260126.7. The release's own CMake project requires CMake 3.29
# and enables its tests and packaging by default. HLaunch only consumes the
# header-only library, so SOURCE_SUBDIR deliberately prevents add_subdirectory.
FetchContent_Declare(
    wil
    GIT_REPOSITORY https://github.com/microsoft/wil.git
    GIT_TAG ${HLAUNCH_WIL_REVISION}
    SOURCE_SUBDIR _hlaunch_header_only
)
FetchContent_MakeAvailable(wil)

add_library(hlaunch_wil INTERFACE)
add_library(WIL::WIL ALIAS hlaunch_wil)
target_include_directories(
    hlaunch_wil
    SYSTEM INTERFACE
        "${wil_SOURCE_DIR}/include"
)

# Glaze v7.9.1 is header-only. HLaunch only uses its JSON serialization layer;
# optional formats, networking, examples, installation rules, and experimental
# C++26 reflection are deliberately excluded.
set(glaze_INSTALL OFF CACHE BOOL "" FORCE)
set(glaze_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(glaze_EETF_FORMAT OFF CACHE BOOL "" FORCE)
set(glaze_ENABLE_SSL OFF CACHE BOOL "" FORCE)
set(glaze_ENABLE_REFLECTION26 OFF CACHE BOOL "" FORCE)
set(glaze_DISABLE_ALWAYS_INLINE ON CACHE BOOL "Reduce Glaze compile time and binary size" FORCE)
set(glaze_DEFAULT_OPTIMIZATION_SIZE ON CACHE BOOL "Prefer compact configuration parsing" FORCE)

FetchContent_Declare(
    glaze
    GIT_REPOSITORY https://github.com/stephenberry/glaze.git
    GIT_TAG ${HLAUNCH_GLAZE_REVISION}
    EXCLUDE_FROM_ALL
    SYSTEM
)
FetchContent_MakeAvailable(glaze)

if(NOT TARGET glaze::glaze)
    message(FATAL_ERROR "Glaze did not provide glaze::glaze.")
endif()

if(BUILD_TESTING)
    # doctest v2.5.3 is development-only and is not fetched for product-only
    # configurations that set BUILD_TESTING=OFF.
    set(DOCTEST_WITH_TESTS OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG ${HLAUNCH_DOCTEST_REVISION}
        EXCLUDE_FROM_ALL
        SYSTEM
    )
    FetchContent_MakeAvailable(doctest)

    if(NOT TARGET doctest::doctest)
        message(FATAL_ERROR "doctest did not provide doctest::doctest.")
    endif()
endif()
