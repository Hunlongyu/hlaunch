#include "app/application.h"
#include "app/command_line.h"

#include <Windows.h>
#include <shellapi.h>
#include <wil/resource.h>

#include <span>
#include <string_view>
#include <vector>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
    int argumentCount = 0;
    wil::unique_hlocal_ptr<wchar_t*[]> arguments{
        CommandLineToArgvW(GetCommandLineW(), &argumentCount)};
    if (!arguments || argumentCount < 1) {
        return 1;
    }

    std::vector<std::wstring_view> argumentViews{};
    argumentViews.reserve(static_cast<std::size_t>(argumentCount - 1));
    for (int index = 1; index < argumentCount; ++index) {
        argumentViews.emplace_back(arguments[index]);
    }

    const auto options = hlaunch::app::parseCommandLine(argumentViews);
    if (!options) {
        MessageBoxW(nullptr, options.error().c_str(), L"HLaunch", MB_OK | MB_ICONERROR);
        return 2;
    }

    hlaunch::app::Application application{};
    return application.run(instance, *options);
}
