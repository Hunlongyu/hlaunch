#pragma once

#include <Windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <string>
#include <string_view>

namespace hlaunch::infrastructure::logging {

enum class Level : std::uint8_t {
    Trace,
    Debug,
    Info,
    Warning,
    Error,
};

struct Options {
    std::filesystem::path directory{};
    std::uintmax_t maxFileBytes{2U * 1024U * 1024U};
    std::size_t maxFiles{5};
    std::chrono::hours maxAge{std::chrono::hours{24 * 7}};
};

struct LogError {
    unsigned long systemCode{};
    std::string message{};
};

[[nodiscard]] std::expected<std::filesystem::path, LogError> initialize(
    const Options& options);
void shutdown() noexcept;
void installUnhandledExceptionHandler() noexcept;

void write(Level level, std::string_view event) noexcept;
void writeSystemError(
    Level level,
    std::string_view event,
    unsigned long systemCode) noexcept;

// Kept public so the crash record can be verified without intentionally
// crashing the test process. Application code should use the installed filter.
void writeUnhandledException(
    unsigned long exceptionCode,
    std::uintptr_t instructionAddress) noexcept;

[[nodiscard]] std::filesystem::path currentLogFile();

} // namespace hlaunch::infrastructure::logging
