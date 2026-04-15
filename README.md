# Forgotten Total Wars

Community patches for older Total War games, starting with Shogun 2 borderless window mode and UI scaling.

This repository is for compatibility, quality-of-life, and engine-behavior patches for older Total War games.

The first shipped project is Shogun 2:

- true borderless window mode
- launch-time UI scaling from an external config

Loading-time research exists, but it is still experimental and is not part of the stable release target.

## Repository Layout

- [docs](docs/)
  - shared cross-game documents
  - [PORTING_HANDOFF.md](docs/PORTING_HANDOFF.md)
- [games](games/)
  - one folder per supported game
- [games/shogun2](games/shogun2/)
  - source, notes, tools, and releases for Shogun 2

## Current Game

Shogun 2 lives here:

- [games/shogun2](games/shogun2/)

Stable release package:

- [current release](games/shogun2/releases/current/)

Use the files from that folder like this:

1. Copy `dinput8.dll` and `shogun2_mod.ini`
2. Put them into `X:\Program Files (x86)\Steam\steamapps\common\Total War SHOGUN 2`
3. Edit `shogun2_mod.ini` and change `[ui] scale=...` if you want a larger UI

## Build

For Shogun 2:

1. Go to [games/shogun2](games/shogun2/)
2. Build from source using the manual MSVC settings in [games/shogun2/README.md](games/shogun2/README.md)

Build output:

- `games\shogun2\build\bin\dinput8.dll`
- `games\shogun2\build\bin\shogun2_mod.ini`

## Future Work

Future Total War game patches should be added as siblings under `games/`, not mixed into the Shogun 2 tree.
