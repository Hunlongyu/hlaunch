#include "platform/windows/app_identity.h"

namespace hlaunch::platform::windows {

std::expected<void, HRESULT> setProcessAppUserModelId() noexcept
{
    const HRESULT result = SetCurrentProcessExplicitAppUserModelID(appUserModelId);
    if (FAILED(result)) {
        return std::unexpected(result);
    }
    return {};
}

} // namespace hlaunch::platform::windows
