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
