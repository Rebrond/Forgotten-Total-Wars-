# Shogun 2

This folder contains the Shogun 2 mod project from the repository.

Current stable features:

- true borderless window mode through the game's `windowed` path
- launch-time UI scaling from `shogun2_mod.ini`

Still experimental:

- loading-time optimization
- live in-game UI scale changes

## Build

Build prerequisites:

- Visual Studio with MSVC and Windows SDK
- build for `x86`
- run the commands from this folder

Recommended environment setup:

```bat
call "D:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat" x86
```

Create output folders:

```bat
if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\bin mkdir build\bin
```

Compile:

```bat
cl /nologo /std:c++20 /O2 /MT /EHsc /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /D_CRT_SECURE_NO_WARNINGS /I src /c src\common.cpp src\borderless_hooks.cpp src\dinput8_proxy.cpp /Fobuild\obj\
```

Link:

```bat
link /nologo /DLL /MACHINE:X86 /OUT:build\bin\dinput8.dll /DEF:dinput8_proxy.def build\obj\common.obj build\obj\borderless_hooks.obj build\obj\dinput8_proxy.obj user32.lib gdi32.lib kernel32.lib ole32.lib
```

Copy the default config:

```bat
copy /Y shogun2_mod.ini build\bin\shogun2_mod.ini
```

Expected output:

- `build\bin\dinput8.dll`
- `build\bin\shogun2_mod.ini`

## Install

Use the release files from [releases/current](releases/current/).

Copy these files into the Shogun 2 install folder:

`D:\Program Files (x86)\Steam\steamapps\common\Total War SHOGUN 2`

- `dinput8.dll`
- `shogun2_mod.ini`

Those files are the tracked release copies here:

- [dinput8.dll](releases/current/dinput8.dll)
- [shogun2_mod.ini](releases/current/shogun2_mod.ini)

## Usage

For borderless:

- set Shogun 2 to `windowed`
- use your desktop resolution
- launch the game

For UI scaling:

- open `shogun2_mod.ini` in a text editor
- find:

```ini
[ui]
scale=1.00
```

- change `scale` and save the file
- launch the game again

Examples:

- `1.00` = disabled
- `1.10` = slightly larger
- `1.25` = good starting point for high resolutions
- `1.40` = aggressive scaling

UI scaling is applied at launch, not live during gameplay.

## Layout

- [src](src/)
- [notes](notes/)
- [tools](tools/)

## Release

Tracked current release:

- [dinput8.dll](releases/current/dinput8.dll)
- [shogun2_mod.ini](releases/current/shogun2_mod.ini)

Generated zip packages and checksums are intentionally not tracked in git.

## Notes

Shared cross-game porting guidance is in:

- [docs/PORTING_HANDOFF.md](../../docs/PORTING_HANDOFF.md)
