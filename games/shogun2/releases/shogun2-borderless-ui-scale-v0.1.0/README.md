# Shogun 2 Borderless + UI Scale v0.1.0

This package contains the current stable features from the project:

- true borderless window mode
- launch-time UI scaling from an external config file

Loading-time experiments are included in the DLL, but they are disabled by default in the shipped config because they did not produce a clear practical improvement yet.

## Install

Copy these files into your Shogun 2 game folder:

`D:\Program Files (x86)\Steam\steamapps\common\Total War SHOGUN 2`

Files:

- `dinput8.dll`
- `shogun2_mod.ini`

## Use

For borderless mode:

1. Set Shogun 2 to `windowed` mode.
2. Set the game resolution to your desktop resolution.
3. Launch the game.

The DLL will convert the normal windowed path into a monitor-sized borderless window.

For UI scaling:

Edit `shogun2_mod.ini`:

```ini
[ui]
scale=1.00
```

Examples:

- `1.00` = disabled
- `1.15` = slightly larger UI
- `1.25` = good starting point for high resolutions
- `1.40` = aggressive scaling

Then launch the game. UI scale is applied at startup, not live during gameplay.

## Default config

The packaged config keeps all loading experiments off by default:

- `map_cache=0`
- `random_access_hint=0`
- `prewarm_hot_packs=0`
- `prewarm_hot_regions=0`
- `pin_shaders_pack=0`

They remain available for future testing, but are not part of the stable release.

## Known limitations

- Use `windowed` mode for borderless. Fullscreen is not overridden in this release package.
- Loading-screen text and bars are not affected by the UI scaler.
- Live in-game UI scale changes are intentionally disabled.
- The mod writes `shogun2_borderless.log` next to the DLL for debugging.

## Uninstall

Remove:

- `dinput8.dll`
- `shogun2_mod.ini`
