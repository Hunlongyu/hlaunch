#pragma once

#include <Windows.h>
#include <ShObjIdl_core.h>

#include <expected>

namespace hlaunch::platform::windows {

inline constexpr wchar_t appUserModelId[] = L"Hunlongyu.HLaunch";

[[nodiscard]] std::expected<void, HRESULT> setProcessAppUserModelId() noexcept;

} // namespace hlaunch::platform::windows
