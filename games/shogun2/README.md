# Shogun 2

This folder contains the Shogun 2 mod project from the repository.

Current stable features:

- true borderless window mode through the game's `windowed` path
- launch-time UI scaling from `shogun2_mod.ini`

Still experimental:

- loading-time optimization
- live in-game UI scale changes

## Build

Run from this folder:

```bat
build_x86.bat
```

Output:

- `build\bin\dinput8.dll`
- `build\bin\shogun2_mod.ini`

## Install

Copy these files into:

`D:\Program Files (x86)\Steam\steamapps\common\Total War SHOGUN 2`

- `build\bin\dinput8.dll`
- `build\bin\shogun2_mod.ini`

## Usage

For borderless:

- set Shogun 2 to `windowed`
- use your desktop resolution
- launch the game

For UI scaling:

- edit `shogun2_mod.ini`
- set `scale=1.00` to disable scaling
- use values above `1.00` to make the UI larger

UI scaling is applied at launch, not live during gameplay.

## Layout

- [src](/D:/TW_uiscaler/games/shogun2/src)
- [notes](/D:/TW_uiscaler/games/shogun2/notes)
- [tools](/D:/TW_uiscaler/games/shogun2/tools)
- [releases](/D:/TW_uiscaler/games/shogun2/releases)

## Release

Current stable release:

- [shogun2-borderless-ui-scale-v0.1.0](/D:/TW_uiscaler/games/shogun2/releases/shogun2-borderless-ui-scale-v0.1.0)
- [zip](/D:/TW_uiscaler/games/shogun2/releases/shogun2-borderless-ui-scale-v0.1.0.zip)

## Notes

Shared cross-game porting guidance is in:

- [docs/PORTING_HANDOFF.md](/D:/TW_uiscaler/docs/PORTING_HANDOFF.md)
