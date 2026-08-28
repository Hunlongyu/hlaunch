#pragma once

#include <string_view>

#ifndef HLAUNCH_VERSION_STRING
#error HLAUNCH_VERSION_STRING must be provided by CMake.
#endif

#define HLAUNCH_DETAIL_WIDEN_IMPL(value) L##value
#define HLAUNCH_DETAIL_WIDEN(value) HLAUNCH_DETAIL_WIDEN_IMPL(value)
#define HLAUNCH_VERSION_WIDE_LITERAL HLAUNCH_DETAIL_WIDEN(HLAUNCH_VERSION_STRING)

namespace hlaunch {

inline constexpr std::string_view applicationVersion{HLAUNCH_VERSION_STRING};
inline constexpr std::wstring_view applicationVersionWide{HLAUNCH_VERSION_WIDE_LITERAL};

} // namespace hlaunch
