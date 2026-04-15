#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace shogun2 {

namespace {

HMODULE g_self_module = nullptr;
constexpr LONGLONG kMaxLogSizeBytes = 256 * 1024;
INIT_ONCE g_log_reset_once = INIT_ONCE_STATIC_INIT;

std::wstring GetLogPath() {
    return GetModuleDirectory() + L"\\shogun2_borderless.log";
}

BOOL CALLBACK ResetLogFileForSession(PINIT_ONCE, PVOID, PVOID*) {
    // Start each game launch with a fresh log file so crash/debug sessions do
    // not append into previous runs.
    const std::wstring log_path = GetLogPath();
    HANDLE file = CreateFileW(
        log_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        CloseHandle(file);
    }
    return TRUE;
}

}  // namespace

void SetSelfModule(HMODULE module) noexcept {
    g_self_module = module;
}

HMODULE GetSelfModule() noexcept {
    return g_self_module;
}

std::wstring GetModuleDirectory() {
    wchar_t buffer[MAX_PATH] = {};
    HMODULE module = g_self_module != nullptr ? g_self_module : GetModuleHandleW(nullptr);
    const DWORD length = GetModuleFileNameW(module, buffer, static_cast<DWORD>(std::size(buffer)));
    if (length == 0 || length >= std::size(buffer)) {
        return L".";
    }

    std::wstring path(buffer, length);
    const std::wstring::size_type slash = path.find_last_of(L"\\/");
    if (slash == std::wstring::npos) {
        return L".";
    }

    return path.substr(0, slash);
}

void Log(const char* format, ...) {
    PVOID context = nullptr;
    (void)InitOnceExecuteOnce(&g_log_reset_once, &ResetLogFileForSession, nullptr, &context);

    char message[1024] = {};

    va_list args;
    va_start(args, format);
    (void)vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
    va_end(args);

    SYSTEMTIME local_time = {};
    GetLocalTime(&local_time);

    char line[1280] = {};
    (void)snprintf(
        line,
        sizeof(line),
        "[%02u:%02u:%02u.%03u] %s\r\n",
        local_time.wHour,
        local_time.wMinute,
        local_time.wSecond,
        local_time.wMilliseconds,
        message);

    OutputDebugStringA(line);

    const std::wstring log_path = GetLogPath();
    HANDLE file = CreateFileW(
        log_path.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return;
    }

    LARGE_INTEGER file_size = {};
    if (GetFileSizeEx(file, &file_size) && file_size.QuadPart >= kMaxLogSizeBytes) {
        LARGE_INTEGER zero = {};
        (void)SetFilePointerEx(file, zero, nullptr, FILE_BEGIN);
        (void)SetEndOfFile(file);

        static constexpr char kTruncatedMarker[] = "[log reset] shogun2_borderless.log exceeded size limit\r\n";
        DWORD truncated_written = 0;
        (void)WriteFile(file, kTruncatedMarker, static_cast<DWORD>(sizeof(kTruncatedMarker) - 1), &truncated_written, nullptr);
    }

    LARGE_INTEGER end = {};
    (void)SetFilePointerEx(file, end, nullptr, FILE_END);

    const DWORD byte_count = static_cast<DWORD>(strnlen(line, sizeof(line)));
    DWORD written = 0;
    (void)WriteFile(file, line, byte_count, &written, nullptr);
    CloseHandle(file);
}

}  // namespace shogun2
