#include "infrastructure/logging/diagnostic_log.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <system_error>
#include <vector>

namespace hlaunch::infrastructure::logging {
namespace {

constexpr std::string_view sizeLimitMarker =
    "[WARN] event=log_size_limit_reached\r\n";

struct State {
    std::mutex mutex{};
    HANDLE file{INVALID_HANDLE_VALUE};
    std::filesystem::path path{};
    std::uintmax_t bytesWritten{};
    std::uintmax_t maxFileBytes{};
    bool sizeLimitReached{};
    LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter{};
    bool exceptionFilterInstalled{};
};

State state{};
std::atomic<HANDLE> crashFile{INVALID_HANDLE_VALUE};

const char* levelName(const Level level) noexcept
{
    switch (level) {
    case Level::Trace:
        return "TRACE";
    case Level::Debug:
        return "DEBUG";
    case Level::Info:
        return "INFO";
    case Level::Warning:
        return "WARN";
    case Level::Error:
        return "ERROR";
    }
    return "UNKNOWN";
}

std::string sanitizedEvent(const std::string_view event)
{
    constexpr std::size_t maxEventBytes = 4U * 1024U;
    std::string sanitized{};
    sanitized.reserve(std::min(event.size(), maxEventBytes));
    for (const char character : event.substr(0, maxEventBytes)) {
        sanitized.push_back(character == '\r' || character == '\n' ? ' ' : character);
    }
    return sanitized;
}

std::string timestamp()
{
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::array<char, 32> buffer{};
    const auto written = std::snprintf(
        buffer.data(),
        buffer.size(),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        utc.wYear,
        utc.wMonth,
        utc.wDay,
        utc.wHour,
        utc.wMinute,
        utc.wSecond,
        utc.wMilliseconds);
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)}
                       : std::string{};
}

std::string makeLine(const Level level, const std::string_view event)
{
    std::string line = timestamp();
    line += " [";
    line += levelName(level);
    line += "] pid=";
    line += std::to_string(GetCurrentProcessId());
    line += " tid=";
    line += std::to_string(GetCurrentThreadId());
    line += " event=";
    line += sanitizedEvent(event);
    line += "\r\n";
    return line;
}

bool writeBytes(const HANDLE file, const std::string_view bytes) noexcept
{
    if (file == INVALID_HANDLE_VALUE || bytes.empty()) {
        return false;
    }
    DWORD written{};
    return WriteFile(
               file,
               bytes.data(),
               static_cast<DWORD>(bytes.size()),
               &written,
               nullptr)
        && written == bytes.size();
}

void closeFileLocked() noexcept
{
    crashFile.store(INVALID_HANDLE_VALUE, std::memory_order_release);
    if (state.file != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(state.file);
        CloseHandle(state.file);
    }
    state.file = INVALID_HANDLE_VALUE;
    state.path.clear();
    state.bytesWritten = 0;
    state.maxFileBytes = 0;
    state.sizeLimitReached = false;
}

void cleanupLogs(const Options& options)
{
    struct LogFile {
        std::filesystem::path path{};
        std::filesystem::file_time_type modified{};
    };

    std::vector<LogFile> files{};
    std::error_code error{};
    const auto now = std::filesystem::file_time_type::clock::now();
    for (const auto& entry : std::filesystem::directory_iterator(options.directory, error)) {
        if (error) {
            break;
        }
        if (!entry.is_regular_file(error) || error) {
            error.clear();
            continue;
        }
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(L"HLaunch-") || entry.path().extension() != L".log") {
            continue;
        }
        const auto modified = entry.last_write_time(error);
        if (error) {
            error.clear();
            continue;
        }
        if (now - modified > options.maxAge) {
            std::filesystem::remove(entry.path(), error);
            error.clear();
            continue;
        }
        files.push_back({entry.path(), modified});
    }

    std::ranges::sort(files, {}, &LogFile::modified);
    const auto filesToKeepBeforeCreating = options.maxFiles > 0 ? options.maxFiles - 1U : 0U;
    while (files.size() > filesToKeepBeforeCreating) {
        std::filesystem::remove(files.front().path, error);
        error.clear();
        files.erase(files.begin());
    }
}

std::filesystem::path makeLogPath(const std::filesystem::path& directory, const unsigned int suffix)
{
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    std::array<wchar_t, 128> name{};
    if (suffix == 0) {
        swprintf_s(
            name.data(),
            name.size(),
            L"HLaunch-%04u%02u%02u-%02u%02u%02u-%lu.log",
            utc.wYear,
            utc.wMonth,
            utc.wDay,
            utc.wHour,
            utc.wMinute,
            utc.wSecond,
            GetCurrentProcessId());
    }
    else {
        swprintf_s(
            name.data(),
            name.size(),
            L"HLaunch-%04u%02u%02u-%02u%02u%02u-%lu-%u.log",
            utc.wYear,
            utc.wMonth,
            utc.wDay,
            utc.wHour,
            utc.wMinute,
            utc.wSecond,
            GetCurrentProcessId(),
            suffix);
    }
    return directory / name.data();
}

LONG WINAPI unhandledExceptionFilter(EXCEPTION_POINTERS* pointers) noexcept
{
    unsigned long code{};
    std::uintptr_t address{};
    if (pointers && pointers->ExceptionRecord) {
        code = pointers->ExceptionRecord->ExceptionCode;
        address = reinterpret_cast<std::uintptr_t>(pointers->ExceptionRecord->ExceptionAddress);
    }
    writeUnhandledException(code, address);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

std::expected<std::filesystem::path, LogError> initialize(const Options& options)
{
    std::scoped_lock lock{state.mutex};
    closeFileLocked();
    if (options.directory.empty() || options.maxFileBytes == 0 || options.maxFiles == 0) {
        return std::unexpected(LogError{ERROR_INVALID_PARAMETER, "invalid log options"});
    }

    std::error_code error{};
    std::filesystem::create_directories(options.directory, error);
    if (error) {
        return std::unexpected(LogError{
            static_cast<unsigned long>(error.value()),
            "failed to create log directory"});
    }
    cleanupLogs(options);

    for (unsigned int suffix = 0; suffix < 1'000U; ++suffix) {
        auto candidate = makeLogPath(options.directory, suffix);
        const auto file = CreateFileW(
            candidate.c_str(),
            FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_DELETE,
            nullptr,
            CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file != INVALID_HANDLE_VALUE) {
            state.file = file;
            state.path = candidate;
            state.maxFileBytes = options.maxFileBytes;
            crashFile.store(file, std::memory_order_release);
            const auto line = makeLine(Level::Info, "diagnostic_log_started");
            writeBytes(file, line);
            state.bytesWritten = line.size();
            return candidate;
        }
        if (GetLastError() != ERROR_FILE_EXISTS) {
            return std::unexpected(LogError{GetLastError(), "failed to create log file"});
        }
    }
    return std::unexpected(LogError{ERROR_FILE_EXISTS, "failed to allocate unique log name"});
}

void shutdown() noexcept
{
    std::scoped_lock lock{state.mutex};
    if (state.exceptionFilterInstalled) {
        SetUnhandledExceptionFilter(state.previousExceptionFilter);
        state.previousExceptionFilter = nullptr;
        state.exceptionFilterInstalled = false;
    }
    closeFileLocked();
}

void installUnhandledExceptionHandler() noexcept
{
    std::scoped_lock lock{state.mutex};
    if (state.exceptionFilterInstalled) {
        return;
    }
    state.previousExceptionFilter = SetUnhandledExceptionFilter(&unhandledExceptionFilter);
    state.exceptionFilterInstalled = true;
}

void write(const Level level, const std::string_view event) noexcept
{
    try {
        const auto line = makeLine(level, event);
        std::scoped_lock lock{state.mutex};
        if (state.file == INVALID_HANDLE_VALUE || state.sizeLimitReached) {
            return;
        }
        if (state.bytesWritten + line.size() > state.maxFileBytes) {
            const auto remaining = state.maxFileBytes - state.bytesWritten;
            if (sizeLimitMarker.size() <= remaining && writeBytes(state.file, sizeLimitMarker)) {
                state.bytesWritten += sizeLimitMarker.size();
            }
            state.sizeLimitReached = true;
            FlushFileBuffers(state.file);
            return;
        }
        if (writeBytes(state.file, line)) {
            state.bytesWritten += line.size();
        }
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch diagnostic logging failed.\n");
    }
}

void writeSystemError(
    const Level level,
    const std::string_view event,
    const unsigned long systemCode) noexcept
{
    try {
        std::string message{event};
        message += " system_code=";
        message += std::to_string(systemCode);
        write(level, message);
    }
    catch (...) {
        OutputDebugStringW(L"HLaunch diagnostic logging failed.\n");
    }
}

void writeUnhandledException(
    const unsigned long exceptionCode,
    const std::uintptr_t instructionAddress) noexcept
{
    std::array<char, 256> line{};
    SYSTEMTIME utc{};
    GetSystemTime(&utc);
    const auto length = std::snprintf(
        line.data(),
        line.size(),
        "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ [ERROR] pid=%lu tid=%lu "
        "event=unhandled_exception exception_code=0x%08lX instruction_address=0x%llX\r\n",
        utc.wYear,
        utc.wMonth,
        utc.wDay,
        utc.wHour,
        utc.wMinute,
        utc.wSecond,
        utc.wMilliseconds,
        GetCurrentProcessId(),
        GetCurrentThreadId(),
        exceptionCode,
        static_cast<unsigned long long>(instructionAddress));
    const auto file = crashFile.load(std::memory_order_acquire);
    if (length > 0 && file != INVALID_HANDLE_VALUE) {
        writeBytes(file, std::string_view{line.data(), static_cast<std::size_t>(length)});
        FlushFileBuffers(file);
    }
}

std::filesystem::path currentLogFile()
{
    std::scoped_lock lock{state.mutex};
    return state.path;
}

} // namespace hlaunch::infrastructure::logging
