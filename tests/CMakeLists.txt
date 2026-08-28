add_executable(hlaunch_dependency_smoke dependency_smoke.cpp)

target_link_libraries(
    hlaunch_dependency_smoke
    PRIVATE
        HLaunch::ProjectOptions
        HLaunch::Dependencies
        doctest::doctest
)

add_test(NAME dependencies.smoke COMMAND hlaunch_dependency_smoke)

add_executable(hlaunch_data_config_tests data_config_tests.cpp)

target_link_libraries(
    hlaunch_data_config_tests
    PRIVATE
        HLaunch::ProjectOptions
        HLaunch::InfrastructureJson
        doctest::doctest
)

add_test(NAME DATA-CONFIG-001.memory-codec COMMAND hlaunch_data_config_tests)

add_executable(hlaunch_data_store_tests data_store_tests.cpp)

target_link_libraries(
    hlaunch_data_store_tests
    PRIVATE
        HLaunch::ProjectOptions
        HLaunch::InfrastructureStorage
        doctest::doctest
)

add_test(NAME DATA-CONFIG-001.storage COMMAND hlaunch_data_store_tests)

add_executable(hlaunch_diagnostic_log_tests diagnostic_log_tests.cpp)

target_link_libraries(
    hlaunch_diagnostic_log_tests
    PRIVATE
        HLaunch::InfrastructureLogging
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME QUALITY-LOG-001.rotation-and-crash-record COMMAND hlaunch_diagnostic_log_tests)

add_executable(hlaunch_search_tests search_tests.cpp)

target_link_libraries(
    hlaunch_search_tests
    PRIVATE
        HLaunch::Core
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PROD-GRID-001.search COMMAND hlaunch_search_tests)

add_executable(hlaunch_item_operations_tests item_operations_tests.cpp)

target_link_libraries(
    hlaunch_item_operations_tests
    PRIVATE
        HLaunch::Core
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PROD-ITEM-001.mutations COMMAND hlaunch_item_operations_tests)

add_executable(hlaunch_drop_item_resolver_tests drop_item_resolver_tests.cpp)

target_link_libraries(
    hlaunch_drop_item_resolver_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PROD-DROP-001.resolve COMMAND hlaunch_drop_item_resolver_tests)

add_executable(hlaunch_icon_loader_tests icon_loader_tests.cpp)

target_link_libraries(
    hlaunch_icon_loader_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)
target_compile_definitions(
    hlaunch_icon_loader_tests
    PRIVATE
        HLAUNCH_TEST_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
)

add_test(NAME UI-ICON-001.extract-and-cache COMMAND hlaunch_icon_loader_tests)

add_executable(hlaunch_drop_target_tests drop_target_tests.cpp)

target_link_libraries(
    hlaunch_drop_target_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PROD-DROP-001.ole-data-object COMMAND hlaunch_drop_target_tests)

add_executable(hlaunch_app_ui_tests app_ui_tests.cpp)

target_link_libraries(
    hlaunch_app_ui_tests
    PRIVATE
        HLaunch::AppSupport
        HLaunch::ProjectOptions
        HLaunch::UI
        doctest::doctest
)

add_test(NAME PLAT-SINGLE-001.PROD-GRID-001.scaffold COMMAND hlaunch_app_ui_tests)

add_executable(hlaunch_global_hotkey_tests global_hotkey_tests.cpp)

target_link_libraries(
    hlaunch_global_hotkey_tests
    PRIVATE
        HLaunch::Core
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME ACT-HOTKEY-001.registration COMMAND hlaunch_global_hotkey_tests)

add_executable(hlaunch_startup_registration_tests startup_registration_tests.cpp)

target_link_libraries(
    hlaunch_startup_registration_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PLAT-AUTOSTART-001.registry COMMAND hlaunch_startup_registration_tests)

add_executable(hlaunch_screen_edge_tests screen_edge_tests.cpp)

target_link_libraries(
    hlaunch_screen_edge_tests
    PRIVATE
        HLaunch::Activation
        HLaunch::ProjectOptions
        HLaunch::UI
        doctest::doctest
)

add_test(NAME ACT-EDGE-001.state-and-layout COMMAND hlaunch_screen_edge_tests)

add_executable(hlaunch_tray_icon_tests tray_icon_tests.cpp)

target_link_libraries(
    hlaunch_tray_icon_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)

add_test(NAME PLAT-TRAY-001.lifecycle COMMAND hlaunch_tray_icon_tests)

add_executable(hlaunch_shell_probe WIN32 shell_launch_probe.cpp)

target_link_libraries(
    hlaunch_shell_probe
    PRIVATE
        HLaunch::ProjectOptions
        shell32
)

add_executable(hlaunch_shell_launcher_tests shell_launcher_tests.cpp)

target_link_libraries(
    hlaunch_shell_launcher_tests
    PRIVATE
        HLaunch::PlatformWindows
        HLaunch::ProjectOptions
        doctest::doctest
)
add_dependencies(hlaunch_shell_launcher_tests hlaunch_shell_probe)

add_test(NAME PLAT-SHELL-001.launch COMMAND hlaunch_shell_launcher_tests)
