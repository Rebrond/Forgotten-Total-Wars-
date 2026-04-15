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

## Release

Tracked current release:

- [dinput8.dll](/D:/TW_uiscaler/games/shogun2/releases/current/dinput8.dll)
- [shogun2_mod.ini](/D:/TW_uiscaler/games/shogun2/releases/current/shogun2_mod.ini)

Generated zip packages and checksums are intentionally not tracked in git.

## Notes

Shared cross-game porting guidance is in:

- [docs/PORTING_HANDOFF.md](/D:/TW_uiscaler/docs/PORTING_HANDOFF.md)
