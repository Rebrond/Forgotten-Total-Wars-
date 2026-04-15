# Forgotten Total Wars

This repository is for compatibility, quality-of-life, and engine-behavior patches for older Total War games.

The first shipped project is Shogun 2:

- true borderless window mode
- launch-time UI scaling from an external config

Loading-time research exists, but it is still experimental and is not part of the stable release target.

## Repository Layout

- [docs](/D:/TW_uiscaler/docs)
  - shared cross-game documents
  - [PORTING_HANDOFF.md](/D:/TW_uiscaler/docs/PORTING_HANDOFF.md)
- [games](/D:/TW_uiscaler/games)
  - one folder per supported game
- [games/shogun2](/D:/TW_uiscaler/games/shogun2)
  - source, notes, tools, and releases for Shogun 2

## Current Game

Shogun 2 lives here:

- [games/shogun2](/D:/TW_uiscaler/games/shogun2)

Stable release package:

- [release folder](/D:/TW_uiscaler/games/shogun2/releases/shogun2-borderless-ui-scale-v0.1.0)
- [release zip](/D:/TW_uiscaler/games/shogun2/releases/shogun2-borderless-ui-scale-v0.1.0.zip)

## Build

For Shogun 2:

1. Go to [games/shogun2](/D:/TW_uiscaler/games/shogun2)
2. Run `build_x86.bat`

Build output:

- `games\shogun2\build\bin\dinput8.dll`
- `games\shogun2\build\bin\shogun2_mod.ini`

## Scope

This public repository is intentionally curated. It includes:

- source code
- build scripts
- reverse-engineering notes
- release packages we built ourselves

It does not include:

- original game binaries
- extracted game assets
- Ghidra project databases
- downloaded third-party tools

## Future Work

Future Total War game patches should be added as siblings under `games/`, not mixed into the Shogun 2 tree.
