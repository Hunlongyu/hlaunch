#pragma once

namespace hlaunch::platform::windows {

enum class ActivationCommand : unsigned long long { // NOLINT(performance-enum-size): IPC stores the command directly in Win32 WPARAM.
    Show = 1,
    Hide = 2,
    Toggle = 3,
};

} // namespace hlaunch::platform::windows
