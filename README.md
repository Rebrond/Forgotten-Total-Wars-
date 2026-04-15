# Shogun 2 Borderless Prototype

This workspace now contains a first-pass `dinput8.dll` proxy for Total War: Shogun 2.

## Project objectives

1. Add a true borderless window mode to Shogun 2.
2. Add a usable UI scaling solution for modern high resolutions.
3. Investigate and reduce Shogun 2 loading times, using Warhammer 3 as a reference where useful.

## What it does

- forwards the real system `dinput8.dll` exports
- tries to patch `empire.retail.dll` immediately and falls back to a short wait loop if needed
- patches its USER32 imports for:
  - `CreateWindowExW`
  - `SetWindowLongW`
  - `SetWindowPos`
  - `MoveWindow`
  - `ChangeDisplaySettingsW`
- converts the normal windowed mode path into monitor-sized borderless windowing
- leaves the game's fullscreen display-mode path alone
- can also drive Shogun 2's hidden engine-side UI scaler from an external config file

## Build

Run:

```bat
build_x86.bat
```

Output:

- `build\bin\dinput8.dll`
- `build\bin\shogun2_mod.ini`

## Install for testing

Copy `build\bin\dinput8.dll` into:

`D:\Program Files (x86)\Steam\steamapps\common\Total War SHOGUN 2`

If you want UI scaling, copy `build\bin\shogun2_mod.ini` next to the DLL as well.

Remove the file to disable the mod.

## Current scope

This build is intended to be used with Shogun 2 set to windowed mode. The proxy then restyles that window into a monitor-sized borderless window.

Fullscreen mode is not overridden in this build.

UI scaling is controlled through `shogun2_mod.ini`:

```ini
[ui]
scale=1.25

[load]
map_cache=0
random_access_hint=0
prewarm_hot_packs=1
pin_shaders_pack=1
cache_window_mb=8
cache_min_file_mb=128
max_cached_windows=24
prewarm_delay_ms=5000
prewarm_chunk_mb=4
```

Behavior:

- `scale=1.0` disables the hidden scaler override
- values above `1.0` make the UI larger
- values below `1.0` make the UI smaller
- the current implementation clamps `scale` to the `0.5` to `2.0` range

Changes are applied by editing `shogun2_mod.ini` and launching the game with the desired value.

Live UI-scale hotkeys are intentionally disabled again for now. Shrinking the UI mid-session causes the layout to drift toward the top-left, so the stable path is still "edit INI, then launch".

Loading options:

- `map_cache=1` would enable the experimental pack-view cache
- `random_access_hint=1` adds a low-risk `FILE_FLAG_RANDOM_ACCESS` hint to read-only pack opens
- `prewarm_hot_packs=1` starts a low-priority background reader for the hottest small packs from the profiler
- `prewarm_hot_regions=1` enables an optional profile-driven hot-range prefetch inside `data.pack`
- `pin_shaders_pack=1` keeps a private read-only mapping of `shaders.pack` alive for the full session
- `cache_window_mb` controls the size of each cached mapping window
- `cache_min_file_mb` limits the cache to larger pack files and avoids the early boot packs
- `max_cached_windows` limits how many idle cached windows may stay alive
- `prewarm_delay_ms` waits before the background prewarm starts, so it can run while you are in menus instead of fighting initial boot
- `prewarm_chunk_mb` controls the sequential read chunk size for that prewarm worker

The pack-view cache experiment is currently disabled by default because the first two test passes were unstable. Leave `map_cache=0` for now.

`random_access_hint=0` is now the default again. The hint was applied correctly in testing, but it did not produce a meaningful subjective improvement or a clear shift in the profiler totals. Keep it off unless you want to compare it manually.

`prewarm_hot_packs=1` is the first loading optimization. Based on the profiler, a few small packs create disproportionate churn for their size. The DLL reads `boot.pack`, `bp_orig.pack`, and `large_font.pack` once, sequentially, on a low-priority background thread after the main window appears.

`prewarm_hot_regions=1` is the first targeted large-pack experiment. The current build prefetches three profile-derived hot ranges inside `data.pack`, chosen from the `top-readex-region` output rather than from guesswork.

In testing, that region prefetch did run correctly but did not produce a meaningful subjective improvement. It is left available as an option, but it is not enabled by default.

`pin_shaders_pack=1` is the shader-specific follow-up. `shaders.pack` is only about `18 MB`, but it generated over `10k` map calls and hundreds of megabytes of mapped-byte churn. The DLL now opens that file read-only, maps it once, touches its pages so they are faulted in, and keeps that mapping alive until shutdown. This is a stronger version of “warming” that still avoids rewriting the game’s own mapping behavior.

The DLL computes a virtual UI resolution from the current game window size, then writes Shogun 2's hidden engine settings:

Current loading status: the shader pin path runs correctly and may provide a modest improvement, but it did not materially reduce the game's internal shader-pack churn counters. Treat `prewarm_hot_packs=1` and `pin_shaders_pack=1` as low-risk helpers, not major loading fixes.

- `enable_ui_scaling`
- `ui_rect_override`
- `ui_x`
- `ui_y`
- `ui_width`
- `ui_height`

This means the current UI-scale prototype uses a real built-in Warscape scaling path, not only a font override.

This is still an INI-driven control, not a native slider inside Shogun 2's options menu. Adding a real menu entry would require patching the compiled options UI and its bindings separately.

The runtime log is written next to the proxy DLL as:

- `shogun2_borderless.log`

The log is a single file capped at 256 KB. Each new game launch starts a fresh file, and if the current session exceeds the size limit the same file is truncated and reused.

For UI-scaling debugging, look for `[ui-debug]` lines. They dump:

- the runtime base address of `empire.retail.dll`
- the resolved addresses of the hidden UI-scaling config wrappers
- each wrapper's dirty flag
- the read-back value after our writes
- the raw bytes around the value slot at offsets `0x58` through `0x5f`
- direct hits on the internal UI helpers:
  - `source-rect`
  - `ui-size-primary`
  - `ui-size-secondary`
- active size overrides on those helpers:
  - `ui-size-primary override`
  - `ui-size-secondary override`

For loading-time profiling, look for `[load]` lines. They currently cover:

- pack and data file opens
- synchronous `ReadFile` bursts, including blocking time
- `ReadFileEx` queued work
- file mapping creation and `MapViewOfFile` usage
- `UnmapViewOfFile` lifetimes
- cache hits, misses, and evictions for the experimental mapping cache
- per-file summaries when tracked handles close
- one process summary when the DLL detaches on game exit
- ranked per-path rollups at process exit:
  - `top-map` for the files with the most mapped-byte churn
  - `top-readex` for the files with the most queued async read volume
- ranked per-region rollups at process exit:
  - `top-map-region` for the hottest mapped offset buckets inside large packs
  - `top-readex-region` for the hottest async-read offset buckets inside large packs
- low-risk cache-warming activity:
  - `[prewarm] start`
  - `[prewarm] done`
  - `[prewarm] complete`
- targeted region prefetch:
  - `[region-prewarm] done`
- shader-pack pinning:
  - `[shader-pin] active`

The first useful artifacts are usually:

- the final `[load] summary ...` line
- the `top-map` lines
- the `top-readex` lines
- the `top-map-region` lines
- the `top-readex-region` lines

## Legacy UI asset tooling

The workspace also still contains earlier font-pack experimentation tools:

- `tools\make_font_overrides.ps1`
- `tools\build_font_pack.ps1`

It stages larger `.cuf` font files under:

- `mod_staging\font_overrides`

Example:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\make_font_overrides.ps1 -Preset conservative
```

The output folder mirrors the game's `font` tree and includes:

- overridden `.cuf` files
- `font_override_manifest.txt`

To build a pack from `mod_staging\pack_root`, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\build_font_pack.ps1
```

This is now secondary to the runtime scaler in `dinput8.dll`. Keep it for asset-side experiments only.
