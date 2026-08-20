add_executable(hlaunch_dependency_smoke dependency_smoke.cpp)

target_link_libraries(
    hlaunch_dependency_smoke
    PRIVATE
        HLaunch::ProjectOptions
        HLaunch::Dependencies
        doctest::doctest
)

add_test(NAME dependencies.smoke COMMAND hlaunch_dependency_smoke)
