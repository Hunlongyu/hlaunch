#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN

#include <doctest/doctest.h>
#include <glaze/glaze.hpp>
#include <windows.h>
#include <wil/resource.h>

#include <string>

struct smoke_config {
    std::string name{};
    bool enabled{};
};

TEST_CASE("FetchContent dependencies are usable")
{
    wil::unique_handle event{CreateEventW(nullptr, TRUE, FALSE, nullptr)};
    REQUIRE(event);

    constexpr auto configReadOptions = glz::opts{.error_on_unknown_keys = false};
    std::string json = R"({"ignored":1,"enabled":true,"name":"HLaunch"})";
    smoke_config config{};

    const auto readError = glz::read<configReadOptions>(config, json);
    REQUIRE(!readError);
    CHECK(config.name == "HLaunch");
    CHECK(config.enabled);

    std::string serialized;
    const auto writeError = glz::write_json(config, serialized);
    REQUIRE(!writeError);
    CHECK(serialized == R"({"name":"HLaunch","enabled":true})");
}
