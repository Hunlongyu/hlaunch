#include <Windows.h>
#include <shellapi.h>

#include <cstdint>
#include <filesystem>
#include <fstream>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    int argumentCount = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argumentCount);
    if (!arguments || argumentCount < 2) {
        if (arguments) {
            LocalFree(reinterpret_cast<HLOCAL>(arguments));
        }
        return 2;
    }

    std::ofstream output(
        std::filesystem::path{arguments[1]},
        std::ios::binary | std::ios::trunc);
    if (!output) {
        LocalFree(reinterpret_cast<HLOCAL>(arguments));
        return 3;
    }

    const auto valueCount = static_cast<std::uint32_t>(argumentCount - 2);
    output.write(reinterpret_cast<const char*>(&valueCount), sizeof(valueCount));
    for (int index = 2; index < argumentCount; ++index) {
        const std::wstring_view value{arguments[index]};
        const auto characterCount = static_cast<std::uint32_t>(value.size());
        output.write(
            reinterpret_cast<const char*>(&characterCount),
            sizeof(characterCount));
        output.write(
            reinterpret_cast<const char*>(value.data()),
            static_cast<std::streamsize>(value.size() * sizeof(wchar_t)));
    }
    output.close();
    LocalFree(reinterpret_cast<HLOCAL>(arguments));
    return output ? 0 : 4;
}
