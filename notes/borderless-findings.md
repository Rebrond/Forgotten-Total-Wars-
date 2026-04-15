# Shogun 2 Borderless Findings

Target binary: `empire.retail.dll`
Ghidra project: `shogun2-borderless`

## Confirmed borderless/fullscreen-relevant code

### `FUN_113e0bc0` at `0x113E0BC0`

Main window creation path.

Observed behavior:

- Calls `AdjustWindowRect`
- Calls `MonitorFromRect`
- Calls `GetMonitorInfoW`
- Calls `CreateWindowExW`
- Stores chosen window style at `param_1[0x1A731]`

Decompiled behavior summary:

- If `*(config + 0x1c) == 0`, style becomes `0x90000000`
- Else style becomes `0x14c20000`
- If that same config flag is set and `param_6 == 0`, style becomes `0x00CF0000`
- Creates the window using monitor-aware coordinates and adjusted size

Likely interpretation:

- `0x90000000` = popup-style fullscreen/exclusive-style shell window
- `0x14c20000` / `0x00CF0000` = normal decorated windowed modes

This is one of the best hook points for forcing borderless creation.

### `FUN_114015F0` at `0x114015F0`

Display-mode exit / restore path.

Observed behavior:

- Calls `ShowWindow(hwnd, 6)`
- Calls `UpdateWindow(hwnd)`
- Calls `ChangeDisplaySettingsW(NULL, 0x40000000)`

Decompiled behavior summary:

- Writes mode flag to `param_1 + 0x69871`
- If two gating flags are clear and `param_2 == 0`, it restores desktop display settings

This is a primary hook point for suppressing exclusive fullscreen mode changes.

### Style / move / resize block around `0x11416740`

Not currently defined as a Ghidra function, but clearly part of the runtime mode-switch code.

Observed API usage in this block:

- `SetWindowLongW(hwnd, GWL_STYLE, style)` via calls at:
  - `0x1141674C`
  - `0x11416893`
  - `0x114168A4`
  - `0x114168B3`
- `MoveWindow` at `0x114167A4`
- `GetWindowInfo` at `0x114167B5`
- `GetWindowLongW` at `0x11416973`
- `SetWindowPos` at `0x11416903`
- `SetWindowPos` at `0x114169A4`

Relevant constants seen directly in disassembly:

- `0x90000000`
- `0x14C20000`
- `0x00CF0000`
- `GWL_STYLE = -16`
- `GWL_EXSTYLE = -20`

This block appears to:

- swap between popup and decorated window styles
- recompute window rects with `AdjustWindowRect`
- place the window using monitor-aware geometry helpers
- commit the size/position with `MoveWindow` / `SetWindowPos`

This is the strongest runtime patch region for true borderless mode switching.

### `FUN_113F5000` at `0x113F5000`

Monitor rect helper.

Observed behavior:

- Calls `GetWindowRect`
- Calls `MonitorFromRect`
- Calls `GetMonitorInfoW`

Decompiled behavior summary:

- If `param_3 != 0`, sets rect origin to monitor work-area top-left
- Else centers a rect inside the current monitor region

This is relevant if we want to force placement onto the current monitor after style changes.

### `FUN_11401650` at `0x11401650`

Mode-application helper called from the state machine around `0x11401F2A`.

Observed behavior:

- Reads client rect from the live game window
- Compares current client size against target config values
- Calls helpers that appear to normalize or apply the chosen mode

This looks like part of the transition pipeline when toggling window/fullscreen state.

## Practical implementation direction

Best first implementation: runtime loader hook, not a direct binary patch.

Recommended strategy:

1. Ship a `dinput8.dll` proxy/loader in the Shogun 2 directory.
2. Intercept:
   - `CreateWindowExW`
   - `SetWindowLongW`
   - `SetWindowPos`
   - `MoveWindow`
   - `ChangeDisplaySettingsW`
3. Force popup borderless style when the game requests fullscreen.
4. Resize the main window to the active monitor bounds.
5. Suppress `ChangeDisplaySettingsW` calls that would enter or restore exclusive mode.

Fallback strategy:

- Patch the code around `0x113E0BC0`, `0x114015F0`, and the `0x11416740` block directly after runtime behavior is validated.

## Notes

- `shogun2.exe` is only a launcher stub. The real engine target is `empire.retail.dll`.
- Ghidra analysis of `empire.retail.dll` completed successfully in the local project before the interactive bridge startup issues.
