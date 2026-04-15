#include "borderless_hooks.h"

#include "common.h"

#include <windows.h>
#include <winnt.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cwchar>

namespace shogun2 {

namespace {

constexpr DWORD kStyleBitsToClear = WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME;
constexpr DWORD kExStyleBitsToClear =
    WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE;
constexpr std::uintptr_t kWarscapeImageBase = 0x10000000u;
constexpr std::uintptr_t kConfigDirtyOffset = 0x59u;
constexpr std::uintptr_t kConfigValueOffset = 0x5cu;

constexpr std::uintptr_t kRvaEnableUiScaling = 0x11c9f7e8u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiRectOverride = 0x11c9ae28u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiX = 0x11c9b240u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiY = 0x11c9c418u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiWidth = 0x11c9c2e0u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiHeight = 0x11c9af28u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiRectLeft = 0x11c9f848u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiRectTop = 0x11ca0110u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiRectWidth = 0x11c9f728u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaUiRectHeight = 0x11ca06f0u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaGetUiSourceRect = 0x11421a80u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaComputeUiSizePrimary = 0x113f7c00u - kWarscapeImageBase;
constexpr std::uintptr_t kRvaComputeUiSizeSecondary = 0x113f7d20u - kWarscapeImageBase;
constexpr float kUiScaleMin = 0.5f;
constexpr float kUiScaleMax = 2.0f;
constexpr float kUiScaleStepDefault = 0.10f;
constexpr float kUiScaleStepMin = 0.01f;
constexpr float kUiScaleStepMax = 0.50f;
constexpr float kUiScaleIdentityEpsilon = 0.001f;
constexpr UINT_PTR kOverlayTimerId = 1;
constexpr UINT kOverlayDurationMs = 1600;
constexpr wchar_t kOverlayWindowClassName[] = L"Shogun2UiScaleOverlay";
constexpr std::size_t kMaxTrackedLoadFiles = 512;
constexpr std::size_t kMaxTrackedLoadMappings = 512;
constexpr std::size_t kMaxTrackedRawMappedViews = 4096;
constexpr std::size_t kMaxTrackedLoadPaths = 512;
constexpr std::size_t kMaxTrackedLoadRegions = 2048;
constexpr std::size_t kMaxCachedMapWindows = 128;
constexpr std::size_t kMaxCachedClientViews = 8192;
constexpr std::size_t kMaxTrackedLoadPathChars = 520;
constexpr std::size_t kMaxTrackedLoadRegionLabelChars = 96;
constexpr std::size_t kLoadTopPathCount = 8;
constexpr std::size_t kLoadTopRegionCount = 12;
constexpr ULONGLONG kLargeLoadThresholdBytes = 1ull << 20;
constexpr ULONGLONG kLoadRegionBucketBytes = 16ull * 1024ull * 1024ull;
constexpr LONG kLoadOpenLogLimit = 64;
constexpr LONG kLoadCloseLogLimit = 256;
constexpr LONG kLoadReadLogLimit = 32;
constexpr LONG kLoadMapLogLimit = 64;
constexpr LONG kLoadPrewarmLogLimit = 24;
constexpr LONG kLoadShaderPinLogLimit = 16;
constexpr DWORD kLoadCacheWindowMbDefault = 8;
constexpr DWORD kLoadCacheWindowMbMin = 1;
constexpr DWORD kLoadCacheWindowMbMax = 64;
constexpr DWORD kLoadCacheMinFileMbDefault = 128;
constexpr DWORD kLoadCacheMinFileMbMin = 1;
constexpr DWORD kLoadCacheMinFileMbMax = 4096;
constexpr DWORD kLoadCacheMaxWindowsDefault = 24;
constexpr DWORD kLoadCacheMaxWindowsMin = 1;
constexpr DWORD kLoadCacheMaxWindowsMax = static_cast<DWORD>(kMaxCachedMapWindows);
constexpr DWORD kPrewarmDelayMsDefault = 5000;
constexpr DWORD kPrewarmDelayMsMax = 60000;
constexpr DWORD kPrewarmChunkMbDefault = 4;
constexpr DWORD kPrewarmChunkMbMin = 1;
constexpr DWORD kPrewarmChunkMbMax = 16;
constexpr std::size_t kInvalidCachedWindowIndex = static_cast<std::size_t>(-1);

using CreateWindowExWFn = decltype(&::CreateWindowExW);
using SetWindowLongWFn = decltype(&::SetWindowLongW);
using SetWindowPosFn = decltype(&::SetWindowPos);
using MoveWindowFn = decltype(&::MoveWindow);
using ChangeDisplaySettingsWFn = decltype(&::ChangeDisplaySettingsW);
using SetWindowLongPtrWFn = decltype(&::SetWindowLongPtrW);
using CreateFileWFn = decltype(&::CreateFileW);
using ReadFileFn = decltype(&::ReadFile);
using ReadFileExFn = decltype(&::ReadFileEx);
using CreateFileMappingWFn = decltype(&::CreateFileMappingW);
using MapViewOfFileFn = decltype(&::MapViewOfFile);
using UnmapViewOfFileFn = decltype(&::UnmapViewOfFile);
using SetFilePointerExFn = decltype(&::SetFilePointerEx);
using CloseHandleFn = decltype(&::CloseHandle);

struct UiRect {
    float x;
    float y;
    float width;
    float height;
};

struct UiSize {
    LONG width;
    LONG height;
};

using GetUiSourceRectFn = UiRect* (WINAPI*)(UiRect*);
using ComputeUiSizeFn = UiSize* (__thiscall*)(void*, UiSize*);

CreateWindowExWFn g_original_create_window_ex_w = ::CreateWindowExW;
SetWindowLongWFn g_original_set_window_long_w = ::SetWindowLongW;
SetWindowPosFn g_original_set_window_pos = ::SetWindowPos;
MoveWindowFn g_original_move_window = ::MoveWindow;
ChangeDisplaySettingsWFn g_original_change_display_settings_w = ::ChangeDisplaySettingsW;
GetUiSourceRectFn g_original_get_ui_source_rect = nullptr;
ComputeUiSizeFn g_original_compute_ui_size_primary = nullptr;
ComputeUiSizeFn g_original_compute_ui_size_secondary = nullptr;
SetWindowLongPtrWFn g_original_set_window_long_ptr_w = ::SetWindowLongPtrW;

HWND g_main_window = nullptr;
WNDPROC g_original_main_window_proc = nullptr;
HWND g_overlay_window = nullptr;
HFONT g_overlay_font = nullptr;
LONG g_install_started = 0;
LONG g_hooks_installed = 0;

struct UiScaleConfig {
    bool present = true;
    float scale = 1.0f;
    float step = kUiScaleStepDefault;
    bool hotkeys_enabled = false;
    bool overlay_enabled = false;
};

enum class ConfigValueKind {
    Bool,
    Float,
};

struct UiDebugEntry {
    const char* name;
    std::uintptr_t rva;
    ConfigValueKind kind;
};

struct UiDebugSnapshot {
    bool valid = false;
    LONG target_width = 0;
    LONG target_height = 0;
    float scale = 0.0f;
    bool disable = false;
};

// Load-time tuning is kept separate from UI config because it controls
// experimental behavior that may change while we iterate on profiling.
struct LoadConfig {
    bool map_cache_enabled = false;
    bool random_access_hint = false;
    bool prewarm_hot_packs = true;
    bool prewarm_hot_regions = true;
    bool pin_shaders_pack = true;
    DWORD cache_window_mb = kLoadCacheWindowMbDefault;
    DWORD cache_min_file_mb = kLoadCacheMinFileMbDefault;
    DWORD max_cached_windows = kLoadCacheMaxWindowsDefault;
    DWORD prewarm_delay_ms = kPrewarmDelayMsDefault;
    DWORD prewarm_chunk_mb = kPrewarmChunkMbDefault;
};

struct LoadFileRecord;

struct MapCacheRequest {
    bool eligible = false;
    HANDLE file_handle = nullptr;
    LoadFileRecord* file_record = nullptr;
    bool interesting = false;
    ULONGLONG request_offset = 0;
    ULONGLONG request_bytes = 0;
    ULONGLONG window_offset = 0;
    ULONGLONG window_bytes = 0;
};

// Per-file state for loader profiling. We keep this fixed-size so the hook
// path stays simple and does not depend on dynamic allocation.
struct LoadFileRecord {
    bool active = false;
    HANDLE handle = nullptr;
    DWORD file_type = FILE_TYPE_UNKNOWN;
    DWORD desired_access = 0;
    DWORD creation_disposition = 0;
    DWORD flags_and_attributes = 0;
    bool interesting = false;
    bool random_access_hint_applied = false;
    wchar_t path[kMaxTrackedLoadPathChars] = {};
    LARGE_INTEGER file_size = {};
    ULONGLONG open_tick = 0;
    ULONGLONG total_read_requested_bytes = 0;
    ULONGLONG total_read_bytes = 0;
    ULONGLONG total_read_ex_requested_bytes = 0;
    ULONGLONG total_sync_read_time_ms = 0;
    ULONGLONG total_mapped_bytes = 0;
    ULONGLONG total_map_api_time_ms = 0;
    ULONGLONG total_unmap_api_time_ms = 0;
    DWORD read_calls = 0;
    DWORD read_ex_calls = 0;
    DWORD pending_read_calls = 0;
    DWORD map_view_calls = 0;
    DWORD unmap_view_calls = 0;
    DWORD seek_calls = 0;
    DWORD max_read_size = 0;
    ULONGLONG max_map_view_size = 0;
    DWORD cached_map_hits = 0;
};

// Tracks file-mapping handles so a later MapViewOfFile call can be attributed
// back to the original file handle and path.
struct LoadMappingRecord {
    bool active = false;
    HANDLE handle = nullptr;
    HANDLE file_handle = nullptr;
    ULONGLONG max_mapping_bytes = 0;
    DWORD protect = 0;
};

// Raw mapped views are the pass-through case: the game asked the OS for a view
// and we returned that exact pointer. We track them so later UnmapViewOfFile
// calls can be attributed back to the correct file.
struct RawMappedViewRecord {
    bool active = false;
    void* base = nullptr;
    HANDLE mapping_handle = nullptr;
    HANDLE file_handle = nullptr;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG offset = 0;
    ULONGLONG map_tick = 0;
    bool interesting = false;
};

// A cached window is one real OS mapping that may satisfy many nearby logical
// MapViewOfFile requests from the game. We keep it alive after client unmaps so
// later requests can reuse it without another kernel mapping call.
struct CachedMapWindow {
    bool active = false;
    bool reusable = false;
    HANDLE mapping_handle = nullptr;
    HANDLE file_handle = nullptr;
    DWORD desired_access = 0;
    std::uint8_t* real_base = nullptr;
    ULONGLONG window_offset = 0;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG map_tick = 0;
    ULONGLONG last_touch_tick = 0;
    LONG live_client_views = 0;
    bool interesting = false;
};

// Each cached client view is what the game thinks it received from
// MapViewOfFile. It may point into the middle of a larger cached window.
struct CachedClientView {
    bool active = false;
    void* client_base = nullptr;
    HANDLE file_handle = nullptr;
    ULONGLONG requested_bytes = 0;
    ULONGLONG request_offset = 0;
    ULONGLONG map_tick = 0;
    bool interesting = false;
    std::size_t window_index = kInvalidCachedWindowIndex;
};

// Process-wide counters used to answer the first question about loading:
// is Shogun 2 mostly reading, mapping, or seeking through large pack files?
struct LoadProfilerTotals {
    bool started = false;
    bool flushed = false;
    ULONGLONG start_tick = 0;
    ULONGLONG file_opens = 0;
    ULONGLONG disk_file_opens = 0;
    ULONGLONG pack_file_opens = 0;
    ULONGLONG read_calls = 0;
    ULONGLONG read_requested_bytes = 0;
    ULONGLONG read_bytes = 0;
    ULONGLONG read_ex_calls = 0;
    ULONGLONG read_ex_requested_bytes = 0;
    ULONGLONG pending_read_calls = 0;
    ULONGLONG seek_calls = 0;
    ULONGLONG mapping_creates = 0;
    ULONGLONG map_view_calls = 0;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG unmap_view_calls = 0;
    ULONGLONG map_api_calls = 0;
    ULONGLONG map_api_time_ms = 0;
    ULONGLONG unmap_api_calls = 0;
    ULONGLONG unmap_api_time_ms = 0;
    ULONGLONG map_cache_hits = 0;
    ULONGLONG map_cache_misses = 0;
    ULONGLONG map_cache_evictions = 0;
    ULONGLONG dropped_raw_view_records = 0;
    ULONGLONG dropped_cached_windows = 0;
    ULONGLONG dropped_cached_client_views = 0;
    ULONGLONG close_calls = 0;
    ULONGLONG close_failures = 0;
    ULONGLONG dropped_file_records = 0;
    ULONGLONG dropped_mapping_records = 0;
    ULONGLONG dropped_path_records = 0;
    ULONGLONG dropped_region_records = 0;
};

// Per-path rollups survive handle closes, which makes them much more useful
// than individual close lines when we are trying to identify which assets
// dominate the full-session loading cost.
struct LoadPathAggregate {
    bool active = false;
    bool interesting = false;
    wchar_t path[kMaxTrackedLoadPathChars] = {};
    LARGE_INTEGER file_size = {};
    ULONGLONG handles = 0;
    ULONGLONG random_access_hint_opens = 0;
    ULONGLONG read_calls = 0;
    ULONGLONG read_bytes = 0;
    ULONGLONG read_requested_bytes = 0;
    ULONGLONG read_ex_calls = 0;
    ULONGLONG read_ex_requested_bytes = 0;
    ULONGLONG pending_read_calls = 0;
    ULONGLONG seek_calls = 0;
    ULONGLONG map_view_calls = 0;
    ULONGLONG cached_map_hits = 0;
    ULONGLONG unmap_view_calls = 0;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG map_api_time_ms = 0;
    ULONGLONG unmap_api_time_ms = 0;
    DWORD max_read_size = 0;
    ULONGLONG max_map_view_size = 0;
};

enum class LoadRegionKind {
    MapView,
    ReadFileEx,
};

// Region buckets let us answer the next question after top-file profiling:
// which specific offset ranges inside a giant pack are hot enough to target?
// We keep only a short label here and use a stable path hash for matching.
struct LoadRegionAggregate {
    bool active = false;
    bool interesting = false;
    wchar_t label[kMaxTrackedLoadRegionLabelChars] = {};
    std::uint64_t path_hash = 0;
    ULONGLONG bucket_offset = 0;
    ULONGLONG bucket_size = 0;
    ULONGLONG map_view_calls = 0;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG read_ex_calls = 0;
    ULONGLONG read_ex_requested_bytes = 0;
};

UiScaleConfig g_ui_scale_config = {};
bool g_ui_scale_config_loaded = false;
LoadConfig g_load_config = {};
bool g_load_config_loaded = false;
UiDebugSnapshot g_last_ui_debug_snapshot = {};
LONG g_ui_source_rect_log_count = 0;
LONG g_ui_size_primary_log_count = 0;
LONG g_ui_size_secondary_log_count = 0;
LONG g_ui_size_primary_override_log_count = 0;
LONG g_ui_size_secondary_override_log_count = 0;
LONG g_ui_hotkey_log_count = 0;
LONG g_load_open_log_count = 0;
LONG g_load_close_log_count = 0;
LONG g_load_read_log_count = 0;
LONG g_load_map_log_count = 0;
LONG g_load_prewarm_log_count = 0;
LONG g_load_prewarm_started = 0;
LONG g_load_shader_pin_log_count = 0;

// Shader-pack pinning keeps one private read-only mapping alive in our helper
// DLL for the whole session. The game still uses its own mappings; this just
// gives Windows a strong reason to keep the tiny-but-hot shader archive resident.
HANDLE g_shader_pin_file = nullptr;
HANDLE g_shader_pin_mapping = nullptr;
void* g_shader_pin_view = nullptr;
ULONGLONG g_shader_pin_size = 0;

CreateFileWFn g_original_create_file_w = ::CreateFileW;
ReadFileFn g_original_read_file = ::ReadFile;
ReadFileExFn g_original_read_file_ex = ::ReadFileEx;
CreateFileMappingWFn g_original_create_file_mapping_w = ::CreateFileMappingW;
MapViewOfFileFn g_original_map_view_of_file = ::MapViewOfFile;
UnmapViewOfFileFn g_original_unmap_view_of_file = ::UnmapViewOfFile;
SetFilePointerExFn g_original_set_file_pointer_ex = ::SetFilePointerEx;
CloseHandleFn g_original_close_handle = ::CloseHandle;

SRWLOCK g_load_profile_lock = SRWLOCK_INIT;
LoadFileRecord g_load_file_records[kMaxTrackedLoadFiles] = {};
LoadMappingRecord g_load_mapping_records[kMaxTrackedLoadMappings] = {};
RawMappedViewRecord g_raw_mapped_view_records[kMaxTrackedRawMappedViews] = {};
LoadPathAggregate g_load_path_aggregates[kMaxTrackedLoadPaths] = {};
LoadRegionAggregate g_load_region_aggregates[kMaxTrackedLoadRegions] = {};
// Shutdown summary snapshots are stored in global scratch buffers instead of
// stack locals. The combined size of these record arrays is large enough to be
// risky on a default Windows thread stack during process teardown.
LoadFileRecord g_flush_active_records[kMaxTrackedLoadFiles] = {};
LoadPathAggregate g_flush_path_aggregate_records[kMaxTrackedLoadPaths] = {};
LoadRegionAggregate g_flush_region_aggregate_records[kMaxTrackedLoadRegions] = {};
CachedMapWindow g_cached_map_windows[kMaxCachedMapWindows] = {};
CachedClientView g_cached_client_views[kMaxCachedClientViews] = {};
LoadProfilerTotals g_load_profiler_totals = {};
DWORD g_system_allocation_granularity = 0;

constexpr UiDebugEntry kUiDebugEntries[] = {
    {"enable_ui_scaling", kRvaEnableUiScaling, ConfigValueKind::Bool},
    {"ui_rect_override", kRvaUiRectOverride, ConfigValueKind::Bool},
    {"ui_x", kRvaUiX, ConfigValueKind::Float},
    {"ui_y", kRvaUiY, ConfigValueKind::Float},
    {"ui_width", kRvaUiWidth, ConfigValueKind::Float},
    {"ui_height", kRvaUiHeight, ConfigValueKind::Float},
    {"ui_rect_left", kRvaUiRectLeft, ConfigValueKind::Float},
    {"ui_rect_top", kRvaUiRectTop, ConfigValueKind::Float},
    {"ui_rect_width", kRvaUiRectWidth, ConfigValueKind::Float},
    {"ui_rect_height", kRvaUiRectHeight, ConfigValueKind::Float},
};

RECT GetMonitorRectForWindow(HWND hwnd) noexcept;
SIZE GetUiTargetSize(HWND hwnd) noexcept;
void ApplyUiScalingForWindow(HWND hwnd);
LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);
LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param);

bool InstallCodeDetour(void* target, void* hook, std::size_t patch_length, void** trampoline_out);

std::wstring GetConfigPath() {
    return GetModuleDirectory() + L"\\shogun2_mod.ini";
}

float ClampUiScale(float value) noexcept {
    return std::clamp(value, kUiScaleMin, kUiScaleMax);
}

float ClampUiScaleStep(float value) noexcept {
    return std::clamp(value, kUiScaleStepMin, kUiScaleStepMax);
}

bool ReadIniBool(const wchar_t* section, const wchar_t* key, bool default_value, const std::wstring& path) {
    return GetPrivateProfileIntW(section, key, default_value ? 1 : 0, path.c_str()) != 0;
}

float ReadIniFloat(const wchar_t* section, const wchar_t* key, float default_value, const std::wstring& path) {
    wchar_t buffer[64] = {};
    wchar_t fallback[64] = {};
    (void)swprintf_s(fallback, std::size(fallback), L"%.3f", default_value);
    const DWORD length = GetPrivateProfileStringW(section, key, fallback, buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (length == 0) {
        return default_value;
    }

    wchar_t* parse_end = nullptr;
    const double parsed = wcstod(buffer, &parse_end);
    if (parse_end == buffer || !std::isfinite(parsed)) {
        return default_value;
    }

    return static_cast<float>(parsed);
}

DWORD ReadIniDword(const wchar_t* section, const wchar_t* key, DWORD default_value, const std::wstring& path) {
    return static_cast<DWORD>(GetPrivateProfileIntW(section, key, static_cast<int>(default_value), path.c_str()));
}

DWORD ClampLoadCacheWindowMb(DWORD value) noexcept {
    return std::clamp(value, kLoadCacheWindowMbMin, kLoadCacheWindowMbMax);
}

DWORD ClampLoadCacheMinFileMb(DWORD value) noexcept {
    return std::clamp(value, kLoadCacheMinFileMbMin, kLoadCacheMinFileMbMax);
}

DWORD ClampLoadCacheMaxWindows(DWORD value) noexcept {
    return std::clamp(value, kLoadCacheMaxWindowsMin, kLoadCacheMaxWindowsMax);
}

DWORD ClampPrewarmDelayMs(DWORD value) noexcept {
    return (std::min)(value, kPrewarmDelayMsMax);
}

DWORD ClampPrewarmChunkMb(DWORD value) noexcept {
    return std::clamp(value, kPrewarmChunkMbMin, kPrewarmChunkMbMax);
}

LoadConfig LoadLoadConfig() {
    LoadConfig config = {};
    const std::wstring path = GetConfigPath();

    config.map_cache_enabled = ReadIniBool(L"load", L"map_cache", false, path);
    config.random_access_hint = ReadIniBool(L"load", L"random_access_hint", false, path);
    config.prewarm_hot_packs = ReadIniBool(L"load", L"prewarm_hot_packs", true, path);
    config.prewarm_hot_regions = ReadIniBool(L"load", L"prewarm_hot_regions", true, path);
    config.pin_shaders_pack = ReadIniBool(L"load", L"pin_shaders_pack", true, path);
    config.cache_window_mb = ClampLoadCacheWindowMb(ReadIniDword(L"load", L"cache_window_mb", kLoadCacheWindowMbDefault, path));
    config.cache_min_file_mb =
        ClampLoadCacheMinFileMb(ReadIniDword(L"load", L"cache_min_file_mb", kLoadCacheMinFileMbDefault, path));
    config.max_cached_windows =
        ClampLoadCacheMaxWindows(ReadIniDword(L"load", L"max_cached_windows", kLoadCacheMaxWindowsDefault, path));
    config.prewarm_delay_ms = ClampPrewarmDelayMs(ReadIniDword(L"load", L"prewarm_delay_ms", kPrewarmDelayMsDefault, path));
    config.prewarm_chunk_mb = ClampPrewarmChunkMb(ReadIniDword(L"load", L"prewarm_chunk_mb", kPrewarmChunkMbDefault, path));

    Log(
        "Loaded load.map_cache=%u random_access_hint=%u prewarm_hot_packs=%u prewarm_hot_regions=%u pin_shaders_pack=%u cache_window_mb=%lu cache_min_file_mb=%lu max_cached_windows=%lu prewarm_delay_ms=%lu prewarm_chunk_mb=%lu from %ls",
        config.map_cache_enabled ? 1u : 0u,
        config.random_access_hint ? 1u : 0u,
        config.prewarm_hot_packs ? 1u : 0u,
        config.prewarm_hot_regions ? 1u : 0u,
        config.pin_shaders_pack ? 1u : 0u,
        static_cast<unsigned long>(config.cache_window_mb),
        static_cast<unsigned long>(config.cache_min_file_mb),
        static_cast<unsigned long>(config.max_cached_windows),
        static_cast<unsigned long>(config.prewarm_delay_ms),
        static_cast<unsigned long>(config.prewarm_chunk_mb),
        path.c_str());
    return config;
}

void EnsureLoadConfigLoaded() {
    if (g_load_config_loaded) {
        return;
    }

    g_load_config = LoadLoadConfig();
    g_load_config_loaded = true;
}

UiScaleConfig LoadUiScaleConfig() {
    UiScaleConfig config = {};
    const std::wstring path = GetConfigPath();

    wchar_t buffer[64] = {};
    const DWORD length = GetPrivateProfileStringW(L"ui", L"scale", L"", buffer, static_cast<DWORD>(std::size(buffer)), path.c_str());
    if (length == 0) {
        config.scale = 1.0f;
        config.step = kUiScaleStepDefault;
        config.hotkeys_enabled = false;
        config.overlay_enabled = false;
        Log("UI scale config not present in %ls; using defaults", path.c_str());
        return config;
    }

    wchar_t* parse_end = nullptr;
    const double parsed = wcstod(buffer, &parse_end);
    if (parse_end == buffer || !std::isfinite(parsed)) {
        config.scale = 1.0f;
        config.step = kUiScaleStepDefault;
        config.hotkeys_enabled = false;
        config.overlay_enabled = false;
        Log("Invalid ui.scale value in %ls: %ls; using defaults", path.c_str(), buffer);
        return config;
    }

    config.present = true;
    config.scale = ClampUiScale(static_cast<float>(parsed));
    config.step = ClampUiScaleStep(ReadIniFloat(L"ui", L"step", kUiScaleStepDefault, path));
    config.hotkeys_enabled = ReadIniBool(L"ui", L"hotkeys", false, path);
    config.overlay_enabled = ReadIniBool(L"ui", L"overlay", false, path);
    Log(
        "Loaded ui.scale=%.3f step=%.3f hotkeys=%u overlay=%u from %ls",
        config.scale,
        config.step,
        config.hotkeys_enabled ? 1u : 0u,
        config.overlay_enabled ? 1u : 0u,
        path.c_str());
    return config;
}

void EnsureUiScaleConfigLoaded() {
    if (g_ui_scale_config_loaded) {
        return;
    }

    g_ui_scale_config = LoadUiScaleConfig();
    g_ui_scale_config_loaded = true;
}

void SaveUiScaleConfig() {
    EnsureUiScaleConfigLoaded();

    const std::wstring path = GetConfigPath();
    wchar_t scale_buffer[32] = {};
    wchar_t step_buffer[32] = {};
    (void)swprintf_s(scale_buffer, std::size(scale_buffer), L"%.2f", g_ui_scale_config.scale);
    (void)swprintf_s(step_buffer, std::size(step_buffer), L"%.2f", g_ui_scale_config.step);

    (void)WritePrivateProfileStringW(L"ui", L"scale", scale_buffer, path.c_str());
    (void)WritePrivateProfileStringW(L"ui", L"step", step_buffer, path.c_str());
    (void)WritePrivateProfileStringW(L"ui", L"hotkeys", g_ui_scale_config.hotkeys_enabled ? L"1" : L"0", path.c_str());
    (void)WritePrivateProfileStringW(L"ui", L"overlay", g_ui_scale_config.overlay_enabled ? L"1" : L"0", path.c_str());
}

std::uint8_t* GetEmpireRetailBase() noexcept {
    HMODULE module = GetModuleHandleW(L"empire.retail.dll");
    return reinterpret_cast<std::uint8_t*>(module);
}

void MarkConfigDirty(std::uint8_t* object) noexcept {
    object[kConfigDirtyOffset] = 1;
}

bool ReadBoolConfig(std::uint8_t* base, std::uintptr_t rva) noexcept {
    if (base == nullptr) {
        return false;
    }

    const std::uint8_t* object = base + rva;
    return object[kConfigValueOffset] != 0;
}

float ReadFloatConfig(std::uint8_t* base, std::uintptr_t rva) noexcept {
    if (base == nullptr) {
        return 0.0f;
    }

    float value = 0.0f;
    const std::uint8_t* object = base + rva;
    std::memcpy(&value, object + kConfigValueOffset, sizeof(value));
    return value;
}

void WriteBoolConfig(std::uint8_t* base, std::uintptr_t rva, bool value) noexcept {
    if (base == nullptr) {
        return;
    }

    std::uint8_t* object = base + rva;
    MarkConfigDirty(object);
    object[kConfigValueOffset] = value ? 1 : 0;
}

void WriteFloatConfig(std::uint8_t* base, std::uintptr_t rva, float value) noexcept {
    if (base == nullptr) {
        return;
    }

    std::uint8_t* object = base + rva;
    MarkConfigDirty(object);
    *reinterpret_cast<float*>(object + kConfigValueOffset) = value;
}

void LogConfigState(std::uint8_t* base, const UiDebugEntry& entry) {
    if (base == nullptr) {
        return;
    }

    std::uint8_t* object = base + entry.rva;
    const unsigned int dirty = object[kConfigDirtyOffset];
    const unsigned int byte58 = object[0x58];
    const unsigned int byte59 = object[0x59];
    const unsigned int byte5a = object[0x5a];
    const unsigned int byte5b = object[0x5b];
    const unsigned int byte5c = object[0x5c];
    const unsigned int byte5d = object[0x5d];
    const unsigned int byte5e = object[0x5e];
    const unsigned int byte5f = object[0x5f];

    if (entry.kind == ConfigValueKind::Bool) {
        const bool value = ReadBoolConfig(base, entry.rva);
        Log(
            "[ui-debug] %s addr=%p rva=0x%08Ix dirty=%u value=%u bytes[58..5f]=%02x %02x %02x %02x %02x %02x %02x %02x",
            entry.name,
            object,
            entry.rva,
            dirty,
            value ? 1u : 0u,
            byte58,
            byte59,
            byte5a,
            byte5b,
            byte5c,
            byte5d,
            byte5e,
            byte5f);
        return;
    }

    std::uint32_t raw = 0;
    std::memcpy(&raw, object + kConfigValueOffset, sizeof(raw));
    const float value = ReadFloatConfig(base, entry.rva);
    Log(
        "[ui-debug] %s addr=%p rva=0x%08Ix dirty=%u value=%.3f raw=0x%08lx bytes[58..5f]=%02x %02x %02x %02x %02x %02x %02x %02x",
        entry.name,
        object,
        entry.rva,
        dirty,
        value,
        static_cast<unsigned long>(raw),
        byte58,
        byte59,
        byte5a,
        byte5b,
        byte5c,
        byte5d,
        byte5e,
        byte5f);
}

void DumpUiScalingState(const char* reason) {
    std::uint8_t* base = GetEmpireRetailBase();
    if (base == nullptr) {
        Log("[ui-debug] %s skipped because empire.retail.dll is not loaded", reason);
        return;
    }

    Log("[ui-debug] %s empire_base=%p", reason, base);
    for (const UiDebugEntry& entry : kUiDebugEntries) {
        LogConfigState(base, entry);
    }
}

bool ShouldLogLimited(LONG* counter, LONG limit) {
    const LONG current = InterlockedIncrement(counter);
    return current <= limit;
}

DWORD GetSystemAllocationGranularity() {
    if (g_system_allocation_granularity != 0) {
        return g_system_allocation_granularity;
    }

    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    g_system_allocation_granularity = system_info.dwAllocationGranularity != 0 ? system_info.dwAllocationGranularity : 65536u;
    return g_system_allocation_granularity;
}

ULONGLONG ComposeFileOffset(DWORD high, DWORD low) noexcept {
    return (static_cast<ULONGLONG>(high) << 32) | static_cast<ULONGLONG>(low);
}

ULONGLONG AlignDownToMultiple(ULONGLONG value, ULONGLONG multiple) noexcept {
    if (multiple == 0) {
        return value;
    }

    return value - (value % multiple);
}

ULONGLONG AlignUpToMultiple(ULONGLONG value, ULONGLONG multiple) noexcept {
    if (multiple == 0) {
        return value;
    }

    const ULONGLONG remainder = value % multiple;
    return remainder == 0 ? value : (value + multiple - remainder);
}

ULONGLONG MegabytesToBytes(DWORD megabytes) noexcept {
    return static_cast<ULONGLONG>(megabytes) * 1024ull * 1024ull;
}

RawMappedViewRecord* FindRawMappedViewRecordLocked(void* base) noexcept {
    if (base == nullptr) {
        return nullptr;
    }

    for (RawMappedViewRecord& record : g_raw_mapped_view_records) {
        if (record.active && record.base == base) {
            return &record;
        }
    }

    return nullptr;
}

RawMappedViewRecord* AllocateRawMappedViewRecordLocked() noexcept {
    for (RawMappedViewRecord& record : g_raw_mapped_view_records) {
        if (!record.active) {
            record = {};
            record.active = true;
            return &record;
        }
    }

    return nullptr;
}

CachedMapWindow* FindCachedMapWindowLocked(
    HANDLE mapping_handle,
    DWORD desired_access,
    ULONGLONG request_offset,
    ULONGLONG request_bytes) noexcept {
    for (CachedMapWindow& window : g_cached_map_windows) {
        if (!window.active || !window.reusable) {
            continue;
        }

        if (window.mapping_handle != mapping_handle || window.desired_access != desired_access) {
            continue;
        }

        const ULONGLONG window_end = window.window_offset + window.mapped_bytes;
        const ULONGLONG request_end = request_offset + request_bytes;
        if (request_offset >= window.window_offset && request_end <= window_end) {
            return &window;
        }
    }

    return nullptr;
}

std::size_t FindCachedMapWindowIndexLocked(const CachedMapWindow* target) noexcept {
    if (target == nullptr) {
        return kInvalidCachedWindowIndex;
    }

    return static_cast<std::size_t>(target - g_cached_map_windows);
}

CachedClientView* FindCachedClientViewLocked(void* client_base) noexcept {
    if (client_base == nullptr) {
        return nullptr;
    }

    for (CachedClientView& view : g_cached_client_views) {
        if (view.active && view.client_base == client_base) {
            return &view;
        }
    }

    return nullptr;
}

CachedClientView* AllocateCachedClientViewLocked() noexcept {
    for (CachedClientView& view : g_cached_client_views) {
        if (!view.active) {
            view = {};
            view.active = true;
            return &view;
        }
    }

    return nullptr;
}

std::size_t CountActiveCachedWindowsLocked() noexcept {
    std::size_t count = 0;
    for (const CachedMapWindow& window : g_cached_map_windows) {
        if (window.active) {
            ++count;
        }
    }
    return count;
}

CachedMapWindow* FindEvictableCachedWindowLocked() noexcept {
    CachedMapWindow* best = nullptr;
    for (CachedMapWindow& window : g_cached_map_windows) {
        if (!window.active || window.live_client_views != 0) {
            continue;
        }

        if (best == nullptr || window.last_touch_tick < best->last_touch_tick) {
            best = &window;
        }
    }

    return best;
}

CachedMapWindow* AllocateCachedMapWindowSlotLocked(std::size_t* out_index) noexcept {
    for (std::size_t index = 0; index < std::size(g_cached_map_windows); ++index) {
        if (!g_cached_map_windows[index].active) {
            if (out_index != nullptr) {
                *out_index = index;
            }
            return &g_cached_map_windows[index];
        }
    }

    if (out_index != nullptr) {
        *out_index = kInvalidCachedWindowIndex;
    }
    return nullptr;
}

void AttributeLogicalMapCallLocked(LoadFileRecord* record, ULONGLONG requested_bytes, bool cache_hit, bool cache_counted) {
    g_load_profiler_totals.map_view_calls++;
    g_load_profiler_totals.mapped_bytes += requested_bytes;
    if (cache_counted) {
        if (cache_hit) {
            g_load_profiler_totals.map_cache_hits++;
        } else {
            g_load_profiler_totals.map_cache_misses++;
        }
    }

    if (record != nullptr) {
        record->map_view_calls++;
        record->total_mapped_bytes += requested_bytes;
        if (requested_bytes > record->max_map_view_size) {
            record->max_map_view_size = requested_bytes;
        }
        if (cache_counted && cache_hit) {
            record->cached_map_hits++;
        }
    }
}

void AttributeMapApiCallLocked(LoadFileRecord* record, ULONGLONG elapsed_ms) {
    g_load_profiler_totals.map_api_calls++;
    g_load_profiler_totals.map_api_time_ms += elapsed_ms;
    if (record != nullptr) {
        record->total_map_api_time_ms += elapsed_ms;
    }
}

void AttributeUnmapApiCallLocked(LoadFileRecord* record, ULONGLONG elapsed_ms) {
    g_load_profiler_totals.unmap_api_calls++;
    g_load_profiler_totals.unmap_api_time_ms += elapsed_ms;
    if (record != nullptr) {
        record->total_unmap_api_time_ms += elapsed_ms;
    }
}

void AttributeLogicalUnmapLocked(LoadFileRecord* record) {
    g_load_profiler_totals.unmap_view_calls++;
    if (record != nullptr) {
        record->unmap_view_calls++;
    }
}

void EnsureLoadProfilerStarted() {
    if (!g_load_profiler_totals.started) {
        g_load_profiler_totals.started = true;
        g_load_profiler_totals.start_tick = GetTickCount64();
    }

    EnsureLoadConfigLoaded();
    (void)GetSystemAllocationGranularity();
}

std::wstring AppendRelativeModulePath(const wchar_t* relative_path) {
    std::wstring result = GetModuleDirectory();
    if (!result.empty() && result.back() != L'\\' && result.back() != L'/') {
        result.push_back(L'\\');
    }
    if (relative_path != nullptr) {
        result.append(relative_path);
    }
    return result;
}

bool PrewarmFileIntoSystemCache(const std::wstring& path, DWORD chunk_bytes) {
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log("[prewarm] skip path=%ls error=%lu", path.c_str(), GetLastError());
        }
        return false;
    }

    LARGE_INTEGER file_size = {};
    (void)GetFileSizeEx(file, &file_size);

    std::uint8_t* buffer = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, chunk_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (buffer == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log("[prewarm] alloc failed path=%ls chunk=%lu error=%lu", path.c_str(), static_cast<unsigned long>(chunk_bytes), error);
        }
        return false;
    }

    const ULONGLONG start_tick = GetTickCount64();
    ULONGLONG total_bytes = 0;
    DWORD chunk_counter = 0;
    for (;;) {
        DWORD bytes_read = 0;
        const BOOL read_ok = ReadFile(file, buffer, chunk_bytes, &bytes_read, nullptr);
        if (!read_ok) {
            const DWORD error = GetLastError();
            if (error != ERROR_HANDLE_EOF) {
                if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
                    Log("[prewarm] read failed path=%ls after=%llu error=%lu", path.c_str(), total_bytes, error);
                }
            }
            break;
        }

        if (bytes_read == 0) {
            break;
        }

        total_bytes += bytes_read;
        ++chunk_counter;
        if ((chunk_counter % 8u) == 0u) {
            Sleep(0);
        }
    }

    const ULONGLONG elapsed_ms = GetTickCount64() - start_tick;
    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(file);

    if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
        Log(
            "[prewarm] done bytes=%llu time=%llums fileSize=%lld path=%ls",
            total_bytes,
            elapsed_ms,
            file_size.QuadPart,
            path.c_str());
    }

    return total_bytes > 0;
}

// Profile-driven region prewarm is the safer follow-up to the failed generic
// mapping-cache experiment. Instead of changing the game's mapping behavior, we
// simply read a few hot archive ranges in the background so Windows can keep
// them in the file cache before the engine asks for them.
bool PrewarmFileRegionIntoSystemCache(
    const std::wstring& path,
    ULONGLONG region_offset,
    ULONGLONG region_length,
    DWORD chunk_bytes) {
    if (region_length == 0) {
        return false;
    }

    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log("[region-prewarm] open failed path=%ls error=%lu", path.c_str(), GetLastError());
        }
        return false;
    }

    LARGE_INTEGER file_size = {};
    (void)GetFileSizeEx(file, &file_size);
    if (file_size.QuadPart <= 0 || region_offset >= static_cast<ULONGLONG>(file_size.QuadPart)) {
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log(
                "[region-prewarm] skip path=%ls offset=0x%llx length=%llu fileSize=%lld",
                path.c_str(),
                region_offset,
                region_length,
                file_size.QuadPart);
        }
        return false;
    }

    ULONGLONG remaining = region_length;
    const ULONGLONG file_remaining = static_cast<ULONGLONG>(file_size.QuadPart) - region_offset;
    if (remaining > file_remaining) {
        remaining = file_remaining;
    }

    LARGE_INTEGER seek = {};
    seek.QuadPart = static_cast<LONGLONG>(region_offset);
    if (!SetFilePointerEx(file, seek, nullptr, FILE_BEGIN)) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log(
                "[region-prewarm] seek failed path=%ls offset=0x%llx error=%lu",
                path.c_str(),
                region_offset,
                error);
        }
        return false;
    }

    std::uint8_t* buffer = static_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, chunk_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    if (buffer == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
            Log(
                "[region-prewarm] alloc failed path=%ls chunk=%lu error=%lu",
                path.c_str(),
                static_cast<unsigned long>(chunk_bytes),
                error);
        }
        return false;
    }

    const ULONGLONG start_tick = GetTickCount64();
    ULONGLONG total_bytes = 0;
    DWORD chunk_counter = 0;
    while (remaining != 0) {
        const DWORD request_bytes =
            remaining > chunk_bytes ? chunk_bytes : static_cast<DWORD>(remaining);
        DWORD bytes_read = 0;
        if (!ReadFile(file, buffer, request_bytes, &bytes_read, nullptr)) {
            const DWORD error = GetLastError();
            if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
                Log(
                    "[region-prewarm] read failed path=%ls offset=0x%llx after=%llu error=%lu",
                    path.c_str(),
                    region_offset,
                    total_bytes,
                    error);
            }
            break;
        }

        if (bytes_read == 0) {
            break;
        }

        total_bytes += bytes_read;
        remaining -= bytes_read;
        ++chunk_counter;
        if ((chunk_counter % 8u) == 0u) {
            Sleep(0);
        }
    }

    const ULONGLONG elapsed_ms = GetTickCount64() - start_tick;
    VirtualFree(buffer, 0, MEM_RELEASE);
    CloseHandle(file);

    if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
        Log(
            "[region-prewarm] done bytes=%llu offset=0x%llx length=%llu time=%llums path=%ls",
            total_bytes,
            region_offset,
            region_length,
            elapsed_ms,
            path.c_str());
    }

    return total_bytes > 0;
}

void ReleasePinnedShadersPack() {
    if (g_shader_pin_view != nullptr) {
        UnmapViewOfFile(g_shader_pin_view);
        g_shader_pin_view = nullptr;
    }
    if (g_shader_pin_mapping != nullptr) {
        CloseHandle(g_shader_pin_mapping);
        g_shader_pin_mapping = nullptr;
    }
    if (g_shader_pin_file != nullptr && g_shader_pin_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_shader_pin_file);
        g_shader_pin_file = nullptr;
    }
    g_shader_pin_size = 0;
}

bool PinShadersPackForSession() {
    if (g_shader_pin_view != nullptr) {
        return true;
    }

    const std::wstring path = AppendRelativeModulePath(L"data\\shaders.pack");
    HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_RANDOM_ACCESS,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (ShouldLogLimited(&g_load_shader_pin_log_count, kLoadShaderPinLogLimit)) {
            Log("[shader-pin] open failed path=%ls error=%lu", path.c_str(), GetLastError());
        }
        return false;
    }

    LARGE_INTEGER file_size = {};
    if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_shader_pin_log_count, kLoadShaderPinLogLimit)) {
            Log("[shader-pin] size failed path=%ls error=%lu", path.c_str(), error);
        }
        return false;
    }

    HANDLE mapping = CreateFileMappingW(file, nullptr, PAGE_READONLY, 0, 0, nullptr);
    if (mapping == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_shader_pin_log_count, kLoadShaderPinLogLimit)) {
            Log("[shader-pin] mapping failed path=%ls error=%lu", path.c_str(), error);
        }
        return false;
    }

    void* view = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
    if (view == nullptr) {
        const DWORD error = GetLastError();
        CloseHandle(mapping);
        CloseHandle(file);
        if (ShouldLogLimited(&g_load_shader_pin_log_count, kLoadShaderPinLogLimit)) {
            Log("[shader-pin] view failed path=%ls error=%lu", path.c_str(), error);
        }
        return false;
    }

    SYSTEM_INFO system_info = {};
    GetSystemInfo(&system_info);
    const std::size_t page_size = system_info.dwPageSize != 0 ? system_info.dwPageSize : 4096u;

    // Touch one byte per page so the mapped file data is faulted into memory.
    // Keeping this view alive for the session is the actual "shader pin":
    // later game mappings of the same file can reuse these already-hot pages.
    const ULONGLONG start_tick = GetTickCount64();
    volatile std::uint8_t page_checksum = 0;
    const std::uint8_t* bytes = static_cast<const std::uint8_t*>(view);
    const std::size_t total_bytes = static_cast<std::size_t>(file_size.QuadPart);
    for (std::size_t offset = 0; offset < total_bytes; offset += page_size) {
        page_checksum ^= bytes[offset];
    }
    if (total_bytes != 0) {
        page_checksum ^= bytes[total_bytes - 1];
    }
    const ULONGLONG elapsed_ms = GetTickCount64() - start_tick;

    g_shader_pin_file = file;
    g_shader_pin_mapping = mapping;
    g_shader_pin_view = view;
    g_shader_pin_size = static_cast<ULONGLONG>(file_size.QuadPart);

    if (ShouldLogLimited(&g_load_shader_pin_log_count, kLoadShaderPinLogLimit)) {
        Log(
            "[shader-pin] active bytes=%llu time=%llums checksum=%u path=%ls",
            g_shader_pin_size,
            elapsed_ms,
            static_cast<unsigned int>(page_checksum),
            path.c_str());
    }

    return true;
}

DWORD WINAPI HotPackPrewarmThread(void*) {
    EnsureLoadConfigLoaded();

    if (g_load_config.prewarm_delay_ms != 0) {
        Sleep(g_load_config.prewarm_delay_ms);
    }

    (void)SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);

    // These are the highest-confidence low-risk candidates from the profiler:
    // tiny or modest packs with disproportionately high mapping churn. Reading
    // them once sequentially is cheap, and it avoids touching the much riskier
    // multi-gigabyte archives until we have stronger evidence for a better
    // strategy there.
    static constexpr const wchar_t* kHotPackPaths[] = {
        L"data\\boot.pack",
        L"data\\bp_orig.pack",
        L"data\\large_font.pack",
    };
    struct HotRegionSpec {
        const wchar_t* relative_path;
        ULONGLONG offset;
        ULONGLONG length;
    };

    // These ranges are taken from the new region-bucket profiler. We only
    // prewarm the clearest clustered async-read hotspot for now:
    //
    // - `data.pack` around 0x74000000-0x75000000
    // - `data.pack` around 0x7a000000-0x82000000
    // - `data.pack` around 0x88000000-0x89000000
    //
    // This keeps the experiment focused on one file with a real contiguous
    // hotspot instead of blindly touching many scattered model-pack regions.
    static constexpr HotRegionSpec kHotRegions[] = {
        {L"data\\data.pack", 0x74000000ull, 0x02000000ull},
        {L"data\\data.pack", 0x7a000000ull, 0x09000000ull},
        {L"data\\data.pack", 0x88000000ull, 0x02000000ull},
    };

    const DWORD chunk_bytes = MegabytesToBytes(g_load_config.prewarm_chunk_mb) > MAXDWORD
        ? MAXDWORD
        : static_cast<DWORD>(MegabytesToBytes(g_load_config.prewarm_chunk_mb));

    if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
        Log(
            "[prewarm] start delay=%lu chunk=%lu",
            static_cast<unsigned long>(g_load_config.prewarm_delay_ms),
            static_cast<unsigned long>(chunk_bytes));
    }

    for (const wchar_t* relative_path : kHotPackPaths) {
        (void)PrewarmFileIntoSystemCache(AppendRelativeModulePath(relative_path), chunk_bytes);
    }

    if (g_load_config.prewarm_hot_regions) {
        for (const HotRegionSpec& region : kHotRegions) {
            (void)PrewarmFileRegionIntoSystemCache(
                AppendRelativeModulePath(region.relative_path),
                region.offset,
                region.length,
                chunk_bytes);
        }
    }

    if (g_load_config.pin_shaders_pack) {
        (void)PinShadersPackForSession();
    }

    if (ShouldLogLimited(&g_load_prewarm_log_count, kLoadPrewarmLogLimit)) {
        Log("[prewarm] complete");
    }
    return 0;
}

void EnsureHotPackPrewarmStarted() {
    EnsureLoadConfigLoaded();
    if (!g_load_config.prewarm_hot_packs && !g_load_config.prewarm_hot_regions && !g_load_config.pin_shaders_pack) {
        return;
    }

    if (InterlockedCompareExchange(&g_load_prewarm_started, 1, 0) != 0) {
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, &HotPackPrewarmThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        InterlockedExchange(&g_load_prewarm_started, 0);
        Log("[prewarm] failed to create worker thread: %lu", GetLastError());
        return;
    }

    CloseHandle(thread);
}

const wchar_t* GetPathExtension(const wchar_t* path) noexcept {
    if (path == nullptr || path[0] == L'\0') {
        return nullptr;
    }

    const wchar_t* last_slash = wcsrchr(path, L'\\');
    const wchar_t* alt_slash = wcsrchr(path, L'/');
    if (alt_slash != nullptr && (last_slash == nullptr || alt_slash > last_slash)) {
        last_slash = alt_slash;
    }

    const wchar_t* dot = wcsrchr(path, L'.');
    if (dot == nullptr) {
        return nullptr;
    }

    if (last_slash != nullptr && dot < last_slash) {
        return nullptr;
    }

    return dot;
}

bool PathHasExtension(const wchar_t* path, const wchar_t* extension) noexcept {
    const wchar_t* dot = GetPathExtension(path);
    return dot != nullptr && extension != nullptr && _wcsicmp(dot, extension) == 0;
}

bool IsInterestingLoadPath(const wchar_t* path) noexcept {
    return PathHasExtension(path, L".pack") ||
        PathHasExtension(path, L".idx") ||
        PathHasExtension(path, L".animpack") ||
        PathHasExtension(path, L".loc");
}

bool IsPackLikeLoadPath(const wchar_t* path) noexcept {
    return PathHasExtension(path, L".pack") || PathHasExtension(path, L".animpack");
}

void CopyPathForRecord(wchar_t (&buffer)[kMaxTrackedLoadPathChars], const wchar_t* path) noexcept {
    if (path == nullptr) {
        buffer[0] = L'\0';
        return;
    }

    (void)wcsncpy_s(buffer, std::size(buffer), path, _TRUNCATE);
}

const wchar_t* GetPathLeafName(const wchar_t* path) noexcept {
    if (path == nullptr || path[0] == L'\0') {
        return L"(unknown)";
    }

    const wchar_t* last_slash = wcsrchr(path, L'\\');
    const wchar_t* alt_slash = wcsrchr(path, L'/');
    if (alt_slash != nullptr && (last_slash == nullptr || alt_slash > last_slash)) {
        last_slash = alt_slash;
    }

    return (last_slash != nullptr && last_slash[1] != L'\0') ? (last_slash + 1) : path;
}

void CopyRegionLabelForRecord(wchar_t (&buffer)[kMaxTrackedLoadRegionLabelChars], const wchar_t* path) noexcept {
    (void)wcsncpy_s(buffer, std::size(buffer), GetPathLeafName(path), _TRUNCATE);
}

wchar_t FoldAsciiLower(wchar_t value) noexcept {
    if (value >= L'A' && value <= L'Z') {
        return static_cast<wchar_t>(value - L'A' + L'a');
    }
    return value;
}

std::uint64_t HashPathForRegion(const wchar_t* path) noexcept {
    if (path == nullptr) {
        return 0;
    }

    std::uint64_t hash = 1469598103934665603ull;
    for (const wchar_t* cursor = path; *cursor != L'\0'; ++cursor) {
        hash ^= static_cast<std::uint64_t>(FoldAsciiLower(*cursor));
        hash *= 1099511628211ull;
    }
    return hash;
}

LoadFileRecord* FindLoadFileRecordLocked(HANDLE handle) noexcept {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    for (LoadFileRecord& record : g_load_file_records) {
        if (record.active && record.handle == handle) {
            return &record;
        }
    }

    return nullptr;
}

LoadFileRecord* AllocateLoadFileRecordLocked() noexcept {
    for (LoadFileRecord& record : g_load_file_records) {
        if (!record.active) {
            record = {};
            record.active = true;
            return &record;
        }
    }

    return nullptr;
}

LoadMappingRecord* FindLoadMappingRecordLocked(HANDLE handle) noexcept {
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return nullptr;
    }

    for (LoadMappingRecord& record : g_load_mapping_records) {
        if (record.active && record.handle == handle) {
            return &record;
        }
    }

    return nullptr;
}

LoadMappingRecord* AllocateLoadMappingRecordLocked() noexcept {
    for (LoadMappingRecord& record : g_load_mapping_records) {
        if (!record.active) {
            record = {};
            record.active = true;
            return &record;
        }
    }

    return nullptr;
}

LoadPathAggregate* FindLoadPathAggregateLocked(
    LoadPathAggregate* entries,
    std::size_t capacity,
    const wchar_t* path) noexcept {
    if (entries == nullptr || capacity == 0 || path == nullptr || path[0] == L'\0') {
        return nullptr;
    }

    for (std::size_t index = 0; index < capacity; ++index) {
        if (!entries[index].active) {
            continue;
        }

        if (_wcsicmp(entries[index].path, path) == 0) {
            return &entries[index];
        }
    }

    return nullptr;
}

LoadPathAggregate* AllocateLoadPathAggregateLocked(LoadPathAggregate* entries, std::size_t capacity) noexcept {
    if (entries == nullptr || capacity == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < capacity; ++index) {
        if (!entries[index].active) {
            entries[index] = {};
            entries[index].active = true;
            return &entries[index];
        }
    }

    return nullptr;
}

void AccumulateIntoLoadPathAggregate(LoadPathAggregate* aggregate, const LoadFileRecord& record) noexcept {
    if (aggregate == nullptr) {
        return;
    }

    aggregate->interesting = aggregate->interesting || record.interesting;
    aggregate->handles++;
    aggregate->read_calls += record.read_calls;
    aggregate->read_bytes += record.total_read_bytes;
    aggregate->read_requested_bytes += record.total_read_requested_bytes;
    aggregate->read_ex_calls += record.read_ex_calls;
    aggregate->read_ex_requested_bytes += record.total_read_ex_requested_bytes;
    aggregate->pending_read_calls += record.pending_read_calls;
    aggregate->seek_calls += record.seek_calls;
    aggregate->map_view_calls += record.map_view_calls;
    aggregate->cached_map_hits += record.cached_map_hits;
    aggregate->unmap_view_calls += record.unmap_view_calls;
    aggregate->mapped_bytes += record.total_mapped_bytes;
    aggregate->map_api_time_ms += record.total_map_api_time_ms;
    aggregate->unmap_api_time_ms += record.total_unmap_api_time_ms;
    if (record.random_access_hint_applied) {
        aggregate->random_access_hint_opens++;
    }
    if (record.max_read_size > aggregate->max_read_size) {
        aggregate->max_read_size = record.max_read_size;
    }
    if (record.max_map_view_size > aggregate->max_map_view_size) {
        aggregate->max_map_view_size = record.max_map_view_size;
    }
    if (record.file_size.QuadPart > aggregate->file_size.QuadPart) {
        aggregate->file_size = record.file_size;
    }
}

void FoldLoadFileRecordIntoAggregateArray(
    LoadPathAggregate* entries,
    std::size_t capacity,
    const LoadFileRecord& record,
    ULONGLONG* dropped_counter) noexcept {
    if (entries == nullptr || capacity == 0) {
        return;
    }

    const wchar_t* path = record.path[0] != L'\0' ? record.path : L"(unknown)";
    LoadPathAggregate* aggregate = FindLoadPathAggregateLocked(entries, capacity, path);
    if (aggregate == nullptr) {
        aggregate = AllocateLoadPathAggregateLocked(entries, capacity);
        if (aggregate == nullptr) {
            if (dropped_counter != nullptr) {
                (*dropped_counter)++;
            }
            return;
        }

        CopyPathForRecord(aggregate->path, path);
    }

    AccumulateIntoLoadPathAggregate(aggregate, record);
}

LoadRegionAggregate* FindLoadRegionAggregateLocked(
    LoadRegionAggregate* entries,
    std::size_t capacity,
    std::uint64_t path_hash,
    ULONGLONG bucket_offset) noexcept {
    if (entries == nullptr || capacity == 0 || path_hash == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < capacity; ++index) {
        if (!entries[index].active) {
            continue;
        }

        if (entries[index].path_hash == path_hash && entries[index].bucket_offset == bucket_offset) {
            return &entries[index];
        }
    }

    return nullptr;
}

LoadRegionAggregate* AllocateLoadRegionAggregateLocked(LoadRegionAggregate* entries, std::size_t capacity) noexcept {
    if (entries == nullptr || capacity == 0) {
        return nullptr;
    }

    for (std::size_t index = 0; index < capacity; ++index) {
        if (!entries[index].active) {
            entries[index] = {};
            entries[index].active = true;
            return &entries[index];
        }
    }

    return nullptr;
}

void AttributeLoadRegionSpanLocked(
    const LoadFileRecord* record,
    ULONGLONG request_offset,
    ULONGLONG request_bytes,
    LoadRegionKind kind) noexcept {
    if (record == nullptr || request_bytes == 0 || record->path[0] == L'\0' || !IsPackLikeLoadPath(record->path)) {
        return;
    }

    const std::uint64_t path_hash = HashPathForRegion(record->path);
    if (path_hash == 0) {
        return;
    }

    const ULONGLONG end_offset = request_offset + request_bytes;
    ULONGLONG bucket_offset = AlignDownToMultiple(request_offset, kLoadRegionBucketBytes);
    while (bucket_offset < end_offset) {
        const ULONGLONG bucket_end = bucket_offset + kLoadRegionBucketBytes;
        const ULONGLONG span_start = (std::max)(request_offset, bucket_offset);
        const ULONGLONG span_end = (std::min)(end_offset, bucket_end);
        const ULONGLONG span_bytes = span_end > span_start ? (span_end - span_start) : 0;
        if (span_bytes == 0) {
            break;
        }

        LoadRegionAggregate* aggregate =
            FindLoadRegionAggregateLocked(g_load_region_aggregates, std::size(g_load_region_aggregates), path_hash, bucket_offset);
        if (aggregate == nullptr) {
            aggregate = AllocateLoadRegionAggregateLocked(g_load_region_aggregates, std::size(g_load_region_aggregates));
            if (aggregate == nullptr) {
                g_load_profiler_totals.dropped_region_records++;
                return;
            }

            aggregate->interesting = record->interesting;
            aggregate->path_hash = path_hash;
            aggregate->bucket_offset = bucket_offset;
            aggregate->bucket_size = kLoadRegionBucketBytes;
            CopyRegionLabelForRecord(aggregate->label, record->path);
        }

        if (kind == LoadRegionKind::MapView) {
            aggregate->map_view_calls++;
            aggregate->mapped_bytes += span_bytes;
        } else {
            aggregate->read_ex_calls++;
            aggregate->read_ex_requested_bytes += span_bytes;
        }

        bucket_offset = bucket_end;
    }
}

ULONGLONG ResolveMappedBytes(const LoadFileRecord* file_record, SIZE_T bytes_to_map) noexcept {
    if (bytes_to_map != 0) {
        return static_cast<ULONGLONG>(bytes_to_map);
    }

    if (file_record != nullptr && file_record->file_size.QuadPart > 0) {
        return static_cast<ULONGLONG>(file_record->file_size.QuadPart);
    }

    return 0;
}

void LogLoadFileSummary(const LoadFileRecord& record, const char* reason) {
    const ULONGLONG lifetime_ms = record.open_tick != 0 ? (GetTickCount64() - record.open_tick) : 0;
    Log(
        "[load] %s handle=%p life=%llums reads=%lu readBytes=%llu readReq=%llu readEx=%lu readExReq=%llu pending=%lu seeks=%lu maps=%lu cachedHits=%lu unmaps=%lu mappedBytes=%llu maxRead=%lu maxMap=%llu mapApi=%llums unmapApi=%llums flags=0x%08lx randomHint=%u fileSize=%lld path=%ls",
        reason,
        record.handle,
        lifetime_ms,
        static_cast<unsigned long>(record.read_calls),
        record.total_read_bytes,
        record.total_read_requested_bytes,
        static_cast<unsigned long>(record.read_ex_calls),
        record.total_read_ex_requested_bytes,
        static_cast<unsigned long>(record.pending_read_calls),
        static_cast<unsigned long>(record.seek_calls),
        static_cast<unsigned long>(record.map_view_calls),
        static_cast<unsigned long>(record.cached_map_hits),
        static_cast<unsigned long>(record.unmap_view_calls),
        record.total_mapped_bytes,
        static_cast<unsigned long>(record.max_read_size),
        record.max_map_view_size,
        record.total_map_api_time_ms,
        record.total_unmap_api_time_ms,
        record.flags_and_attributes,
        record.random_access_hint_applied ? 1u : 0u,
        record.file_size.QuadPart,
        record.path[0] != L'\0' ? record.path : L"(unknown)");
}

void LogLoadPathAggregateSummary(const LoadPathAggregate& aggregate, const char* reason, std::size_t rank) {
    Log(
        "[load] %s rank=%zu handles=%llu randomHintOpens=%llu reads=%llu readBytes=%llu readReq=%llu readEx=%llu readExReq=%llu pending=%llu seeks=%llu maps=%llu cachedHits=%llu unmaps=%llu mappedBytes=%llu maxRead=%lu maxMap=%llu mapApi=%llums unmapApi=%llums fileSize=%lld path=%ls",
        reason,
        rank,
        aggregate.handles,
        aggregate.random_access_hint_opens,
        aggregate.read_calls,
        aggregate.read_bytes,
        aggregate.read_requested_bytes,
        aggregate.read_ex_calls,
        aggregate.read_ex_requested_bytes,
        aggregate.pending_read_calls,
        aggregate.seek_calls,
        aggregate.map_view_calls,
        aggregate.cached_map_hits,
        aggregate.unmap_view_calls,
        aggregate.mapped_bytes,
        static_cast<unsigned long>(aggregate.max_read_size),
        aggregate.max_map_view_size,
        aggregate.map_api_time_ms,
        aggregate.unmap_api_time_ms,
        aggregate.file_size.QuadPart,
        aggregate.path[0] != L'\0' ? aggregate.path : L"(unknown)");
}

void LogLoadRegionAggregateSummary(const LoadRegionAggregate& aggregate, const char* reason, std::size_t rank) {
    Log(
        "[load] %s rank=%zu maps=%llu mappedBytes=%llu readEx=%llu readExReq=%llu bucketStart=0x%llx bucketSize=%llu file=%ls",
        reason,
        rank,
        aggregate.map_view_calls,
        aggregate.mapped_bytes,
        aggregate.read_ex_calls,
        aggregate.read_ex_requested_bytes,
        aggregate.bucket_offset,
        aggregate.bucket_size,
        aggregate.label[0] != L'\0' ? aggregate.label : L"(unknown)");
}

bool CompareLoadPathByMappedBytes(const LoadPathAggregate& left, const LoadPathAggregate& right) noexcept {
    if (left.active != right.active) {
        return left.active && !right.active;
    }
    if (left.mapped_bytes != right.mapped_bytes) {
        return left.mapped_bytes > right.mapped_bytes;
    }
    if (left.map_view_calls != right.map_view_calls) {
        return left.map_view_calls > right.map_view_calls;
    }
    return _wcsicmp(left.path, right.path) < 0;
}

bool CompareLoadPathByReadExBytes(const LoadPathAggregate& left, const LoadPathAggregate& right) noexcept {
    if (left.active != right.active) {
        return left.active && !right.active;
    }
    if (left.read_ex_requested_bytes != right.read_ex_requested_bytes) {
        return left.read_ex_requested_bytes > right.read_ex_requested_bytes;
    }
    if (left.read_requested_bytes != right.read_requested_bytes) {
        return left.read_requested_bytes > right.read_requested_bytes;
    }
    return _wcsicmp(left.path, right.path) < 0;
}

bool CompareLoadRegionByMappedBytes(const LoadRegionAggregate& left, const LoadRegionAggregate& right) noexcept {
    if (left.active != right.active) {
        return left.active && !right.active;
    }
    if (left.mapped_bytes != right.mapped_bytes) {
        return left.mapped_bytes > right.mapped_bytes;
    }
    if (left.map_view_calls != right.map_view_calls) {
        return left.map_view_calls > right.map_view_calls;
    }
    if (left.path_hash != right.path_hash) {
        return left.path_hash < right.path_hash;
    }
    return left.bucket_offset < right.bucket_offset;
}

bool CompareLoadRegionByReadExBytes(const LoadRegionAggregate& left, const LoadRegionAggregate& right) noexcept {
    if (left.active != right.active) {
        return left.active && !right.active;
    }
    if (left.read_ex_requested_bytes != right.read_ex_requested_bytes) {
        return left.read_ex_requested_bytes > right.read_ex_requested_bytes;
    }
    if (left.read_ex_calls != right.read_ex_calls) {
        return left.read_ex_calls > right.read_ex_calls;
    }
    if (left.path_hash != right.path_hash) {
        return left.path_hash < right.path_hash;
    }
    return left.bucket_offset < right.bucket_offset;
}

void FlushLoadProfilerSummaryInternal(const char* reason) {
    EnsureLoadProfilerStarted();

    AcquireSRWLockExclusive(&g_load_profile_lock);
    if (g_load_profiler_totals.flushed) {
        ReleaseSRWLockExclusive(&g_load_profile_lock);
        return;
    }

    g_load_profiler_totals.flushed = true;
    const LoadProfilerTotals totals = g_load_profiler_totals;

    std::memset(g_flush_active_records, 0, sizeof(g_flush_active_records));
    std::memset(g_flush_path_aggregate_records, 0, sizeof(g_flush_path_aggregate_records));
    std::memset(g_flush_region_aggregate_records, 0, sizeof(g_flush_region_aggregate_records));
    std::size_t active_count = 0;
    std::size_t aggregate_count = 0;
    std::size_t region_count = 0;
    for (const LoadFileRecord& record : g_load_file_records) {
        if (!record.active) {
            continue;
        }

        if (active_count < std::size(g_flush_active_records)) {
            g_flush_active_records[active_count++] = record;
        }
    }
    for (const LoadPathAggregate& aggregate : g_load_path_aggregates) {
        if (!aggregate.active) {
            continue;
        }

        if (aggregate_count < std::size(g_flush_path_aggregate_records)) {
            g_flush_path_aggregate_records[aggregate_count++] = aggregate;
        }
    }
    for (const LoadRegionAggregate& aggregate : g_load_region_aggregates) {
        if (!aggregate.active) {
            continue;
        }

        if (region_count < std::size(g_flush_region_aggregate_records)) {
            g_flush_region_aggregate_records[region_count++] = aggregate;
        }
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    ULONGLONG dropped_path_records = totals.dropped_path_records;
    for (std::size_t index = 0; index < active_count; ++index) {
        FoldLoadFileRecordIntoAggregateArray(
            g_flush_path_aggregate_records,
            std::size(g_flush_path_aggregate_records),
            g_flush_active_records[index],
            &dropped_path_records);
    }
    aggregate_count = 0;
    for (const LoadPathAggregate& aggregate : g_flush_path_aggregate_records) {
        if (aggregate.active) {
            ++aggregate_count;
        }
    }

    const ULONGLONG uptime_ms =
        totals.start_tick != 0 && GetTickCount64() >= totals.start_tick ? (GetTickCount64() - totals.start_tick) : 0;
    Log(
        "[load] summary reason=%s uptime=%llums opens=%llu diskOpens=%llu packOpens=%llu reads=%llu readReq=%llu readBytes=%llu readEx=%llu readExReq=%llu pending=%llu seeks=%llu mappings=%llu mapViews=%llu mappedBytes=%llu unmaps=%llu mapApiCalls=%llu mapApi=%llums unmapApiCalls=%llu unmapApi=%llums cacheHits=%llu cacheMisses=%llu evictions=%llu closes=%llu closeFailures=%llu droppedFiles=%llu droppedMappings=%llu droppedPaths=%llu droppedRegions=%llu droppedRawViews=%llu droppedCachedWindows=%llu droppedCachedViews=%llu activeFiles=%zu",
        reason,
        uptime_ms,
        totals.file_opens,
        totals.disk_file_opens,
        totals.pack_file_opens,
        totals.read_calls,
        totals.read_requested_bytes,
        totals.read_bytes,
        totals.read_ex_calls,
        totals.read_ex_requested_bytes,
        totals.pending_read_calls,
        totals.seek_calls,
        totals.mapping_creates,
        totals.map_view_calls,
        totals.mapped_bytes,
        totals.unmap_view_calls,
        totals.map_api_calls,
        totals.map_api_time_ms,
        totals.unmap_api_calls,
        totals.unmap_api_time_ms,
        totals.map_cache_hits,
        totals.map_cache_misses,
        totals.map_cache_evictions,
        totals.close_calls,
        totals.close_failures,
        totals.dropped_file_records,
        totals.dropped_mapping_records,
        dropped_path_records,
        totals.dropped_region_records,
        totals.dropped_raw_view_records,
        totals.dropped_cached_windows,
        totals.dropped_cached_client_views,
        active_count);

    std::sort(
        std::begin(g_flush_path_aggregate_records),
        std::end(g_flush_path_aggregate_records),
        CompareLoadPathByMappedBytes);
    const std::size_t top_map_count = (std::min)(kLoadTopPathCount, aggregate_count);
    for (std::size_t index = 0; index < top_map_count; ++index) {
        if (!g_flush_path_aggregate_records[index].active || g_flush_path_aggregate_records[index].mapped_bytes == 0) {
            continue;
        }
        LogLoadPathAggregateSummary(g_flush_path_aggregate_records[index], "top-map", index + 1);
    }

    std::sort(
        std::begin(g_flush_path_aggregate_records),
        std::end(g_flush_path_aggregate_records),
        CompareLoadPathByReadExBytes);
    const std::size_t top_read_count = (std::min)(kLoadTopPathCount, aggregate_count);
    for (std::size_t index = 0; index < top_read_count; ++index) {
        if (!g_flush_path_aggregate_records[index].active ||
            g_flush_path_aggregate_records[index].read_ex_requested_bytes == 0) {
            continue;
        }
        LogLoadPathAggregateSummary(g_flush_path_aggregate_records[index], "top-readex", index + 1);
    }

    std::sort(
        std::begin(g_flush_region_aggregate_records),
        std::end(g_flush_region_aggregate_records),
        CompareLoadRegionByMappedBytes);
    const std::size_t top_map_region_count = (std::min)(kLoadTopRegionCount, region_count);
    for (std::size_t index = 0; index < top_map_region_count; ++index) {
        if (!g_flush_region_aggregate_records[index].active || g_flush_region_aggregate_records[index].mapped_bytes == 0) {
            continue;
        }
        LogLoadRegionAggregateSummary(g_flush_region_aggregate_records[index], "top-map-region", index + 1);
    }

    std::sort(
        std::begin(g_flush_region_aggregate_records),
        std::end(g_flush_region_aggregate_records),
        CompareLoadRegionByReadExBytes);
    const std::size_t top_read_region_count = (std::min)(kLoadTopRegionCount, region_count);
    for (std::size_t index = 0; index < top_read_region_count; ++index) {
        if (!g_flush_region_aggregate_records[index].active ||
            g_flush_region_aggregate_records[index].read_ex_requested_bytes == 0) {
            continue;
        }
        LogLoadRegionAggregateSummary(g_flush_region_aggregate_records[index], "top-readex-region", index + 1);
    }

    for (std::size_t index = 0; index < active_count; ++index) {
        LogLoadFileSummary(g_flush_active_records[index], "still-open");
    }
}

bool BuildMapCacheRequestLocked(
    HANDLE file_mapping_object,
    DWORD desired_access,
    DWORD file_offset_high,
    DWORD file_offset_low,
    SIZE_T number_of_bytes_to_map,
    MapCacheRequest* out_request) {
    if (out_request == nullptr) {
        return false;
    }

    *out_request = {};
    if (!g_load_config.map_cache_enabled || number_of_bytes_to_map == 0) {
        return false;
    }

    LoadMappingRecord* mapping = FindLoadMappingRecordLocked(file_mapping_object);
    if (mapping == nullptr) {
        return false;
    }

    LoadFileRecord* file_record = FindLoadFileRecordLocked(mapping->file_handle);
    if (file_record == nullptr || !file_record->interesting) {
        return false;
    }

    // The first aggressive version crashed during startup because it also
    // touched small boot-time packs such as boot.pack. Keep the experiment on
    // the heavy data packs only until we know the cache semantics are safe.
    if (file_record->file_size.QuadPart <= 0 ||
        static_cast<ULONGLONG>(file_record->file_size.QuadPart) < MegabytesToBytes(g_load_config.cache_min_file_mb)) {
        return false;
    }

    // Only cache plain read-only views for now. Shared writable/copy-on-write
    // mappings would make aliasing behavior much riskier.
    if (desired_access != FILE_MAP_READ) {
        return false;
    }

    const DWORD granularity = GetSystemAllocationGranularity();
    const ULONGLONG request_offset = ComposeFileOffset(file_offset_high, file_offset_low);
    const ULONGLONG request_bytes = static_cast<ULONGLONG>(number_of_bytes_to_map);
    const ULONGLONG base_window_bytes =
        AlignUpToMultiple(MegabytesToBytes(g_load_config.cache_window_mb), static_cast<ULONGLONG>(granularity));
    ULONGLONG window_bytes = request_bytes >= base_window_bytes ? request_bytes : base_window_bytes;
    ULONGLONG window_offset = request_offset;

    if (request_bytes <= base_window_bytes) {
        window_offset = AlignDownToMultiple(request_offset, base_window_bytes);
        window_bytes = base_window_bytes;
    } else {
        window_bytes = AlignUpToMultiple(request_bytes, static_cast<ULONGLONG>(granularity));
    }

    if (file_record->file_size.QuadPart > 0) {
        const ULONGLONG file_size = static_cast<ULONGLONG>(file_record->file_size.QuadPart);
        if (window_offset >= file_size || request_offset + request_bytes > file_size) {
            return false;
        }

        const ULONGLONG remaining = file_size - window_offset;
        if (window_bytes > remaining) {
            window_bytes = remaining;
        }
    }

    if (window_bytes < request_bytes || window_bytes == 0) {
        return false;
    }

    out_request->eligible = true;
    out_request->file_handle = mapping->file_handle;
    out_request->file_record = file_record;
    out_request->interesting = file_record->interesting;
    out_request->request_offset = request_offset;
    out_request->request_bytes = request_bytes;
    out_request->window_offset = window_offset;
    out_request->window_bytes = window_bytes;
    return true;
}

bool RegisterRawMappedViewLocked(
    void* base,
    HANDLE mapping_handle,
    HANDLE file_handle,
    ULONGLONG mapped_bytes,
    ULONGLONG offset,
    bool interesting) {
    RawMappedViewRecord* record = AllocateRawMappedViewRecordLocked();
    if (record == nullptr) {
        g_load_profiler_totals.dropped_raw_view_records++;
        return false;
    }

    record->base = base;
    record->mapping_handle = mapping_handle;
    record->file_handle = file_handle;
    record->mapped_bytes = mapped_bytes;
    record->offset = offset;
    record->map_tick = GetTickCount64();
    record->interesting = interesting;
    return true;
}

bool RegisterCachedClientViewLocked(
    std::size_t window_index,
    void* client_base,
    HANDLE file_handle,
    ULONGLONG request_bytes,
    ULONGLONG request_offset,
    bool interesting) {
    if (window_index == kInvalidCachedWindowIndex || window_index >= std::size(g_cached_map_windows)) {
        return false;
    }

    CachedClientView* view = AllocateCachedClientViewLocked();
    if (view == nullptr) {
        g_load_profiler_totals.dropped_cached_client_views++;
        return false;
    }

    CachedMapWindow& window = g_cached_map_windows[window_index];
    view->client_base = client_base;
    view->file_handle = file_handle;
    view->requested_bytes = request_bytes;
    view->request_offset = request_offset;
    view->map_tick = GetTickCount64();
    view->interesting = interesting;
    view->window_index = window_index;

    window.live_client_views++;
    window.last_touch_tick = view->map_tick;
    return true;
}

void ReleaseCachedWindowLocked(std::size_t window_index) noexcept {
    if (window_index == kInvalidCachedWindowIndex || window_index >= std::size(g_cached_map_windows)) {
        return;
    }

    g_cached_map_windows[window_index] = {};
}

bool EvictCachedWindow(std::size_t window_index, const char* reason) {
    if (window_index == kInvalidCachedWindowIndex || window_index >= std::size(g_cached_map_windows)) {
        return false;
    }

    CachedMapWindow evicted = {};
    AcquireSRWLockExclusive(&g_load_profile_lock);
    CachedMapWindow& window = g_cached_map_windows[window_index];
    if (!window.active || window.live_client_views != 0 || window.real_base == nullptr) {
        ReleaseSRWLockExclusive(&g_load_profile_lock);
        return false;
    }

    evicted = window;
    ReleaseCachedWindowLocked(window_index);
    g_load_profiler_totals.map_cache_evictions++;
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    const ULONGLONG start_tick = GetTickCount64();
    const BOOL unmap_result = g_original_unmap_view_of_file(evicted.real_base);
    const DWORD last_error = GetLastError();
    const ULONGLONG elapsed_ms = GetTickCount64() - start_tick;

    AcquireSRWLockExclusive(&g_load_profile_lock);
    LoadFileRecord* file_record = FindLoadFileRecordLocked(evicted.file_handle);
    if (unmap_result) {
        AttributeUnmapApiCallLocked(file_record, elapsed_ms);
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    if (evicted.interesting && ShouldLogLimited(&g_load_map_log_count, kLoadMapLogLimit)) {
        Log(
            "[load] cache-evict reason=%s file=%p bytes=%llu offset=0x%llx life=%llums result=%u error=%lu",
            reason,
            evicted.file_handle,
            evicted.mapped_bytes,
            evicted.window_offset,
            GetTickCount64() - evicted.map_tick,
            unmap_result ? 1u : 0u,
            static_cast<unsigned long>(last_error));
    }

    SetLastError(last_error);
    return unmap_result != FALSE;
}

// File open hook: starts per-handle tracking and records which pack files the
// engine is actually touching during a load.
HANDLE WINAPI HookCreateFileW(
    LPCWSTR file_name,
    DWORD desired_access,
    DWORD share_mode,
    LPSECURITY_ATTRIBUTES security_attributes,
    DWORD creation_disposition,
    DWORD flags_and_attributes,
    HANDLE template_file) {
    EnsureLoadProfilerStarted();

    const bool read_only_access = (desired_access & (GENERIC_WRITE | FILE_WRITE_DATA | FILE_APPEND_DATA)) == 0;
    const bool can_apply_random_hint =
        g_load_config.random_access_hint &&
        read_only_access &&
        creation_disposition == OPEN_EXISTING &&
        IsPackLikeLoadPath(file_name);
    const DWORD effective_flags_and_attributes =
        can_apply_random_hint ? (flags_and_attributes | FILE_FLAG_RANDOM_ACCESS) : flags_and_attributes;

    const HANDLE result = g_original_create_file_w(
        file_name,
        desired_access,
        share_mode,
        security_attributes,
        creation_disposition,
        effective_flags_and_attributes,
        template_file);
    const DWORD last_error = GetLastError();

    if (result != INVALID_HANDLE_VALUE) {
        const DWORD file_type = GetFileType(result);
        LARGE_INTEGER file_size = {};
        if (file_type == FILE_TYPE_DISK) {
            (void)GetFileSizeEx(result, &file_size);
        }

        bool should_log_open = false;
        bool interesting = false;
        AcquireSRWLockExclusive(&g_load_profile_lock);
        g_load_profiler_totals.file_opens++;

        if (file_type == FILE_TYPE_DISK) {
            g_load_profiler_totals.disk_file_opens++;
            interesting = IsInterestingLoadPath(file_name);
            if (interesting) {
                g_load_profiler_totals.pack_file_opens++;
            }

            LoadFileRecord* record = AllocateLoadFileRecordLocked();
            if (record == nullptr) {
                g_load_profiler_totals.dropped_file_records++;
            } else {
                record->handle = result;
                record->file_type = file_type;
                record->desired_access = desired_access;
                record->creation_disposition = creation_disposition;
                record->flags_and_attributes = effective_flags_and_attributes;
                record->interesting = interesting;
                record->random_access_hint_applied = can_apply_random_hint;
                record->file_size = file_size;
                record->open_tick = GetTickCount64();
                CopyPathForRecord(record->path, file_name);
            }
        }

        should_log_open = interesting && ShouldLogLimited(&g_load_open_log_count, kLoadOpenLogLimit);
        ReleaseSRWLockExclusive(&g_load_profile_lock);

        if (should_log_open) {
            Log(
                "[load] open handle=%p access=0x%08lx disposition=0x%08lx flags=0x%08lx randomHint=%u size=%lld path=%ls",
                result,
                desired_access,
                creation_disposition,
                effective_flags_and_attributes,
                can_apply_random_hint ? 1u : 0u,
                file_size.QuadPart,
                file_name != nullptr ? file_name : L"(null)");
        }
    }

    SetLastError(last_error);
    return result;
}

// Synchronous read hook: captures requested/completed byte counts and how long
// each read call actually blocks the caller.
BOOL WINAPI HookReadFile(
    HANDLE file,
    LPVOID buffer,
    DWORD bytes_to_read,
    LPDWORD bytes_read,
    LPOVERLAPPED overlapped) {
    const ULONGLONG start_tick = GetTickCount64();
    const BOOL result = g_original_read_file(file, buffer, bytes_to_read, bytes_read, overlapped);
    const DWORD last_error = GetLastError();
    const ULONGLONG elapsed_ms = GetTickCount64() - start_tick;
    const bool pending = !result && last_error == ERROR_IO_PENDING;
    const DWORD completed_bytes = result && bytes_read != nullptr ? *bytes_read : 0;

    bool should_log_read = false;
    bool interesting = false;
    AcquireSRWLockExclusive(&g_load_profile_lock);
    g_load_profiler_totals.read_calls++;
    g_load_profiler_totals.read_requested_bytes += bytes_to_read;
    g_load_profiler_totals.read_bytes += completed_bytes;
    if (pending) {
        g_load_profiler_totals.pending_read_calls++;
    }

    if (LoadFileRecord* record = FindLoadFileRecordLocked(file)) {
        record->read_calls++;
        record->total_read_requested_bytes += bytes_to_read;
        record->total_read_bytes += completed_bytes;
        record->total_sync_read_time_ms += elapsed_ms;
        if (completed_bytes > record->max_read_size) {
            record->max_read_size = completed_bytes;
        }
        if (pending) {
            record->pending_read_calls++;
        }

        interesting = record->interesting;
        if ((interesting || bytes_to_read >= kLargeLoadThresholdBytes) &&
            ShouldLogLimited(&g_load_read_log_count, kLoadReadLogLimit)) {
            should_log_read = true;
        }
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    if (should_log_read) {
        Log(
            "[load] read handle=%p req=%lu got=%lu pending=%u time=%llums",
            file,
            static_cast<unsigned long>(bytes_to_read),
            static_cast<unsigned long>(completed_bytes),
            pending ? 1u : 0u,
            elapsed_ms);
    }

    SetLastError(last_error);
    return result;
}

// Asynchronous read hook: we cannot see completion size here, but we can still
// measure how much work the engine queues through ReadFileEx.
BOOL WINAPI HookReadFileEx(
    HANDLE file,
    LPVOID buffer,
    DWORD bytes_to_read,
    LPOVERLAPPED overlapped,
    LPOVERLAPPED_COMPLETION_ROUTINE completion_routine) {
    (void)buffer;
    const BOOL result = g_original_read_file_ex(file, buffer, bytes_to_read, overlapped, completion_routine);
    const DWORD last_error = GetLastError();

    if (result) {
        AcquireSRWLockExclusive(&g_load_profile_lock);
        g_load_profiler_totals.read_ex_calls++;
        g_load_profiler_totals.read_ex_requested_bytes += bytes_to_read;
        if (LoadFileRecord* record = FindLoadFileRecordLocked(file)) {
            record->read_ex_calls++;
            record->total_read_ex_requested_bytes += bytes_to_read;
            if (overlapped != nullptr) {
                const ULONGLONG request_offset = ComposeFileOffset(overlapped->OffsetHigh, overlapped->Offset);
                AttributeLoadRegionSpanLocked(record, request_offset, bytes_to_read, LoadRegionKind::ReadFileEx);
            }
        }
        ReleaseSRWLockExclusive(&g_load_profile_lock);
    }

    SetLastError(last_error);
    return result;
}

// Mapping hooks let us tell whether the engine is streaming data through
// memory-mapped pack files instead of large ReadFile bursts.
HANDLE WINAPI HookCreateFileMappingW(
    HANDLE file,
    LPSECURITY_ATTRIBUTES attributes,
    DWORD protect,
    DWORD maximum_size_high,
    DWORD maximum_size_low,
    LPCWSTR name) {
    const HANDLE result =
        g_original_create_file_mapping_w(file, attributes, protect, maximum_size_high, maximum_size_low, name);
    const DWORD last_error = GetLastError();

    if (result != nullptr && file != nullptr && file != INVALID_HANDLE_VALUE) {
        const ULONGLONG max_mapping_bytes =
            (static_cast<ULONGLONG>(maximum_size_high) << 32) | static_cast<ULONGLONG>(maximum_size_low);

        AcquireSRWLockExclusive(&g_load_profile_lock);
        g_load_profiler_totals.mapping_creates++;
        if (FindLoadFileRecordLocked(file) != nullptr) {
            LoadMappingRecord* mapping = AllocateLoadMappingRecordLocked();
            if (mapping == nullptr) {
                g_load_profiler_totals.dropped_mapping_records++;
            } else {
                mapping->handle = result;
                mapping->file_handle = file;
                mapping->max_mapping_bytes = max_mapping_bytes;
                mapping->protect = protect;
            }
        }
        ReleaseSRWLockExclusive(&g_load_profile_lock);
    }

    SetLastError(last_error);
    return result;
}

LPVOID WINAPI HookMapViewOfFile(
    HANDLE file_mapping_object,
    DWORD desired_access,
    DWORD file_offset_high,
    DWORD file_offset_low,
    SIZE_T number_of_bytes_to_map) {
    EnsureLoadProfilerStarted();
    const LPVOID result = g_original_map_view_of_file(
        file_mapping_object,
        desired_access,
        file_offset_high,
        file_offset_low,
        number_of_bytes_to_map);
    const DWORD last_error = GetLastError();

    bool should_log_map = false;
    HANDLE file_handle = nullptr;
    ULONGLONG mapped_bytes = 0;
    if (result != nullptr) {
        AcquireSRWLockExclusive(&g_load_profile_lock);
        if (LoadMappingRecord* mapping = FindLoadMappingRecordLocked(file_mapping_object)) {
            file_handle = mapping->file_handle;
            if (LoadFileRecord* record = FindLoadFileRecordLocked(mapping->file_handle)) {
                const ULONGLONG request_offset = ComposeFileOffset(file_offset_high, file_offset_low);
                mapped_bytes = ResolveMappedBytes(record, number_of_bytes_to_map);
                g_load_profiler_totals.map_view_calls++;
                g_load_profiler_totals.mapped_bytes += mapped_bytes;
                record->map_view_calls++;
                record->total_mapped_bytes += mapped_bytes;
                if (mapped_bytes > record->max_map_view_size) {
                    record->max_map_view_size = mapped_bytes;
                }
                AttributeLoadRegionSpanLocked(record, request_offset, mapped_bytes, LoadRegionKind::MapView);
                should_log_map = record->interesting && ShouldLogLimited(&g_load_map_log_count, kLoadMapLogLimit);
            }
        }
        ReleaseSRWLockExclusive(&g_load_profile_lock);
    }

    if (should_log_map) {
        Log(
            "[load] map file=%p bytes=%llu access=0x%08lx offset=0x%08lx%08lx result=%p",
            file_handle,
            mapped_bytes,
            desired_access,
            file_offset_high,
            file_offset_low,
            result);
    }

    SetLastError(last_error);
    return result;
}

// Unmap handling mirrors the cache behavior above:
//
// - cached client views are treated as logical unmaps only
// - raw pass-through views still call the real UnmapViewOfFile
// - cached backing windows are only unmapped when evicted, closed, or on
//   process shutdown
BOOL WINAPI HookUnmapViewOfFile(LPCVOID base_address) {
    EnsureLoadProfilerStarted();

    std::size_t pending_window_eviction = kInvalidCachedWindowIndex;
    bool handled_cached_view = false;
    bool should_log_unmap = false;
    HANDLE file_handle = nullptr;
    ULONGLONG mapped_bytes = 0;
    ULONGLONG lifetime_ms = 0;

    AcquireSRWLockExclusive(&g_load_profile_lock);
    if (CachedClientView* cached_view = FindCachedClientViewLocked(const_cast<void*>(base_address))) {
        file_handle = cached_view->file_handle;
        mapped_bytes = cached_view->requested_bytes;
        lifetime_ms = GetTickCount64() - cached_view->map_tick;
        handled_cached_view = true;

        LoadFileRecord* file_record = FindLoadFileRecordLocked(cached_view->file_handle);
        AttributeLogicalUnmapLocked(file_record);

        if (cached_view->interesting && ShouldLogLimited(&g_load_map_log_count, kLoadMapLogLimit)) {
            should_log_unmap = true;
        }

        if (cached_view->window_index < std::size(g_cached_map_windows)) {
            CachedMapWindow& window = g_cached_map_windows[cached_view->window_index];
            if (window.active && window.live_client_views > 0) {
                window.live_client_views--;
                window.last_touch_tick = GetTickCount64();
                if (!window.reusable && window.live_client_views == 0) {
                    pending_window_eviction = cached_view->window_index;
                }
            }
        }

        *cached_view = {};
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    if (handled_cached_view) {
        if (pending_window_eviction != kInvalidCachedWindowIndex) {
            (void)EvictCachedWindow(pending_window_eviction, "mapping-closed");
        }

        if (should_log_unmap) {
            Log(
                "[load] unmap-cached file=%p bytes=%llu life=%llums base=%p",
                file_handle,
                mapped_bytes,
                lifetime_ms,
                base_address);
        }
        return TRUE;
    }

    RawMappedViewRecord raw_view = {};
    bool have_raw_view = false;
    AcquireSRWLockExclusive(&g_load_profile_lock);
    if (RawMappedViewRecord* view = FindRawMappedViewRecordLocked(const_cast<void*>(base_address))) {
        raw_view = *view;
        have_raw_view = true;
        *view = {};
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    const ULONGLONG api_start_tick = GetTickCount64();
    const BOOL result = g_original_unmap_view_of_file(base_address);
    const DWORD last_error = GetLastError();
    const ULONGLONG api_elapsed_ms = GetTickCount64() - api_start_tick;

    if (have_raw_view && result) {
        AcquireSRWLockExclusive(&g_load_profile_lock);
        if (LoadFileRecord* file_record = FindLoadFileRecordLocked(raw_view.file_handle)) {
            AttributeLogicalUnmapLocked(file_record);
            AttributeUnmapApiCallLocked(file_record, api_elapsed_ms);
            should_log_unmap = raw_view.interesting && ShouldLogLimited(&g_load_map_log_count, kLoadMapLogLimit);
            file_handle = raw_view.file_handle;
            mapped_bytes = raw_view.mapped_bytes;
            lifetime_ms = GetTickCount64() - raw_view.map_tick;
        }
        ReleaseSRWLockExclusive(&g_load_profile_lock);
    }

    if (should_log_unmap) {
        Log(
            "[load] unmap-raw file=%p bytes=%llu life=%llums base=%p result=%u",
            file_handle,
            mapped_bytes,
            lifetime_ms,
            base_address,
            result ? 1u : 0u);
    }

    SetLastError(last_error);
    return result;
}

BOOL WINAPI HookSetFilePointerEx(HANDLE file, LARGE_INTEGER distance, PLARGE_INTEGER new_file_pointer, DWORD move_method) {
    const BOOL result = g_original_set_file_pointer_ex(file, distance, new_file_pointer, move_method);
    const DWORD last_error = GetLastError();

    if (result) {
        AcquireSRWLockExclusive(&g_load_profile_lock);
        if (LoadFileRecord* record = FindLoadFileRecordLocked(file)) {
            record->seek_calls++;
            g_load_profiler_totals.seek_calls++;
        }
        ReleaseSRWLockExclusive(&g_load_profile_lock);
    }

    SetLastError(last_error);
    return result;
}

BOOL WINAPI HookCloseHandle(HANDLE object) {
    const BOOL result = g_original_close_handle(object);
    const DWORD last_error = GetLastError();

    LoadFileRecord closed_file = {};
    bool have_closed_file = false;
    bool should_log_close = false;
    std::size_t windows_to_evict[kMaxCachedMapWindows] = {};
    std::size_t windows_to_evict_count = 0;
    AcquireSRWLockExclusive(&g_load_profile_lock);
    g_load_profiler_totals.close_calls++;
    if (!result) {
        g_load_profiler_totals.close_failures++;
    } else {
        if (LoadMappingRecord* mapping = FindLoadMappingRecordLocked(object)) {
            for (std::size_t index = 0; index < std::size(g_cached_map_windows); ++index) {
                CachedMapWindow& window = g_cached_map_windows[index];
                if (!window.active || window.mapping_handle != object) {
                    continue;
                }

                window.reusable = false;
                if (window.live_client_views == 0 && windows_to_evict_count < std::size(windows_to_evict)) {
                    windows_to_evict[windows_to_evict_count++] = index;
                }
            }
            *mapping = {};
        }

        if (LoadFileRecord* record = FindLoadFileRecordLocked(object)) {
            closed_file = *record;
            have_closed_file = true;
            FoldLoadFileRecordIntoAggregateArray(
                g_load_path_aggregates,
                std::size(g_load_path_aggregates),
                *record,
                &g_load_profiler_totals.dropped_path_records);
            should_log_close =
                (record->interesting ||
                    record->total_read_bytes >= kLargeLoadThresholdBytes ||
                    record->map_view_calls > 0) &&
                ShouldLogLimited(&g_load_close_log_count, kLoadCloseLogLimit);
            *record = {};
        }
    }
    ReleaseSRWLockExclusive(&g_load_profile_lock);

    for (std::size_t index = 0; index < windows_to_evict_count; ++index) {
        (void)EvictCachedWindow(windows_to_evict[index], "mapping-close");
    }

    if (should_log_close && have_closed_file) {
        LogLoadFileSummary(closed_file, "close");
    }

    SetLastError(last_error);
    return result;
}

bool ShouldLogUiSnapshot(LONG target_width, LONG target_height, float scale, bool disable) noexcept {
    if (!g_last_ui_debug_snapshot.valid) {
        g_last_ui_debug_snapshot = {true, target_width, target_height, scale, disable};
        return true;
    }

    const bool changed =
        g_last_ui_debug_snapshot.target_width != target_width ||
        g_last_ui_debug_snapshot.target_height != target_height ||
        std::fabs(g_last_ui_debug_snapshot.scale - scale) > kUiScaleIdentityEpsilon ||
        g_last_ui_debug_snapshot.disable != disable;

    if (changed) {
        g_last_ui_debug_snapshot = {true, target_width, target_height, scale, disable};
    }

    return changed;
}

bool TryGetVirtualUiSize(LONG base_width, LONG base_height, LONG* virtual_width_out, LONG* virtual_height_out, float* scale_out) {
    EnsureUiScaleConfigLoaded();
    if (!g_ui_scale_config.present) {
        return false;
    }

    const float scale = ClampUiScale(g_ui_scale_config.scale);
    if (std::fabs(scale - 1.0f) <= kUiScaleIdentityEpsilon) {
        return false;
    }

    LONG width = base_width;
    LONG height = base_height;
    if (width <= 0 || height <= 0) {
        const SIZE target_size = GetUiTargetSize(g_main_window);
        width = target_size.cx;
        height = target_size.cy;
    }

    if (width <= 0 || height <= 0) {
        return false;
    }

    const LONG virtual_width = static_cast<LONG>(std::lround(static_cast<double>(width) / scale));
    const LONG virtual_height = static_cast<LONG>(std::lround(static_cast<double>(height) / scale));
    if (virtual_width <= 0 || virtual_height <= 0) {
        return false;
    }

    if (virtual_width_out != nullptr) {
        *virtual_width_out = virtual_width;
    }

    if (virtual_height_out != nullptr) {
        *virtual_height_out = virtual_height;
    }

    if (scale_out != nullptr) {
        *scale_out = scale;
    }

    return true;
}

UiRect* WINAPI HookGetUiSourceRect(UiRect* out_rect) {
    UiRect* result = g_original_get_ui_source_rect != nullptr ? g_original_get_ui_source_rect(out_rect) : out_rect;

    if (ShouldLogLimited(&g_ui_source_rect_log_count, 8) && result != nullptr) {
        Log(
            "[ui-debug] source-rect result=%.3f,%.3f,%.3f,%.3f",
            result->x,
            result->y,
            result->width,
            result->height);
        DumpUiScalingState("source-rect-call");
    }

    return result;
}

UiSize* __fastcall HookComputeUiSizePrimary(void* self, void*, UiSize* out_size) {
    UiSize* result = g_original_compute_ui_size_primary != nullptr
        ? g_original_compute_ui_size_primary(self, out_size)
        : out_size;

    if (ShouldLogLimited(&g_ui_size_primary_log_count, 8) && result != nullptr) {
        Log(
            "[ui-debug] ui-size-primary self=%p result=%ldx%ld",
            self,
            result->width,
            result->height);
    }

    if (result != nullptr) {
        LONG virtual_width = 0;
        LONG virtual_height = 0;
        float scale = 1.0f;
        if (TryGetVirtualUiSize(result->width, result->height, &virtual_width, &virtual_height, &scale)) {
            const LONG original_width = result->width;
            const LONG original_height = result->height;
            result->width = virtual_width;
            result->height = virtual_height;

            if (ShouldLogLimited(&g_ui_size_primary_override_log_count, 8)) {
                Log(
                    "[ui-debug] ui-size-primary override self=%p scale=%.3f original=%ldx%ld virtual=%ldx%ld",
                    self,
                    scale,
                    original_width,
                    original_height,
                    result->width,
                    result->height);
            }
        }
    }

    return result;
}

UiSize* __fastcall HookComputeUiSizeSecondary(void* self, void*, UiSize* out_size) {
    UiSize* result = g_original_compute_ui_size_secondary != nullptr
        ? g_original_compute_ui_size_secondary(self, out_size)
        : out_size;

    if (ShouldLogLimited(&g_ui_size_secondary_log_count, 8) && result != nullptr) {
        Log(
            "[ui-debug] ui-size-secondary self=%p result=%ldx%ld",
            self,
            result->width,
            result->height);
    }

    if (result != nullptr) {
        LONG virtual_width = 0;
        LONG virtual_height = 0;
        float scale = 1.0f;
        if (TryGetVirtualUiSize(result->width, result->height, &virtual_width, &virtual_height, &scale)) {
            const LONG original_width = result->width;
            const LONG original_height = result->height;
            result->width = virtual_width;
            result->height = virtual_height;

            if (ShouldLogLimited(&g_ui_size_secondary_override_log_count, 8)) {
                Log(
                    "[ui-debug] ui-size-secondary override self=%p scale=%.3f original=%ldx%ld virtual=%ldx%ld",
                    self,
                    scale,
                    original_width,
                    original_height,
                    result->width,
                    result->height);
            }
        }
    }

    return result;
}

void DisableUiScalingOverride() {
    std::uint8_t* base = GetEmpireRetailBase();
    if (base == nullptr) {
        return;
    }

    const bool should_log = ShouldLogUiSnapshot(0, 0, 1.0f, true);
    if (should_log) {
        DumpUiScalingState("before-disable");
    }

    WriteBoolConfig(base, kRvaUiRectOverride, false);
    WriteBoolConfig(base, kRvaEnableUiScaling, false);
    Log("Disabled hidden UI scaling override");

    if (should_log) {
        DumpUiScalingState("after-disable");
    }
}

SIZE GetUiTargetSize(HWND hwnd) noexcept {
    SIZE size = {};

    RECT client_rect = {};
    if (hwnd != nullptr && GetClientRect(hwnd, &client_rect)) {
        size.cx = client_rect.right - client_rect.left;
        size.cy = client_rect.bottom - client_rect.top;
    }

    if (size.cx > 0 && size.cy > 0) {
        return size;
    }

    const RECT monitor_rect = GetMonitorRectForWindow(hwnd);
    size.cx = monitor_rect.right - monitor_rect.left;
    size.cy = monitor_rect.bottom - monitor_rect.top;
    return size;
}

bool EnsureOverlayWindowClass() {
    static ATOM overlay_class = 0;
    if (overlay_class != 0) {
        return true;
    }

    WNDCLASSEXW window_class = {};
    window_class.cbSize = sizeof(window_class);
    window_class.hInstance = GetSelfModule();
    window_class.lpfnWndProc = &OverlayWindowProc;
    window_class.lpszClassName = kOverlayWindowClassName;
    window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);

    overlay_class = RegisterClassExW(&window_class);
    if (overlay_class == 0 && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        Log("RegisterClassExW failed for overlay window: %lu", GetLastError());
        return false;
    }

    return true;
}

void DestroyOverlayWindow() {
    if (g_overlay_window != nullptr && IsWindow(g_overlay_window)) {
        DestroyWindow(g_overlay_window);
    }

    g_overlay_window = nullptr;

    if (g_overlay_font != nullptr) {
        DeleteObject(g_overlay_font);
        g_overlay_font = nullptr;
    }
}

HWND EnsureOverlayWindow(HWND owner) {
    if (g_overlay_window != nullptr && IsWindow(g_overlay_window)) {
        return g_overlay_window;
    }

    if (!EnsureOverlayWindowClass()) {
        return nullptr;
    }

    g_overlay_window = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        kOverlayWindowClassName,
        L"",
        WS_POPUP,
        0,
        0,
        260,
        56,
        owner,
        nullptr,
        GetSelfModule(),
        nullptr);
    if (g_overlay_window == nullptr) {
        Log("CreateWindowExW failed for overlay window: %lu", GetLastError());
        return nullptr;
    }

    if (g_overlay_font == nullptr) {
        g_overlay_font = CreateFontW(
            -24,
            0,
            0,
            0,
            FW_BOLD,
            FALSE,
            FALSE,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            L"Segoe UI");
    }

    return g_overlay_window;
}

void PositionOverlayWindow(HWND owner) {
    if (g_overlay_window == nullptr || !IsWindow(g_overlay_window)) {
        return;
    }

    const RECT monitor_rect = GetMonitorRectForWindow(owner);
    const int overlay_width = 260;
    const int overlay_height = 56;
    const int x = monitor_rect.left + ((monitor_rect.right - monitor_rect.left - overlay_width) / 2);
    const int y = monitor_rect.top + 28;

    SetWindowPos(
        g_overlay_window,
        HWND_TOPMOST,
        x,
        y,
        overlay_width,
        overlay_height,
        SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ShowUiScaleOverlay(HWND owner, float scale) {
    EnsureUiScaleConfigLoaded();
    if (!g_ui_scale_config.overlay_enabled) {
        return;
    }

    HWND overlay = EnsureOverlayWindow(owner);
    if (overlay == nullptr) {
        return;
    }

    wchar_t text[64] = {};
    (void)swprintf_s(text, std::size(text), L"UI Scale %.2f", scale);
    SetWindowTextW(overlay, text);
    PositionOverlayWindow(owner);
    InvalidateRect(overlay, nullptr, TRUE);
    ShowWindow(overlay, SW_SHOWNOACTIVATE);
    KillTimer(overlay, kOverlayTimerId);
    SetTimer(overlay, kOverlayTimerId, kOverlayDurationMs, nullptr);
    UpdateWindow(overlay);
}

void ForceUiRefresh(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    InvalidateRect(hwnd, nullptr, FALSE);
    RedrawWindow(hwnd, nullptr, nullptr, RDW_INVALIDATE | RDW_FRAME | RDW_ALLCHILDREN);
}

void UpdateUiScale(HWND hwnd, float new_scale, const char* reason) {
    EnsureUiScaleConfigLoaded();

    const float old_scale = g_ui_scale_config.scale;
    const float clamped_scale = ClampUiScale(new_scale);
    g_ui_scale_config.present = true;
    g_ui_scale_config.scale = clamped_scale;
    SaveUiScaleConfig();
    g_last_ui_debug_snapshot.valid = false;

    Log("Updated ui.scale from %.3f to %.3f via %s", old_scale, clamped_scale, reason);

    ApplyUiScalingForWindow(hwnd);
    ForceUiRefresh(hwnd);
    ShowUiScaleOverlay(hwnd, clamped_scale);
}

bool IsKeyCurrentlyDown(int virtual_key) {
    return (GetAsyncKeyState(virtual_key) & 0x8000) != 0;
}

bool HandleUiScaleHotkey(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    EnsureUiScaleConfigLoaded();
    if (!g_ui_scale_config.hotkeys_enabled || hwnd == nullptr) {
        return false;
    }

    if (message != WM_KEYDOWN && message != WM_SYSKEYDOWN) {
        return false;
    }

    if ((l_param & 0x40000000) != 0) {
        return false;
    }

    const bool has_ctrl = IsKeyCurrentlyDown(VK_CONTROL);
    const bool has_shift = IsKeyCurrentlyDown(VK_SHIFT);
    const bool has_alt = IsKeyCurrentlyDown(VK_MENU);
    if (!has_ctrl) {
        return false;
    }

    bool handled = false;
    float target_scale = g_ui_scale_config.scale;

    switch (w_param) {
    case VK_PRIOR:
        if (has_shift) {
            target_scale += g_ui_scale_config.step;
            handled = true;
        }
        break;
    case VK_NEXT:
        if (has_shift) {
            target_scale -= g_ui_scale_config.step;
            handled = true;
        }
        break;
    case VK_HOME:
        if (has_shift) {
            target_scale = 1.0f;
            handled = true;
        }
        break;
    case VK_ADD:
    case VK_OEM_PLUS:
        if (has_alt) {
            target_scale += g_ui_scale_config.step;
            handled = true;
        }
        break;
    case VK_SUBTRACT:
    case VK_OEM_MINUS:
        if (has_alt) {
            target_scale -= g_ui_scale_config.step;
            handled = true;
        }
        break;
    case '0':
    case VK_NUMPAD0:
        if (has_alt) {
            target_scale = 1.0f;
            handled = true;
        }
        break;
    default:
        break;
    }

    if (!handled) {
        return false;
    }

    target_scale = ClampUiScale(target_scale);
    UpdateUiScale(hwnd, target_scale, "hotkey");

    if (ShouldLogLimited(&g_ui_hotkey_log_count, 16)) {
        Log(
            "[ui-debug] hotkey message=0x%04x vk=0x%02Ix ctrl=%u shift=%u alt=%u newScale=%.3f",
            message,
            static_cast<std::uintptr_t>(w_param),
            has_ctrl ? 1u : 0u,
            has_shift ? 1u : 0u,
            has_alt ? 1u : 0u,
            target_scale);
    }

    return true;
}

void AttachMainWindowProc(HWND hwnd) {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return;
    }

    if (g_main_window == hwnd && g_original_main_window_proc != nullptr) {
        return;
    }

    if (g_main_window != nullptr && g_original_main_window_proc != nullptr && IsWindow(g_main_window)) {
        SetWindowLongPtrW(g_main_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(g_original_main_window_proc));
    }

    WNDPROC current_proc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    if (current_proc == &MainWindowProc) {
        return;
    }

    g_original_main_window_proc = current_proc;
    SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&MainWindowProc));
    Log("Attached main window proc hwnd=%p originalProc=%p", hwnd, current_proc);
}

void ApplyUiScalingForWindow(HWND hwnd) {
    EnsureUiScaleConfigLoaded();
    if (!g_ui_scale_config.present) {
        return;
    }

    std::uint8_t* base = GetEmpireRetailBase();
    if (base == nullptr) {
        return;
    }

    const SIZE target_size = GetUiTargetSize(hwnd);
    if (target_size.cx < 640 || target_size.cy < 480) {
        Log("Skipping UI scale update for hwnd=%p because target size is too small: %ldx%ld", hwnd, target_size.cx, target_size.cy);
        return;
    }

    const float scale = ClampUiScale(g_ui_scale_config.scale);
    if (std::fabs(scale - 1.0f) <= kUiScaleIdentityEpsilon) {
        DisableUiScalingOverride();
        return;
    }

    const float virtual_width = static_cast<float>(target_size.cx) / scale;
    const float virtual_height = static_cast<float>(target_size.cy) / scale;
    const bool should_log = ShouldLogUiSnapshot(target_size.cx, target_size.cy, scale, false);

    if (should_log) {
        Log(
            "[ui-debug] apply-request hwnd=%p target=%ldx%ld scale=%.3f virtual=%.1fx%.1f",
            hwnd,
            target_size.cx,
            target_size.cy,
            scale,
            virtual_width,
            virtual_height);
        DumpUiScalingState("before-apply");
    }

    WriteFloatConfig(base, kRvaUiX, 0.0f);
    WriteFloatConfig(base, kRvaUiY, 0.0f);
    WriteFloatConfig(base, kRvaUiWidth, virtual_width);
    WriteFloatConfig(base, kRvaUiHeight, virtual_height);
    WriteBoolConfig(base, kRvaUiRectOverride, true);
    WriteBoolConfig(base, kRvaEnableUiScaling, true);

    Log(
        "Applied hidden UI scale hwnd=%p scale=%.3f target=%ldx%ld virtual=%.1fx%.1f",
        hwnd,
        scale,
        target_size.cx,
        target_size.cy,
        virtual_width,
        virtual_height);

    if (should_log) {
        DumpUiScalingState("after-apply");
    }
}

DWORD NormalizeStyle(DWORD style) noexcept {
    style &= ~kStyleBitsToClear;
    style |= WS_POPUP;
    return style;
}

DWORD NormalizeExStyle(DWORD ex_style) noexcept {
    ex_style &= ~kExStyleBitsToClear;
    return ex_style;
}

bool IsTopLevelWindow(HWND hwnd) noexcept {
    if (hwnd == nullptr || !IsWindow(hwnd)) {
        return false;
    }

    return GetParent(hwnd) == nullptr && (static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE)) & WS_CHILD) == 0;
}

bool IsWindowedCandidateStyle(DWORD style) noexcept {
    if ((style & WS_CHILD) != 0) {
        return false;
    }

    if ((style & WS_POPUP) != 0 && (style & (WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) == 0) {
        return false;
    }

    return (style & (WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME)) != 0;
}

bool IsWindowedCandidateCreate(DWORD style, int width, int height, HWND parent) noexcept {
    if (parent != nullptr) {
        return false;
    }

    if (!IsWindowedCandidateStyle(style)) {
        return false;
    }

    return width >= 640 && height >= 480;
}

RECT GetMonitorRectForRect(const RECT& rect) noexcept {
    RECT result = rect;

    MONITORINFO monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info)) {
        result = monitor_info.rcMonitor;
    }

    return result;
}

RECT GetMonitorRectForWindow(HWND hwnd) noexcept {
    RECT rect = {};
    if (hwnd != nullptr && GetWindowRect(hwnd, &rect)) {
        return GetMonitorRectForRect(rect);
    }

    MONITORINFO monitor_info = {};
    monitor_info.cbSize = sizeof(monitor_info);
    const HMONITOR monitor = MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY);
    if (monitor != nullptr && GetMonitorInfoW(monitor, &monitor_info)) {
        return monitor_info.rcMonitor;
    }

    return rect;
}

void TrackMainWindow(HWND hwnd, const char* reason) {
    if (!IsTopLevelWindow(hwnd)) {
        return;
    }

    if (g_main_window != hwnd) {
        g_main_window = hwnd;
        Log("Tracking main window %p via %s", hwnd, reason);
        // We still subclass the main window even though live UI-scale hotkeys
        // are disabled. The subclass is now used for two safer jobs only:
        //
        // 1. passive resize/display notifications for the launch-time scaler
        // 2. guaranteed end-of-session profiler flushing on WM_NCDESTROY
        //
        // The problematic behavior was live scale mutation during gameplay,
        // not the presence of a window proc itself.
        AttachMainWindowProc(hwnd);
        EnsureHotPackPrewarmStarted();
    }
}

bool ShouldForceWindow(HWND hwnd) noexcept {
    return hwnd != nullptr && hwnd == g_main_window && IsTopLevelWindow(hwnd);
}

void ApplyBorderlessPlacement(HWND hwnd) {
    if (!IsTopLevelWindow(hwnd)) {
        return;
    }

    const RECT monitor_rect = GetMonitorRectForWindow(hwnd);
    const int width = monitor_rect.right - monitor_rect.left;
    const int height = monitor_rect.bottom - monitor_rect.top;

    DWORD style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_STYLE));
    DWORD ex_style = static_cast<DWORD>(GetWindowLongW(hwnd, GWL_EXSTYLE));
    style = NormalizeStyle(style);
    ex_style = NormalizeExStyle(ex_style);

    ::SetWindowLongW(hwnd, GWL_STYLE, static_cast<LONG>(style));
    ::SetWindowLongW(hwnd, GWL_EXSTYLE, static_cast<LONG>(ex_style));
    ::SetWindowPos(
        hwnd,
        nullptr,
        monitor_rect.left,
        monitor_rect.top,
        width,
        height,
        SWP_FRAMECHANGED | SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_NOZORDER);

    Log(
        "Applied borderless placement hwnd=%p x=%d y=%d w=%d h=%d",
        hwnd,
        monitor_rect.left,
        monitor_rect.top,
        width,
        height);

    ApplyUiScalingForWindow(hwnd);
}

LRESULT CALLBACK OverlayWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    switch (message) {
    case WM_ERASEBKGND:
        return 1;
    case WM_TIMER:
        if (w_param == kOverlayTimerId) {
            KillTimer(hwnd, kOverlayTimerId);
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint = {};
        HDC device_context = BeginPaint(hwnd, &paint);
        if (device_context != nullptr) {
            RECT client_rect = {};
            GetClientRect(hwnd, &client_rect);

            HBRUSH background = CreateSolidBrush(RGB(24, 24, 24));
            FillRect(device_context, &client_rect, background);
            DeleteObject(background);

            HPEN border_pen = CreatePen(PS_SOLID, 1, RGB(180, 180, 180));
            HPEN old_pen = reinterpret_cast<HPEN>(SelectObject(device_context, border_pen));
            HBRUSH old_brush = reinterpret_cast<HBRUSH>(SelectObject(device_context, GetStockObject(NULL_BRUSH)));
            Rectangle(device_context, client_rect.left, client_rect.top, client_rect.right, client_rect.bottom);
            SelectObject(device_context, old_brush);
            SelectObject(device_context, old_pen);
            DeleteObject(border_pen);

            HFONT old_font = nullptr;
            if (g_overlay_font != nullptr) {
                old_font = reinterpret_cast<HFONT>(SelectObject(device_context, g_overlay_font));
            }

            SetBkMode(device_context, TRANSPARENT);
            SetTextColor(device_context, RGB(245, 245, 245));

            wchar_t text[64] = {};
            GetWindowTextW(hwnd, text, static_cast<int>(std::size(text)));
            DrawTextW(device_context, text, -1, &client_rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

            if (old_font != nullptr) {
                SelectObject(device_context, old_font);
            }
        }
        EndPaint(hwnd, &paint);
        return 0;
    }
    default:
        break;
    }

    return DefWindowProcW(hwnd, message, w_param, l_param);
}

LRESULT CALLBACK MainWindowProc(HWND hwnd, UINT message, WPARAM w_param, LPARAM l_param) {
    WNDPROC original_proc = g_original_main_window_proc != nullptr ? g_original_main_window_proc : DefWindowProcW;

    if (HandleUiScaleHotkey(hwnd, message, w_param, l_param)) {
        return 0;
    }

    switch (message) {
    case WM_SIZE:
    case WM_DISPLAYCHANGE:
        ApplyUiScalingForWindow(hwnd);
        if (g_overlay_window != nullptr && IsWindowVisible(g_overlay_window)) {
            PositionOverlayWindow(hwnd);
        }
        break;
    case WM_NCDESTROY:
        if (hwnd == g_main_window) {
            // Process-detach logging proved unreliable in some clean exits, so
            // flush the loader summary while the main window is still tearing
            // down. The summary function is internally one-shot, so the later
            // detach path will simply become a no-op.
            FlushLoadProfilerSummaryInternal("main-window-destroy");
            SetWindowLongPtrW(hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_proc));
            LRESULT result = CallWindowProcW(original_proc, hwnd, message, w_param, l_param);
            g_original_main_window_proc = nullptr;
            g_main_window = nullptr;
            DestroyOverlayWindow();
            return result;
        }
        break;
    default:
        break;
    }

    return CallWindowProcW(original_proc, hwnd, message, w_param, l_param);
}

bool PatchImport(
    HMODULE module,
    const char* imported_module_name,
    const char* import_name,
    void* replacement,
    void** original_out) {
    if (module == nullptr) {
        return false;
    }

    auto* dos_header = reinterpret_cast<IMAGE_DOS_HEADER*>(module);
    if (dos_header->e_magic != IMAGE_DOS_SIGNATURE) {
        return false;
    }

    auto* nt_headers = reinterpret_cast<IMAGE_NT_HEADERS*>(
        reinterpret_cast<std::uint8_t*>(module) + dos_header->e_lfanew);
    if (nt_headers->Signature != IMAGE_NT_SIGNATURE) {
        return false;
    }

    const IMAGE_DATA_DIRECTORY& import_directory =
        nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (import_directory.VirtualAddress == 0 || import_directory.Size == 0) {
        return false;
    }

    auto* descriptor = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(
        reinterpret_cast<std::uint8_t*>(module) + import_directory.VirtualAddress);

    for (; descriptor->Name != 0; ++descriptor) {
        const char* module_name = reinterpret_cast<const char*>(
            reinterpret_cast<std::uint8_t*>(module) + descriptor->Name);
        if (_stricmp(module_name, imported_module_name) != 0) {
            continue;
        }

        auto* original_thunk = descriptor->OriginalFirstThunk != 0
            ? reinterpret_cast<IMAGE_THUNK_DATA*>(
                  reinterpret_cast<std::uint8_t*>(module) + descriptor->OriginalFirstThunk)
            : reinterpret_cast<IMAGE_THUNK_DATA*>(
                  reinterpret_cast<std::uint8_t*>(module) + descriptor->FirstThunk);
        auto* thunk = reinterpret_cast<IMAGE_THUNK_DATA*>(
            reinterpret_cast<std::uint8_t*>(module) + descriptor->FirstThunk);

        for (; original_thunk->u1.AddressOfData != 0; ++original_thunk, ++thunk) {
            if (IMAGE_SNAP_BY_ORDINAL(original_thunk->u1.Ordinal)) {
                continue;
            }

            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(
                reinterpret_cast<std::uint8_t*>(module) + original_thunk->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), import_name) != 0) {
                continue;
            }

            DWORD old_protect = 0;
            if (!VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), PAGE_READWRITE, &old_protect)) {
                Log("VirtualProtect failed for %s: %lu", import_name, GetLastError());
                return false;
            }

            if (original_out != nullptr && *original_out == nullptr) {
                *original_out = reinterpret_cast<void*>(thunk->u1.Function);
            }

            thunk->u1.Function = reinterpret_cast<ULONG_PTR>(replacement);

            DWORD restored_protect = 0;
            (void)VirtualProtect(&thunk->u1.Function, sizeof(thunk->u1.Function), old_protect, &restored_protect);
            (void)FlushInstructionCache(GetCurrentProcess(), nullptr, 0);

            Log("Patched %s!%s in module %p", imported_module_name, import_name, module);
            return true;
        }
    }

    return false;
}

bool InstallCodeDetour(void* target, void* hook, std::size_t patch_length, void** trampoline_out) {
    if (target == nullptr || hook == nullptr || trampoline_out == nullptr || patch_length < 5) {
        return false;
    }

    auto* target_bytes = reinterpret_cast<std::uint8_t*>(target);
    auto* trampoline = reinterpret_cast<std::uint8_t*>(
        VirtualAlloc(nullptr, patch_length + 5, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (trampoline == nullptr) {
        Log("VirtualAlloc failed for code detour: %lu", GetLastError());
        return false;
    }

    std::memcpy(trampoline, target_bytes, patch_length);

    const std::intptr_t return_delta =
        reinterpret_cast<std::intptr_t>(target_bytes + patch_length) -
        reinterpret_cast<std::intptr_t>(trampoline + patch_length + 5);
    trampoline[patch_length] = 0xE9;
    *reinterpret_cast<std::int32_t*>(trampoline + patch_length + 1) = static_cast<std::int32_t>(return_delta);

    DWORD old_protect = 0;
    if (!VirtualProtect(target_bytes, patch_length, PAGE_EXECUTE_READWRITE, &old_protect)) {
        Log("VirtualProtect failed for code detour target %p: %lu", target, GetLastError());
        VirtualFree(trampoline, 0, MEM_RELEASE);
        return false;
    }

    const std::intptr_t hook_delta =
        reinterpret_cast<std::intptr_t>(hook) - reinterpret_cast<std::intptr_t>(target_bytes + 5);
    target_bytes[0] = 0xE9;
    *reinterpret_cast<std::int32_t*>(target_bytes + 1) = static_cast<std::int32_t>(hook_delta);
    for (std::size_t index = 5; index < patch_length; ++index) {
        target_bytes[index] = 0x90;
    }

    DWORD restored_protect = 0;
    (void)VirtualProtect(target_bytes, patch_length, old_protect, &restored_protect);
    (void)FlushInstructionCache(GetCurrentProcess(), target_bytes, patch_length);

    *trampoline_out = trampoline;
    Log("Installed code detour target=%p hook=%p patchLength=%zu trampoline=%p", target, hook, patch_length, trampoline);
    return true;
}

HWND WINAPI HookCreateWindowExW(
    DWORD ex_style,
    LPCWSTR class_name,
    LPCWSTR window_name,
    DWORD style,
    int x,
    int y,
    int width,
    int height,
    HWND parent,
    HMENU menu,
    HINSTANCE instance,
    LPVOID param) {
    const bool candidate = IsWindowedCandidateCreate(style, width, height, parent);
    if (candidate) {
        RECT requested_rect = {x, y, x + width, y + height};
        const RECT monitor_rect = GetMonitorRectForRect(requested_rect);
        ex_style = NormalizeExStyle(ex_style);
        style = NormalizeStyle(style);
        x = monitor_rect.left;
        y = monitor_rect.top;
        width = monitor_rect.right - monitor_rect.left;
        height = monitor_rect.bottom - monitor_rect.top;
        Log(
            "CreateWindowExW forcing borderless style=0x%08lx ex=0x%08lx x=%d y=%d w=%d h=%d class=%ls title=%ls",
            style,
            ex_style,
            x,
            y,
            width,
            height,
            class_name != nullptr ? class_name : L"(null)",
            window_name != nullptr ? window_name : L"(null)");
    }

    HWND hwnd = g_original_create_window_ex_w(
        ex_style,
        class_name,
        window_name,
        style,
        x,
        y,
        width,
        height,
        parent,
        menu,
        instance,
        param);

    if (candidate && hwnd != nullptr) {
        TrackMainWindow(hwnd, "CreateWindowExW");
        ApplyBorderlessPlacement(hwnd);
    } else if (hwnd != nullptr && hwnd == g_main_window) {
        ApplyUiScalingForWindow(hwnd);
    }

    return hwnd;
}

LONG WINAPI HookSetWindowLongW(HWND hwnd, int index, LONG new_value) {
    if (index == GWL_STYLE && IsTopLevelWindow(hwnd) && IsWindowedCandidateStyle(static_cast<DWORD>(new_value))) {
        TrackMainWindow(hwnd, "SetWindowLongW(style)");
        new_value = static_cast<LONG>(NormalizeStyle(static_cast<DWORD>(new_value)));
        Log("Normalizing GWL_STYLE for hwnd=%p newStyle=0x%08lx", hwnd, static_cast<DWORD>(new_value));
    } else if (index == GWL_EXSTYLE && hwnd == g_main_window) {
        new_value = static_cast<LONG>(NormalizeExStyle(static_cast<DWORD>(new_value)));
        Log("Normalizing GWL_EXSTYLE for hwnd=%p newExStyle=0x%08lx", hwnd, static_cast<DWORD>(new_value));
    }

    const LONG result = g_original_set_window_long_w(hwnd, index, new_value);

    if ((index == GWL_STYLE || index == GWL_EXSTYLE) && ShouldForceWindow(hwnd)) {
        ApplyBorderlessPlacement(hwnd);
    } else if (hwnd == g_main_window) {
        ApplyUiScalingForWindow(hwnd);
    }

    return result;
}

BOOL WINAPI HookSetWindowPos(HWND hwnd, HWND insert_after, int x, int y, int cx, int cy, UINT flags) {
    if (ShouldForceWindow(hwnd)) {
        TrackMainWindow(hwnd, "SetWindowPos");
        const RECT monitor_rect = GetMonitorRectForWindow(hwnd);
        x = monitor_rect.left;
        y = monitor_rect.top;
        cx = monitor_rect.right - monitor_rect.left;
        cy = monitor_rect.bottom - monitor_rect.top;
        flags &= ~(SWP_NOMOVE | SWP_NOSIZE);
        flags |= SWP_FRAMECHANGED;
        Log("Forcing SetWindowPos hwnd=%p x=%d y=%d w=%d h=%d flags=0x%08x", hwnd, x, y, cx, cy, flags);
    }

    const BOOL result = g_original_set_window_pos(hwnd, insert_after, x, y, cx, cy, flags);
    if (result && hwnd == g_main_window) {
        ApplyUiScalingForWindow(hwnd);
    }

    return result;
}

BOOL WINAPI HookMoveWindow(HWND hwnd, int x, int y, int width, int height, BOOL repaint) {
    if (ShouldForceWindow(hwnd)) {
        TrackMainWindow(hwnd, "MoveWindow");
        const RECT monitor_rect = GetMonitorRectForWindow(hwnd);
        x = monitor_rect.left;
        y = monitor_rect.top;
        width = monitor_rect.right - monitor_rect.left;
        height = monitor_rect.bottom - monitor_rect.top;
        Log("Forcing MoveWindow hwnd=%p x=%d y=%d w=%d h=%d", hwnd, x, y, width, height);
    }

    const BOOL result = g_original_move_window(hwnd, x, y, width, height, repaint);
    if (result && hwnd == g_main_window) {
        ApplyUiScalingForWindow(hwnd);
    }

    return result;
}

LONG WINAPI HookChangeDisplaySettingsW(DEVMODEW* dev_mode, DWORD flags) {
    if (dev_mode != nullptr) {
        Log(
            "Passing through ChangeDisplaySettingsW width=%lu height=%lu bits=%lu flags=0x%08lx",
            dev_mode->dmPelsWidth,
            dev_mode->dmPelsHeight,
            dev_mode->dmBitsPerPel,
            flags);
    } else {
        Log("Passing through ChangeDisplaySettingsW restore flags=0x%08lx", flags);
    }

    return g_original_change_display_settings_w(dev_mode, flags);
}

bool InstallHooksInModule(HMODULE module) {
    bool installed_any = false;

    installed_any |= PatchImport(
        module,
        "USER32.dll",
        "CreateWindowExW",
        reinterpret_cast<void*>(&HookCreateWindowExW),
        reinterpret_cast<void**>(&g_original_create_window_ex_w));
    installed_any |= PatchImport(
        module,
        "USER32.dll",
        "SetWindowLongW",
        reinterpret_cast<void*>(&HookSetWindowLongW),
        reinterpret_cast<void**>(&g_original_set_window_long_w));
    installed_any |= PatchImport(
        module,
        "USER32.dll",
        "SetWindowPos",
        reinterpret_cast<void*>(&HookSetWindowPos),
        reinterpret_cast<void**>(&g_original_set_window_pos));
    installed_any |= PatchImport(
        module,
        "USER32.dll",
        "MoveWindow",
        reinterpret_cast<void*>(&HookMoveWindow),
        reinterpret_cast<void**>(&g_original_move_window));
    installed_any |= PatchImport(
        module,
        "USER32.dll",
        "ChangeDisplaySettingsW",
        reinterpret_cast<void*>(&HookChangeDisplaySettingsW),
        reinterpret_cast<void**>(&g_original_change_display_settings_w));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "CreateFileW",
        reinterpret_cast<void*>(&HookCreateFileW),
        reinterpret_cast<void**>(&g_original_create_file_w));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "ReadFile",
        reinterpret_cast<void*>(&HookReadFile),
        reinterpret_cast<void**>(&g_original_read_file));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "ReadFileEx",
        reinterpret_cast<void*>(&HookReadFileEx),
        reinterpret_cast<void**>(&g_original_read_file_ex));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "CreateFileMappingW",
        reinterpret_cast<void*>(&HookCreateFileMappingW),
        reinterpret_cast<void**>(&g_original_create_file_mapping_w));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "MapViewOfFile",
        reinterpret_cast<void*>(&HookMapViewOfFile),
        reinterpret_cast<void**>(&g_original_map_view_of_file));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "SetFilePointerEx",
        reinterpret_cast<void*>(&HookSetFilePointerEx),
        reinterpret_cast<void**>(&g_original_set_file_pointer_ex));
    installed_any |= PatchImport(
        module,
        "KERNEL32.dll",
        "CloseHandle",
        reinterpret_cast<void*>(&HookCloseHandle),
        reinterpret_cast<void**>(&g_original_close_handle));

    auto* base = reinterpret_cast<std::uint8_t*>(module);
    installed_any |= InstallCodeDetour(
        base + kRvaGetUiSourceRect,
        reinterpret_cast<void*>(&HookGetUiSourceRect),
        7,
        reinterpret_cast<void**>(&g_original_get_ui_source_rect));
    installed_any |= InstallCodeDetour(
        base + kRvaComputeUiSizePrimary,
        reinterpret_cast<void*>(&HookComputeUiSizePrimary),
        5,
        reinterpret_cast<void**>(&g_original_compute_ui_size_primary));
    installed_any |= InstallCodeDetour(
        base + kRvaComputeUiSizeSecondary,
        reinterpret_cast<void*>(&HookComputeUiSizeSecondary),
        5,
        reinterpret_cast<void**>(&g_original_compute_ui_size_secondary));

    return installed_any;
}

bool TryInstallHooksNow() {
    HMODULE target = GetModuleHandleW(L"empire.retail.dll");
    if (target == nullptr) {
        return false;
    }

    EnsureUiScaleConfigLoaded();

    if (InstallHooksInModule(target)) {
        InterlockedExchange(&g_hooks_installed, 1);
        Log("Installed borderless hooks into empire.retail.dll at %p", target);
        DumpUiScalingState("post-install");
    } else {
        Log("empire.retail.dll found at %p but no target imports were patched", target);
    }

    return true;
}

DWORD WINAPI HookInstallerThread(void*) {
    Log("Hook installer thread started");

    for (int attempt = 0; attempt < 300; ++attempt) {
        if (TryInstallHooksNow()) {
            return 0;
        }

        Sleep(100);
    }

    Log("Hook installer gave up waiting for empire.retail.dll");
    return 0;
}

}  // namespace

void StartHookInstaller() {
    if (InterlockedCompareExchange(&g_install_started, 1, 0) != 0) {
        return;
    }

    EnsureLoadProfilerStarted();

    if (TryInstallHooksNow()) {
        return;
    }

    HANDLE thread = CreateThread(nullptr, 0, &HookInstallerThread, nullptr, 0, nullptr);
    if (thread == nullptr) {
        Log("CreateThread failed in StartHookInstaller: %lu", GetLastError());
        return;
    }

    CloseHandle(thread);
}

void ShutdownHooks() {
    DestroyOverlayWindow();
    ReleasePinnedShadersPack();
    for (std::size_t index = 0; index < std::size(g_cached_map_windows); ++index) {
        (void)EvictCachedWindow(index, "shutdown");
    }
    FlushLoadProfilerSummaryInternal("process-detach");
}

}  // namespace shogun2
