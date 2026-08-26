add_library(
    hlaunch_core
    STATIC
        core/data_model.h
        core/search_index.cpp
        core/search_index.h
        core/data_validation.cpp
        core/data_validation.h
)
add_library(HLaunch::Core ALIAS hlaunch_core)

target_include_directories(
    hlaunch_core
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_core
    PUBLIC
        HLaunch::ProjectOptions
)

add_library(
    hlaunch_infrastructure_json
    STATIC
        infrastructure/json/data_json_codec.cpp
        infrastructure/json/data_json_codec.h
)
add_library(HLaunch::InfrastructureJson ALIAS hlaunch_infrastructure_json)

target_include_directories(
    hlaunch_infrastructure_json
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_infrastructure_json
    PUBLIC
        HLaunch::Core
    PRIVATE
        HLaunch::Dependencies
        HLaunch::ProjectOptions
)

add_library(
    hlaunch_infrastructure_storage
    STATIC
        infrastructure/filesystem/atomic_file.cpp
        infrastructure/filesystem/atomic_file.h
        infrastructure/filesystem/data_paths.cpp
        infrastructure/filesystem/data_paths.h
        infrastructure/filesystem/data_store.cpp
        infrastructure/filesystem/data_store.h
)
add_library(HLaunch::InfrastructureStorage ALIAS hlaunch_infrastructure_storage)

target_include_directories(
    hlaunch_infrastructure_storage
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_infrastructure_storage
    PUBLIC
        HLaunch::Core
        HLaunch::InfrastructureJson
    PRIVATE
        HLaunch::Dependencies
        HLaunch::ProjectOptions
        shell32
)

add_library(
    hlaunch_infrastructure_logging
    STATIC
        infrastructure/logging/diagnostic_log.cpp
        infrastructure/logging/diagnostic_log.h
)
add_library(HLaunch::InfrastructureLogging ALIAS hlaunch_infrastructure_logging)

target_include_directories(
    hlaunch_infrastructure_logging
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_infrastructure_logging
    PRIVATE
        HLaunch::ProjectOptions
)

add_library(
    hlaunch_activation
    STATIC
        activation/activation_context.h
        activation/screen_edge_state.cpp
        activation/screen_edge_state.h
)
add_library(HLaunch::Activation ALIAS hlaunch_activation)

target_include_directories(
    hlaunch_activation
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_activation
    PUBLIC
        HLaunch::Core
    PRIVATE
        HLaunch::ProjectOptions
)

add_library(
    hlaunch_platform_windows
    STATIC
        platform/windows/activation_command.h
        platform/windows/global_hotkey.cpp
        platform/windows/global_hotkey.h
        platform/windows/screen_edge_activation.cpp
        platform/windows/screen_edge_activation.h
        platform/windows/search_text.cpp
        platform/windows/search_text.h
        platform/windows/shell_launcher.cpp
        platform/windows/shell_launcher.h
        platform/windows/single_instance.cpp
        platform/windows/single_instance.h
        platform/windows/tray_icon.cpp
        platform/windows/tray_icon.h
        platform/windows/window_effects.cpp
        platform/windows/window_effects.h
)
add_library(HLaunch::PlatformWindows ALIAS hlaunch_platform_windows)

target_include_directories(
    hlaunch_platform_windows
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_platform_windows
    PUBLIC
        HLaunch::Activation
        HLaunch::Dependencies
    PRIVATE
        HLaunch::ProjectOptions
        advapi32
        dwmapi
        shcore
        shell32
        user32
)

add_library(
    hlaunch_ui
    STATIC
        ui/launcher_layout.cpp
        ui/launcher_layout.h
        ui/launcher_window.cpp
        ui/launcher_window.h
        ui/search_window.cpp
        ui/search_window.h
)
add_library(HLaunch::UI ALIAS hlaunch_ui)

target_include_directories(
    hlaunch_ui
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_ui
    PUBLIC
        HLaunch::Activation
        HLaunch::Core
        HLaunch::PlatformWindows
    PRIVATE
        HLaunch::Dependencies
        HLaunch::ProjectOptions
        d2d1
        dwrite
        user32
)

add_library(
    hlaunch_app_support
    STATIC
        app/command_line.cpp
        app/command_line.h
)
add_library(HLaunch::AppSupport ALIAS hlaunch_app_support)

target_include_directories(
    hlaunch_app_support
    PUBLIC
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch_app_support
    PUBLIC
        HLaunch::PlatformWindows
    PRIVATE
        HLaunch::ProjectOptions
)

add_executable(
    hlaunch
    WIN32
        app/application.cpp
        app/application.h
        app/main.cpp
        "${PROJECT_SOURCE_DIR}/resources/windows/HLaunch.manifest"
)
set_target_properties(hlaunch PROPERTIES OUTPUT_NAME HLaunch)

target_include_directories(
    hlaunch
    PRIVATE
        "${PROJECT_SOURCE_DIR}/src"
)
target_link_libraries(
    hlaunch
    PRIVATE
        HLaunch::AppSupport
        HLaunch::InfrastructureLogging
        HLaunch::InfrastructureStorage
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        HLaunch::UI
        ole32
        shell32
        user32
)
