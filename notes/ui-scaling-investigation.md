# UI Scaling Investigation

## Decision

The next issue to solve is UI scaling.

Reason:

- Warhammer 3 already exposes a working `ui_scale` setting.
- UI scaling is the next highest-value user-facing fix after borderless mode.
- Load-time optimization is still worth doing later, but it needs profiling and I/O analysis first.

## Current findings

### Engine-side hidden scaler

Shogun 2 does contain a real engine-side scaling path. The relevant hidden settings are registered in `empire.retail.dll` and backed by config-wrapper objects whose value lives at `+0x5c`:

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

Important mappings confirmed from the registration block and getter helpers:

- `ui_x` -> `0x11c9b240`
- `ui_y` -> `0x11c9c418`
- `ui_width` -> `0x11c9c2e0`
- `ui_height` -> `0x11c9af28`
- `ui_rect_override` -> `0x11c9ae28`
- `enable_ui_scaling` -> `0x11c9f7e8`
- float getter -> `0x100a0b60` reads `float` from `+0x5c`
- bool getter -> `0x100a0b70` reads `bool` from `+0x5c`

The main transformation path appears to be:

- `0x11421ac3` checks `ui_rect_override` and materializes the source UI rect
- `0x113f7b40` materializes the destination UI rect, optionally using `ui_rect_left/top/width/height`
- `0x11411a20` remaps UI coordinates between those rects when `enable_ui_scaling` is enabled
- `0x113e0df0` remaps 2D extents using the same hidden scaling path

Practical conclusion:

- a runtime mod can drive the hidden scaler directly without decoding binary `UIC`
- the clean first prototype is an external `ui_scale` option that computes a virtual UI width/height from the current window size and writes those hidden settings at runtime

### Warhammer 3

From `C:\Users\Rebrond\AppData\Roaming\The Creative Assembly\Warhammer3\scripts\preferences.script.txt`:

- `ui_scale 1.6`
- `ui_radar_scale 1`
- `ui_unit_id_scale 0.5`

This confirms the modern engine supports both:

- a global UI scale
- smaller subsystem-specific scale controls

From `Warhammer3.exe` string and data scans:

- `ui_scale`
- `ui_text_scale`
- `ui_radar_scale`
- `ui_unit_id_scale`
- `ui_scale_warning`

Relevant help text found in the binary:

- `Scale of UI, 1 is default size. Can range between 0.5-2, 0.5 being half size and 2 being double size, but can only scale up if running 1440p or larger res`
- `Scale of text in UI, 1 is default size`
- `Scale of battle radar`
- `Sets scale of unit banners`
- `can only scale up if running 1440p or larger res`

Interpretation:

- Warhammer 3 does not just have one scaler.
- It has a layered scaling model:
  - global UI scale
  - text scale
  - radar scale
  - unit-banner scale
- The binary stores these settings in a contiguous config-schema blob, which is consistent with a mature internal preferences system rather than a one-off hack.

### Shogun 2

From `empire.retail.dll` string scans:

- no `ui_scale` config string
- no `ui_text_scale`
- no `ui_radar_scale`
- no `ui_unit_id_scale`
- does contain:
  - `enable_ui_scaling`
  - `ui_rect_override`
  - `ui_x`
  - `ui_y`
  - `ui_width`
  - `ui_height`

Interpretation:

- Shogun 2 does not appear to have a user-facing global UI scale setting.
- The old engine likely understands layout-level UI scaling metadata already.
- That suggests a data-driven UI-layout solution may be viable without a full engine rewrite.
- Compared to Warhammer 3, Shogun 2 seems to be missing the modern top-level scale controls entirely.

## File-system findings

The live Shogun 2 install contains:

- `data\UI\...`

But that on-disk UI folder is mostly cursors and does not expose the main UI layouts directly.

Interpretation:

- The relevant HUD and menu layout assets are probably still inside the `.pack` archives.
- A complete UI-scaling solution will likely require extracting or overriding packed UI definitions.

After unpacking content from `data.pack` and `local_en.pack` into the workspace:

- `UI\...` contains the actual compiled HUD and menu layout blobs.
- `text\db\ui_component_localisation.loc` contains UI component localisation keys and visible strings.
- `font\...` contains the shipped `.cuf` font assets, including many size variants of:
  - `bardi_*`
  - `bardi_b_*`
  - `editor_*`

This is important because it gives us two independent levers:

1. text and tooltip mapping through `ui_component_localisation.loc`
2. direct font-size overrides through the `.cuf` files

## Pack File Manager source findings

The workspace now also contains an older Pack File Manager source tree at:

- `packfilemanager-code-r1217-trunk`

Useful findings from that source:

- `Common\PackFileCodec.cs` gives us a full reader/writer for `.pack` files.
- `Common\PackFile.cs` and `Common\PackedFile.cs` are enough to build pack files programmatically.
- `PfmCL` and the shipped `pfm.exe` CLI confirm we can already automate `.pack` creation.

Important limitation:

- this source revision does **not** contain a dedicated `twui` or compiled UI-layout codec
- `DecodeTool` is mainly an ad-hoc helper for unknown or unsupported **DB** file layouts
- the Pack File Manager UI opens `DecodeTool` by calling `DBFile.Typename(...)`, which confirms it was designed around database decoding, not Warscape UI layout blobs

Interpretation:

- the Pack File Manager source is useful for packaging and mod output
- it is **not** the missing key for a true global Shogun 2 UI scaler
- we should not spend more time trying to get a `twui` decoder out of this old source tree

Practical impact on the plan:

- use the Pack File Manager code and/or CLI only for pack generation
- continue looking for a root UI scale/rect transform in `empire.retail.dll`
- if runtime scaling is not viable, build our own binary UI-blob transformer with a single external `ui_scale` setting and package its output using the pack tooling

## RPFM source findings

The workspace now also contains a current RPFM source tree at:

- `rpfm-master`

This is a much better reference than the old Pack File Manager source because it has a dedicated `UIC` file type in:

- `rpfm_lib\src\files\uic\mod.rs`

Important findings from that code:

- RPFM explicitly identifies `UIC` files under `ui\...`, including files without extensions.
- the in-memory `UIC` model already includes fields directly relevant to scaling:
  - `font_m_size`
  - `width`
  - `targetmetrics_m_width`
  - `targetmetrics_m_height`
  - layout spacing and column widths

Critical limitation for Shogun 2:

- the module documentation explicitly states:
  - pre-Three Kingdoms `UIC` is a binary format
  - binary format support is **not yet implemented**
- `UIC::decode()` currently only handles XML and falls through to `todo!()` for binary files.
- XML support is currently aimed at later layouts such as v138:
  - `rpfm_lib\src\files\uic\xml\v138.rs`

Interpretation:

- RPFM confirms we are looking in the right place conceptually: `UIC` is the correct UI layout family.
- It also confirms Shogun 2 is on the older binary side of that split.
- So even the best current open-source toolchain does not already provide a drop-in decoder/encoder for the Shogun 2 UI format.

Practical impact on the plan:

- RPFM is useful as:
  - a reference for what a structured `UIC` model should look like
  - a packaging/modding backend
  - a future path if binary `UIC` support is added upstream
- RPFM does **not** remove the core blocker for Shogun 2 today.
- The current best options remain:
  1. find a runtime root UI-scale hook in `empire.retail.dll`
  2. or build our own binary `UIC` scaler for Shogun 2 and package the results with pack tooling

## Layout and font findings

The unpacked compiled UI files are not loose XML, but they do embed readable strings and dimension values.

Examples already confirmed:

- `UI\campaign ui\layout` and `UI\battle ui\layout` reference font faces inline, mostly:
  - `bardi_10`
  - with smaller numbers of `bardi_12`, `bardi_14`, and `bardi_18`
- `UI\campaign ui\campaign_radar` uses a heavier mix of:
  - `bardi_10`
  - `bardi_12`
  - `bardi_14`
  - `bardi_18`
- `UI\battle ui\battleunitcard` stores explicit widget and texture dimensions inline
  - `BattleUnitCard` root block shows `784 x 456`
  - portrait and frame textures show repeated `66 x 88`
- `UI\campaign ui\campaign_radar` stores explicit control sizes inline
  - `button_radar_toggle` is followed by `373 x 172`
  - its button textures use repeated `26 x 26`

Interpretation:

- Font size and widget size are both hard-coded into the shipped UI data.
- A font-only prototype is now practical and low risk.
- It is also unlikely to be sufficient by itself for the smallest widgets, because some controls have tight fixed dimensions that will clip if text grows too much.

## Current best implementation path

Best current path has changed slightly now that the font and localisation data are unpacked:

1. Stage a font-only readability prototype first.
2. Test which panels or widgets clip.
3. Patch only the worst fixed-size controls in the compiled UI blobs:
   - campaign radar
   - battle unit cards
   - campaign HUD buttons and labels
4. Keep runtime hooks as a later option if the data-driven route stalls.

This is a better first implementation than going straight to engine hooks because:

- it is easier to iterate
- it is reversible
- it matches how Warhammer appears to separate global text/UI scaling from specific subsystem sizing
- it lets us identify exactly which Shogun 2 panels need hard dimension edits

## Likely implementation path

Best current hypothesis:

1. Extract or override the packed Shogun 2 UI layout data.
2. Identify where layout nodes use `ui_scaling`, `ui_rect_override`, and related rect fields.
3. Build a first-pass UI mod that enlarges the most critical unreadable elements:
   - campaign HUD
   - battle HUD
   - unit cards
   - unit banners
   - core text-heavy panels
4. If layout-only overrides are not enough, add a runtime hook for a root UI scale factor.

Current recommendation after the deeper Warhammer pass:

- Do not try to port Warhammer 3's exact config system into Shogun 2 first.
- Instead, use Warhammer 3 as the design reference:
  - one global scale concept
  - optional subsystem-specific scale controls later
- Implement Shogun 2 in phases:
  1. data-driven layout enlargement
  2. optional runtime root multiplier
  3. optional per-subsystem controls later if needed

## Why this path is better than starting with load times

- It has a direct lead from Warhammer 3.
- It likely produces visible progress faster.
- It can potentially be solved with data overrides plus a smaller runtime patch.

## Immediate next step

The workspace now contains a generated conservative font-override prototype at:

- `mod_staging\font_overrides`
- `mod_staging\pack_root`
- `mod_staging\shogun2_ui_font_conservative.pack`

Next, validate which fixed-size widgets still need layout patches.

The first likely patch candidates are:

- `UI\campaign ui\campaign_radar`
- `UI\battle ui\battleunitcard`
- `UI\campaign ui\layout`
- `UI\battle ui\layout`
