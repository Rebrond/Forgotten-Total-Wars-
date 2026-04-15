# Total War Porting Handoff

This document is the handoff for future sessions that want to port the current Shogun 2 work into other Total War games.

It is intentionally practical. It records what is already proven, what is Shogun-2-specific, and what a future session should verify again before reusing anything.

## Current proven result in Shogun 2

Stable:

- true borderless mode by converting the normal `windowed` path into monitor-sized borderless
- UI scaling from `shogun2_mod.ini` at launch time

Not stable enough to treat as shipped features:

- live in-game UI scale changes
- loading-time improvements

Current stable release package:

- [release folder](/D:/TW_uiscaler/releases/shogun2-borderless-ui-scale-v0.1.0)
- [release zip](/D:/TW_uiscaler/releases/shogun2-borderless-ui-scale-v0.1.0.zip)

## Code map

Main files:

- [src/dinput8_proxy.cpp](/D:/TW_uiscaler/src/dinput8_proxy.cpp)
- [src/borderless_hooks.cpp](/D:/TW_uiscaler/src/borderless_hooks.cpp)
- [src/common.cpp](/D:/TW_uiscaler/src/common.cpp)
- [dinput8_proxy.def](/D:/TW_uiscaler/dinput8_proxy.def)
- [build_x86.bat](/D:/TW_uiscaler/build_x86.bat)

What each file does:

- `dinput8_proxy.cpp`
  - exports the real DirectInput entry points
  - loads the system `dinput8.dll`
  - starts hook installation
- `borderless_hooks.cpp`
  - borderless window hooks
  - UI scaling hooks
  - loading profiler and experiments
  - all RE-derived offsets and detours
- `common.cpp`
  - runtime log creation and log truncation behavior

## Shogun 2-specific facts

These are reference points, not portable assumptions.

Real game code target:

- [empire.retail.dll](/D:/TW_uiscaler/empire.retail.dll)

Launcher stub:

- [shogun2.exe](/D:/TW_uiscaler/shogun2.exe)

Architecture:

- `x86`

Useful borderless reference points in `empire.retail.dll`:

- window creation path around `0x113E0BC0`
- fullscreen-exit / display-restore path around `0x114015F0`
- runtime style-switch path around `0x11416740`

Useful UI-scaling reference points in `empire.retail.dll`:

- hidden config wrappers:
  - `enable_ui_scaling`
  - `ui_rect_override`
  - `ui_x`
  - `ui_y`
  - `ui_width`
  - `ui_height`
  - `ui_rect_left`
  - `ui_rect_top`
  - `ui_rect_width`
  - `ui_rect_height`
- helper functions:
  - `source-rect`
  - `ui-size-primary`
  - `ui-size-secondary`

Important warning:

- the hidden config writes alone were not enough
- visible UI scaling only worked after the helper functions were also detoured to return the virtual scaled size

## What is likely reusable across another Total War title

- the `dinput8.dll` proxy pattern
- the general borderless approach:
  - intercept `CreateWindowExW`
  - intercept `SetWindowLongW` or `SetWindowLongPtrW`
  - intercept `SetWindowPos`
  - intercept `MoveWindow`
  - optionally intercept `ChangeDisplaySettingsW`
- the general UI-scaling search strategy:
  - find hidden UI config wrappers
  - find the root UI size helpers
  - verify by logging reads and writes
- the logging and profiler infrastructure

## What is not safe to reuse blindly

- any RVA from Shogun 2
- `x86` calling convention assumptions
- the config object layout:
  - dirty flag at `+0x59`
  - value slot at `+0x5C`
- the decision to use only the `windowed -> borderless` path
- the exact import names:
  - some titles may use `SetWindowLongPtrW`
  - some may not call `ChangeDisplaySettingsW` at all

## Recommended porting order

Do not port everything at once.

1. Make the proxy load cleanly in the target game.
2. Solve borderless first.
3. Solve UI scaling second.
4. Only after both are stable, look at loading-time work.

Reason:

- borderless is the lowest-risk feature
- it proves the injection path and window ownership
- UI scaling depends on deeper engine-specific RE
- loading-time work is the riskiest and least proven part of this project

## Porting checklist for a new title

For each game, record these before changing code:

- game name
- install path
- real code module:
  - EXE or DLL
- architecture:
  - `x86` or `x64`
- does local `dinput8.dll` proxy loading work?
- main window class name
- main window title
- import presence:
  - `CreateWindowExW`
  - `SetWindowLongW`
  - `SetWindowLongPtrW`
  - `SetWindowPos`
  - `MoveWindow`
  - `ChangeDisplaySettingsW`
  - `ChangeDisplaySettingsExW`
  - `MonitorFromWindow`
  - `MonitorFromRect`
  - `GetMonitorInfoW`
- config strings found:
  - `ui_scale`
  - `enable_ui_scaling`
  - `ui_rect_override`
  - `ui_width`
  - `ui_height`
  - `ui_text_scale`
  - `ui_radar_scale`
  - `ui_unit_id_scale`

## Borderless RE workflow for a new title

1. Identify the real module.
   - Some Total War games use a launcher EXE plus a large engine DLL.
   - Do not waste time REing a small launcher stub.

2. Check imports first.
   - If the module imports `ChangeDisplaySettingsW`, it probably still has an exclusive fullscreen path.
   - If it does not, the title may already be using a window-management fullscreen model.

3. Find the window creation path.
   - Xrefs from:
     - `CreateWindowExW`
     - `SetWindowLongW`
     - `SetWindowLongPtrW`
     - `SetWindowPos`
     - `MoveWindow`
     - `ChangeDisplaySettingsW`

4. Verify the easiest route.
   - The safest first target is usually:
     - game set to `windowed`
     - proxy forces popup-style borderless and monitor-sized placement

5. Only suppress fullscreen mode-switching if necessary.
   - Some games will work with pure style/placement changes.
   - Others still need `ChangeDisplaySettingsW` suppressed.

## UI-scaling RE workflow for a new title

1. Search strings before patching code.
   - Look for:
     - `ui_scale`
     - `enable_ui_scaling`
     - `ui_rect_override`
     - `ui_width`
     - `ui_height`
     - `ui_text_scale`
     - `ui_radar_scale`
     - `ui_unit_id_scale`

2. If the game already has a real `ui_scale` setting:
   - prefer enabling or exposing that path
   - do not reinvent Shogun 2's lower-level hook if the title already solved it internally

3. If only low-level UI rect strings exist:
   - find the config-wrapper objects
   - verify their layout with logging before writing anything

4. After config writes, verify whether the visible UI changes.
   - If nothing changes, assume there is a deeper root size helper that must also be detoured.

5. Detour the root helper only after proving it with logs.
   - In Shogun 2, that was the difference between:
     - valid writes
     - and visible UI scaling

6. Avoid live scaling until the game is known to refresh layouts cleanly.
   - In Shogun 2, shrinking UI live caused drift toward the top-left
   - the stable solution remained:
     - edit INI
     - launch game

## Logging strategy that worked

For window work:

- log the first `CreateWindowExW` match
- log tracked main window handle
- log forced style and monitor placement

For UI work:

- log config object addresses
- log read-back values after writes
- log dirty flags
- log helper function hits and returned sizes

For loading work:

- keep profiling separate from optimization
- add measurement first
- compare at least one clean baseline run before deciding anything

## Ghidra workflow that worked

Working approach:

- identify the real module first
- import that module into Ghidra
- use xrefs from imports and strings instead of starting from a full blind walk
- use Warhammer 3 only as a conceptual reference, not as an offset source

Practical note:

- Warhammer 3 was useful for understanding what a newer solution looks like
- it was not useful for directly reusing RVAs in Shogun 2

## When to use Warhammer 3 as a reference

Use it for:

- modern fullscreen/window-management behavior
- evidence that a title may prefer desktop-resolution borderless over exclusive mode
- examples of higher-level UI-scale settings:
  - `ui_scale`
  - `ui_text_scale`
  - `ui_radar_scale`
  - `ui_unit_id_scale`

Do not use it for:

- direct offset reuse
- direct structure layout reuse
- assuming the same helper functions exist in the older title

## Best candidates for future ports

Prefer titles with the highest overlap to this work:

- Warscape-era titles
- titles where borderless is still missing or weak
- titles where a hidden UI scaling path may exist but is not exposed
- titles where a `dinput8.dll` proxy can still load locally

Avoid starting with:

- titles that already have strong native borderless and UI scale support
- titles that force a very different 64-bit code path unless there is a strong reason to start there

## Suggested future-session prompt

Use something close to this:

> We already have a working Shogun 2 `dinput8.dll` mod for borderless window mode and launch-time UI scaling. Use `notes/PORTING_HANDOFF.md` as the starting map. First identify the real game module, architecture, and whether local `dinput8.dll` proxy loading works. Then port borderless first, verify it in-game, and only after that begin UI-scale RE. Do not assume Shogun 2 RVAs or config-object layouts carry over.

## Final recommendation

For future ports, keep the discipline from this project:

- prove injection
- solve borderless
- prove hidden UI variables
- prove the visible UI root
- only then call the port successful

That order is what kept Shogun 2 manageable.
