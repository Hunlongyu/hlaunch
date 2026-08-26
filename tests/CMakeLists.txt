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
