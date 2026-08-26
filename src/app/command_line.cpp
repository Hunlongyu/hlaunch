#include "app/command_line.h"

namespace hlaunch::app {
namespace {

constexpr std::wstring_view backdropPrefix = L"--backdrop=";
constexpr std::wstring_view opacityPrefix = L"--opacity=";

std::expected<platform::windows::WindowBackdrop, std::wstring> parseBackdrop(
    const std::wstring_view value)
{
    using platform::windows::WindowBackdrop;
    if (value == L"solid") {
        return WindowBackdrop::Solid;
    }
    if (value == L"mica") {
        return WindowBackdrop::Mica;
    }
    if (value == L"acrylic") {
        return WindowBackdrop::Acrylic;
    }
    if (value == L"tabbed") {
        return WindowBackdrop::Tabbed;
    }
    return std::unexpected(L"无效背景材质：" + std::wstring{value});
}

std::expected<std::uint8_t, std::wstring> parseOpacity(const std::wstring_view value)
{
    if (value.empty()) {
        return std::unexpected(L"透明度不能为空");
    }

    unsigned int opacity{};
    for (const wchar_t character : value) {
        if (character < L'0' || character > L'9') {
            return std::unexpected(L"透明度必须是 30 到 100 的整数");
        }
        opacity = opacity * 10U + static_cast<unsigned int>(character - L'0');
        if (opacity > 100U) {
            return std::unexpected(L"透明度必须是 30 到 100 的整数");
        }
    }
    if (opacity < 30U) {
        return std::unexpected(L"透明度必须是 30 到 100 的整数");
    }
    return static_cast<std::uint8_t>(opacity);
}

} // namespace

std::expected<StartupOptions, std::wstring> parseCommandLine(
    const std::span<const std::wstring_view> arguments)
{
    StartupOptions options{};
    for (const auto argument : arguments) {
        if (argument == L"--portable") {
            options.portable = true;
        }
        else if (argument == L"--show-search") {
            options.showSearch = true;
        }
        else if (argument == L"--show") {
            options.activation = platform::windows::ActivationCommand::Show;
        }
        else if (argument == L"--hide") {
            options.activation = platform::windows::ActivationCommand::Hide;
        }
        else if (argument == L"--toggle") {
            options.activation = platform::windows::ActivationCommand::Toggle;
        }
        else if (argument.starts_with(backdropPrefix)) {
            const auto backdrop = parseBackdrop(argument.substr(backdropPrefix.size()));
            if (!backdrop) {
                return std::unexpected(backdrop.error());
            }
            options.windowEffects.backdrop = *backdrop;
            options.backdropSpecified = true;
        }
        else if (argument.starts_with(opacityPrefix)) {
            const auto opacity = parseOpacity(argument.substr(opacityPrefix.size()));
            if (!opacity) {
                return std::unexpected(opacity.error());
            }
            options.windowEffects.opacityPercent = *opacity;
            options.opacitySpecified = true;
        }
        else {
            return std::unexpected(L"未知命令行参数：" + std::wstring{argument});
        }
    }
    return options;
}

} // namespace hlaunch::app
