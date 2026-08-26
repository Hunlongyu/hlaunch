#pragma once

#include <Windows.h>
#include <oleidl.h>
#include <winrt/base.h>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hlaunch::platform::windows {

enum class DroppedSourceKind : std::uint8_t
{
    Path,
    Url,
};

struct DroppedSource
{
    DroppedSourceKind kind{DroppedSourceKind::Path};
    std::wstring value{};
};

class DropTarget final
{
  public:
    using DropHandler = std::function<void(std::vector<DroppedSource>, POINTL)>;

    DropTarget() = default;
    ~DropTarget();

    DropTarget(const DropTarget &) = delete;
    DropTarget &operator=(const DropTarget &) = delete;

    [[nodiscard]] bool registerForWindow(HWND window, DropHandler handler);
    void revoke() noexcept;

  private:
    HWND window_{};
    winrt::com_ptr<IDropTarget> implementation_{};
};

[[nodiscard]] winrt::com_ptr<IDropTarget> makeOleDropTarget(DropTarget::DropHandler handler);

} // namespace hlaunch::platform::windows
