# Loading-Time Investigation

## Current status

Borderless mode works.

UI scaling works from `shogun2_mod.ini`, and live hotkey changes are disabled again for now so scale is applied only from the INI at launch.

The next project objective is reducing Shogun 2 loading times.

## First comparison: Shogun 2 vs Warhammer 3

### Shogun 2 already has some non-trivial I/O primitives

From `empire.retail.dll` imports:

- `CreateFileW`
- `ReadFile`
- `ReadFileEx`
- `CreateFileMappingW`
- `MapViewOfFile`
- `SetFilePointerEx`
- `WaitForMultipleObjects`
- `CreateThread`
- `tbb.dll`

Interpretation:

- Shogun 2 is not a purely naive synchronous file loader.
- The engine already has support for async reads, file mapping, worker threads, and TBB tasking.
- So the loading-time problem is probably not solved by a trivial "just use memory mapping" patch.

### Warhammer 3 has a more modern loading/runtime substrate

From `Warhammer3.exe` imports:

- `CreateThreadpoolWork`
- `SubmitThreadpoolWork`
- `WaitForThreadpoolWorkCallbacks`
- `InitializeSRWLock`
- `AcquireSRWLockExclusive`
- `ReleaseSRWLockExclusive`
- `InitializeConditionVariable`
- `SleepConditionVariableCS`
- `SleepConditionVariableSRW`
- `MapViewOfFileEx`
- `FlushViewOfFile`
- `SetFileInformationByHandle`
- `GetFileInformationByHandleEx`
- `ReadDirectoryChangesW`
- `NtCreateSection`
- `NtMapViewOfSection`
- `NtUnmapViewOfSection`
- `GetSystemFileCacheSize`
- `SetThreadIdealProcessor`
- `GetLogicalProcessorInformationEx`

Interpretation:

- Warhammer 3 has a clearly more advanced thread scheduling and file/cache pipeline.
- It uses threadpool work rather than only manual threads.
- It uses newer lock primitives and condition variables.
- It reaches into lower-level section mapping APIs, which suggests a more optimized asset streaming layer.
- It also has better system-topology awareness, which points to more deliberate CPU and I/O scheduling.

## What this likely means

The main gap is probably not one API call. It is likely one or more of:

- better parallel staging of load work
- better pack-file mapping / paging strategy
- less serialized decompression or asset setup
- better worker scheduling and wake-up behavior
- more deliberate file cache interaction

In other words:

- Warhammer 3 looks architecturally better at loading
- Shogun 2 probably needs profiling first, not blind patching

## Best next step

The first implementation step is now done: the existing `dinput8.dll` mod has a lightweight loader profiler hooked into `empire.retail.dll` imports for:

- `CreateFileW`
- `ReadFile`
- `ReadFileEx`
- `CreateFileMappingW`
- `MapViewOfFile`
- `SetFilePointerEx`
- `CloseHandle`

It traces:

- `.pack` file opens
- `ReadFile` / `ReadFileEx` usage
- file mapping usage
- per-handle total bytes read
- read burst sizes
- per-handle blocking time for synchronous reads
- one process summary at DLL detach

That will tell us whether Shogun 2 is:

- I/O bound
- decompression bound
- serialized on worker waits
- or spending most of its time on higher-level asset initialization after reads complete

## Current recommendation

Do not patch loading behavior yet.

Run the new profiler on a real long load, then choose the patch target from evidence.

The most useful log artifacts for the next pass are:

- the final `[load] summary ...` line
- the biggest `[load] close ... path=...pack` lines

## Current experiment

The next step is an experimental `MapViewOfFile` cache in the `dinput8.dll` mod.

Reasoning:

- the first profiler run showed very little plain `ReadFile` traffic
- it also showed extremely high `MapViewOfFile` churn
- that makes mapping overhead a better candidate than classic read buffering

Current experiment design:

- only apply the cache to interesting pack-style files
- keep a limited number of larger cached windows alive after client unmaps
- satisfy nearby future `MapViewOfFile` requests from those windows
- fall back to the original exact mapping path if the cache path cannot be tracked safely

Config:

```ini
[load]
map_cache=0
random_access_hint=0
cache_window_mb=8
cache_min_file_mb=128
max_cached_windows=24
```

What to compare in the next test:

- subjective load time before vs after
- `[load] summary ...`
- cache hit / miss / eviction counts
- `mapApi` vs `unmapApi` time totals

Safety note:

- the first cache pass crashed early because it also touched small startup packs
- the current version gates the experiment to larger read-only pack files only
- after another crash, the cache experiment has been pulled back from the active build and left disabled by default

Current low-risk experiment:

- keep `map_cache=0`
- optionally apply `FILE_FLAG_RANDOM_ACCESS` to read-only pack opens

Reasoning:

- the stable profiler still shows very high mapping churn
- that access pattern looks more random than sequential
- `FILE_FLAG_RANDOM_ACCESS` is only a cache-manager hint, so it should be much safer than rewriting mapping behavior

Latest result:

- the hint was applied correctly to pack opens
- subjective loading did not improve
- process-level counters only moved slightly, which is not enough to justify leaving the hint enabled by default

Current profiling focus:

- keep both loading experiments off by default
- add ranked per-path rollups at process exit:
  - `top-map` for mapped-byte churn
  - `top-readex` for queued async read volume
- use those rollups to identify which concrete files are worth targeting next

## Latest profiler result

The first complete shutdown summary now works. The important findings from the current session are:

- session summary:
  - `94.1s` total profiled uptime
  - `33,337` `MapViewOfFile` calls
  - `3.184 GB` total mapped bytes
  - `143` `ReadFileEx` calls
  - `1.519 GB` queued through `ReadFileEx`
- top mapped-byte files:
  - `models.pack`: `1.341 GB`, `9,805` map calls
  - `models2.pack`: `598.7 MB`, `7,737` map calls
  - `terrain.pack`: `346.2 MB`, `2,784` map calls
  - `shaders.pack`: `346.1 MB`, `10,050` map calls
  - `data_fots.pack`: `274.0 MB`, `639` map calls
  - `data.pack`: `131.5 MB`, `1,027` map calls
- top async-read files:
  - `data.pack`: `1.346 GB` via `ReadFileEx`
  - `sound.pack`: `134.1 MB`
  - `data_fots.pack`: `34.1 MB`

Interpretation:

- the loading problem is real, but it is split across two different behaviors:
  - heavy map churn for models, shaders, and terrain
  - heavy async streaming for data/sound packs
- that makes a single flag-based fix unlikely to help much
- the smaller hot packs are the safest first optimization target because they can be warmed without rewriting the engine’s mapping behavior

## Current low-risk optimization

The DLL now has an optional background prewarm worker:

- starts after the main window appears
- waits a few seconds by default so it runs while the player is in menus
- sequentially reads these small hot packs once:
  - `boot.pack`
  - `bp_orig.pack`
  - `shaders.pack`
  - `large_font.pack`

Config:

```ini
[load]
prewarm_hot_packs=1
pin_shaders_pack=1
prewarm_delay_ms=5000
prewarm_chunk_mb=4
```

Reasoning:

- `shaders.pack` is only about `18 MB` but produced `346 MB` of mapped-byte churn and over `10k` map calls
- `boot.pack`, `bp_orig.pack`, and `large_font.pack` are also small enough to warm cheaply
- this is far lower risk than another `MapViewOfFile` interception attempt

## Shader-specific follow-up

The next step is now more aggressive, but still low risk compared with hook-based caching:

- keep `shaders.pack` mapped read-only for the whole session
- touch one byte per page so the file-backed pages are faulted in immediately
- leave that mapping alive until shutdown

Reasoning:

- the profiler shows `shaders.pack` is uniquely attractive for this:
  - only about `18 MB` on disk
  - more than `10k` map calls
  - about `346 MB` of mapped-byte churn
- unlike `models.pack` or `terrain.pack`, keeping an `18 MB` file resident is cheap
- this should be a stronger version of cache warming for the most obviously hot small archive

## Latest shader-pin result

The shader-pack pinning pass ran correctly:

- `[shader-pin] active` was logged for `data\shaders.pack`
- the file was mapped once, faulted in, and held alive for the whole session
- the background prewarm still ran for:
  - `boot.pack`
  - `bp_orig.pack`
  - `large_font.pack`

Latest session summary:

- `84.1s` total profiled uptime
- `33,343` `MapViewOfFile` calls
- `3.194 GB` total mapped bytes
- `138` `ReadFileEx` calls
- `1.498 GB` queued through `ReadFileEx`

Latest top mapped-byte files:

- `models.pack`: `1.350 GB`, `9,805` map calls
- `models2.pack`: `598.9 MB`, `7,737` map calls
- `terrain.pack`: `347.5 MB`, `2,792` map calls
- `shaders.pack`: `346.1 MB`, `10,050` map calls
- `data_fots.pack`: `274.0 MB`, `639` map calls
- `data.pack`: `131.2 MB`, `1,025` map calls

Latest top async-read files:

- `data.pack`: `1.346 GB` via `ReadFileEx`
- `sound.pack`: `113.4 MB`
- `data_fots.pack`: `34.1 MB`

Interpretation:

- shader pinning is working technically
- end-to-end session time is lower than the first full baseline, but the internal `shaders.pack` churn counters barely moved
- so shader pinning may help a little, but it is not the main fix
- the dominant loading cost is still:
  - heavy map churn in `models.pack`, `models2.pack`, and `terrain.pack`
  - heavy async streaming in `data.pack`

Current recommendation:

- keep `prewarm_hot_packs=1` and `pin_shaders_pack=1` as low-risk optional helpers
- do not expect a major loading breakthrough from them
- if loading work continues, the next serious target is region-level profiling or prefetch for `models.pack`, `models2.pack`, and `data.pack`

## Current profiler upgrade

The DLL now also emits region-level rollups for pack offsets:

- `top-map-region`
- `top-readex-region`

Implementation details:

- fixed `16 MB` buckets
- `MapViewOfFile` requests are split into the buckets they touch
- `ReadFileEx` requests are split using the `OVERLAPPED` file offset
- each region line reports:
  - file label
  - bucket start offset
  - bucket size
  - mapped-byte or async-read volume

Reasoning:

- file-level totals told us *which packs* are hot
- region-level totals should tell us *which parts* of `models.pack`, `models2.pack`, and `data.pack` are hot enough to prefetch selectively

## First targeted region prefetch

The first concrete region-prefetch experiment now targets only `data.pack`.

Current ranges:

- `0x74000000` length `0x02000000`
- `0x7a000000` length `0x09000000`
- `0x88000000` length `0x02000000`

Reasoning:

- the new `top-readex-region` output showed a real cluster inside `data.pack`
- that is a better target than `models.pack`, whose hot mapped regions are much more scattered
- the implementation stays low risk because it still does not alter the engine's own mapping logic; it just reads those ranges in the background

Log markers:

- `[region-prewarm] done`

Latest result:

- the targeted `data.pack` region prefetch ran correctly
- end-to-end session time moved by less than a second versus the previous region-profiling run
- subjective loading felt unchanged

Interpretation:

- this path is technically valid, but not strong enough to matter in practice
- leave it optional for future comparison, but do not treat it as a default optimization
