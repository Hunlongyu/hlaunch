#include "platform/windows/uuid.h"

#include <objbase.h>

#include <array>
#include <cstdio>

namespace hlaunch::platform::windows {

std::expected<std::string, DWORD> createUuidV4()
{
    GUID value{};
    const auto result = CoCreateGuid(&value);
    if (FAILED(result))
    {
        return std::unexpected(static_cast<DWORD>(result));
    }

    std::array<char, 37> text{};
    const int length = std::snprintf(
        text.data(), text.size(), "%08lx-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", value.Data1,
        value.Data2, value.Data3, value.Data4[0], value.Data4[1], value.Data4[2], value.Data4[3],
        value.Data4[4], value.Data4[5], value.Data4[6], value.Data4[7]);
    if (length != 36)
    {
        return std::unexpected(ERROR_INVALID_DATA);
    }
    return std::string{text.data(), static_cast<std::size_t>(length)};
}

} // namespace hlaunch::platform::windows
