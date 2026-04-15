# Warhammer 3 Borderless Comparison

## Goal

Determine how `Warhammer3.exe` handles modern fullscreen/windowed behavior and how to reproduce that behavior in Shogun 2.

## Warhammer 3 findings

### User-facing config

From `C:\Users\Rebrond\AppData\Roaming\The Creative Assembly\Warhammer3\scripts\preferences.script.txt`:

- `gfx_fullscreen false`
- `x_res 3440`
- `y_res 1440`
- `x_pos 0`
- `y_pos 0`
- `fix_res true`
- `fix_window_pos true`
- `ui_scale 1.6`

Important points:

- Warhammer 3 still exposes the old `gfx_fullscreen` toggle and fixed window geometry keys.
- It does not expose a separate `borderless` key.
- It does expose `ui_scale`, which Shogun 2 lacks.

### Win32 API profile

`Warhammer3.exe` imports the APIs expected for modern borderless-style window management:

- `CreateWindowExW`
- `SetWindowLongPtrW`
- `GetWindowLongPtrW`
- `AdjustWindowRectEx`
- `MonitorFromWindow`
- `MonitorFromRect`
- `GetMonitorInfoW`
- `SetWindowPos`
- `MoveWindow`
- `ShowWindow`

Notably, Warhammer 3 does not import `ChangeDisplaySettingsW`.

Interpretation:

- Warhammer 3 is very likely not using an old exclusive fullscreen display-mode switch for its normal fullscreen path.
- The more likely model is:
  - keep the desktop display mode unchanged
  - create or restyle the game window
  - size and place it against the active monitor rect
  - treat `gfx_fullscreen` as a mode/style decision, not as a forced display-mode change

This is consistent with how true borderless fullscreen is usually implemented.

## Shogun 2 findings to compare against

From earlier `empire.retail.dll` reversing:

- `0x113E0BC0`: main window creation path
  - chooses styles `0x90000000`, `0x14C20000`, or `0x00CF0000`
  - calls `AdjustWindowRect`, `MonitorFromRect`, `GetMonitorInfoW`, `CreateWindowExW`
- `0x114015F0`: fullscreen exit / display restore path
  - calls `ShowWindow`
  - calls `UpdateWindow`
  - calls `ChangeDisplaySettingsW(NULL, 0x40000000)`
- `0x11416740` block: runtime mode-switch path
  - uses `SetWindowLongW`
  - uses `MoveWindow`
  - uses `SetWindowPos`
  - swaps between popup and decorated styles

Interpretation:

- Shogun 2 already has most of the window-style and monitor-placement logic needed for borderless.
- The main legacy behavior that blocks "true borderless" is the display-mode path around `0x114015F0`.
- In other words, Shogun 2 appears architecturally close to Warhammer 3, but still coupled to exclusive fullscreen behavior.

## Practical conclusion

Warhammer 3's modern behavior is best understood as:

1. keep the monitor in desktop mode
2. use Win32 style changes plus monitor-aware positioning
3. resize the game window to monitor bounds
4. avoid `ChangeDisplaySettingsW`

Shogun 2 can be modernized by forcing that same model onto its existing window pipeline.

## Implementation plan for Shogun 2

### Preferred delivery method

Use a runtime loader patch first, not a permanent binary rewrite.

Recommended vehicle:

- `dinput8.dll` proxy/loader placed in the Shogun 2 game directory

Reason:

- reversible
- easier to test
- no hardcoded binary rewrite on the first pass
- lets us iterate on heuristics for the main game window

### Behavior to force

When the game requests fullscreen:

1. do not allow exclusive display-mode switching
2. force popup-style borderless windowing
3. place the window on the active monitor
4. size it to the monitor rect or work area
5. keep desktop resolution unchanged

### Shogun 2 hook targets

High-value API hooks:

- `CreateWindowExW`
- `SetWindowLongW`
- `SetWindowPos`
- `MoveWindow`
- `ChangeDisplaySettingsW`

High-value native code regions:

- `0x113E0BC0`
  - force the fullscreen request to use the borderless popup-style branch
- `0x114015F0`
  - suppress or neutralize `ChangeDisplaySettingsW`
- `0x11416740` block
  - keep style swaps and placement logic aligned with borderless popup behavior

### Suggested runtime behavior

- Detect the main Shogun 2 window after creation.
- If fullscreen is requested:
  - set `GWL_STYLE` to popup-style borderless
  - optionally remove extended frame styles that introduce borders
  - query the target monitor with `MonitorFromWindow` or `MonitorFromRect`
  - query bounds with `GetMonitorInfoW`
  - call `SetWindowPos` with top-left monitor coordinates and monitor width/height
- Intercept `ChangeDisplaySettingsW` and return success without switching modes when the call belongs to the fullscreen transition path.

## Why this is the right first step

- It matches the modern Warhammer 3 model more closely than trying to patch resolutions alone.
- It solves the real "fake fullscreen" problem first.
- It also gives us the runtime injection framework needed for later UI-scaling work.

## Next step

Build the Shogun 2 `dinput8.dll` loader and wire up:

- import forwarding to the real system `dinput8.dll`
- a startup hook layer
- first-pass hooks for `CreateWindowExW` and `ChangeDisplaySettingsW`

After that, test:

- startup in fullscreen
- alt-tab
- multi-monitor placement
- resolution changes from the in-game settings menu
