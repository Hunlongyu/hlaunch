#pragma once

#include <Windows.h>

#include <expected>
#include <string>

namespace hlaunch::platform::windows {

[[nodiscard]] std::expected<std::string, DWORD> createUuidV4();

} // namespace hlaunch::platform::windows
