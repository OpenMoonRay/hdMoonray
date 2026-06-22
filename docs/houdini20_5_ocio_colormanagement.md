# Houdini 20.5 Universal OCIO Color-Management Notes

This note records the Houdini 20.5 OCIO dependency and universal,
config-driven texture/render color policy implemented for hdMoonray and
MoonRay native texture maps. ARRI configs were used as validation inputs, not
as hidden renderer policy. This pass does not change display/view looks, scene
scale, light radius mapping, displacement stability, AOV architecture, or
RenderProduct/RenderVar handling.

Primary references used for the contract:

- MoonRay `ImageMap` and `UsdUVTexture` user docs: texture maps support EXR/TX
  files and existing gamma/source controls.
- OpenUSD color guidance and UsdPreviewSurface `sourceColorSpace`: texture
  source color is authored separately from render/output display transforms.
- Houdini Solaris OCIO docs: Houdini uses the active OCIO config for color
  management, with renderer processes inheriting environment/config state.

## Local Houdini Inventory

| component | Houdini docs target | local observed version | local path/library | notes |
| --- | --- | --- | --- | --- |
| Houdini | 20.5 | 20.5.584 | `/Applications/Houdini/Houdini20.5.584` | Current integration target. |
| Python | 3.11.7 | 3.11.7 | Houdini `hython` | Matches H20.5 platform notes. |
| USD | 24.03 | `pxr.Usd.GetVersion() == (0, 24, 3)` | Houdini toolkit / framework | Hydra ABI target. |
| OpenColorIO | 2.3.0 | 2.3.0 | `libOpenColorIO_sidefx.2.3.0.dylib` | Symbols are in namespace `sidefx`. |
| MaterialX | 1.38.10 | 1.38.10 | Houdini Python module | Observed through `hython`. |
| OpenImageIO | 2.3.x | 2.3.14 Python module | Houdini Python module | MoonRay bundled OIIO remains separate. |

Houdini OCIO library:

```text
/Applications/Houdini/Houdini20.5.584/Frameworks/Houdini.framework/Versions/20.5/Libraries/libOpenColorIO_sidefx.2.3.0.dylib
install name: @rpath/libOpenColorIO_sidefx.2.3.0.dylib
```

Houdini OCIO headers:

```text
/Applications/Houdini/Houdini20.5.584/Frameworks/Houdini.framework/Versions/Current/Resources/toolkit/include/OpenColorIO
```

Those headers define `OCIO_NAMESPACE sidefx`. A tiny compile/link probe using
the Houdini headers and `-lOpenColorIO_sidefx` reports OCIO `2.3.0`. A mixed
probe using MoonRay's older standard OCIO headers with the SideFX OCIO dylib
fails to link, so header/library pairing must stay explicit.

## Strategy Decision

| strategy | linked OCIO library | headers used | husk | viewport/IPR | hd_usd2rdl | hd_render | standalone MoonRay impact | ABI/rpath risk | upstreamability | verdict |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| A | Houdini `libOpenColorIO_sidefx.2.3.0.dylib` | Houdini HDK OCIO headers with `OCIO_NAMESPACE=sidefx` | Tiny render validated | Not GUI-tested in this pass | Validated | Validated with `-translate-only` | None for standalone MoonRay core | Medium unless include order is guarded | Houdini-specific | Chosen for Houdini-facing hdMoonray. |
| B | MoonRay bundled standard `libOpenColorIO.2.3.dylib` | Standard OCIO 2.3.0 headers | Valid through native map runtime | Not GUI-tested in this pass | Used by loaded native texture/map DSOs | Used by loaded native texture/map DSOs | Updates standalone/default dependency to OCIO 2.3.0 | Safe because install names and namespaces differ from SideFX OCIO | Better upstream/general default | Chosen for MoonRay core/native texture runtime. |

The implementation intentionally uses both strategies at a component boundary:

- `libhydramoonray`, `hd_moonray`, and `hd_moonray_debug` compile against and
  link Houdini's SideFX OCIO 2.3.0 for render-setting and typed-color
  translation inside Houdini/husk.
- MoonRay core texture code and native map DSOs compile against and link the
  bundled standard OCIO 2.3.0 for `ImageMap`, `UsdUVTexture`, and shared texture
  helpers.

This is not a header/library mismatch. It is a deliberate split between
Houdini-facing translation code and MoonRay core map runtime. `otool -L` proves
the install names are distinct:

```text
libhydramoonray.dylib -> @rpath/libOpenColorIO_sidefx.2.3.0.dylib
hd_moonray.dylib -> @rpath/libOpenColorIO_sidefx.2.3.0.dylib
librendering_shading.dylib -> @rpath/libOpenColorIO.2.3.dylib
ImageMap.so -> @rpath/libOpenColorIO.2.3.dylib
UsdUVTexture.so -> @rpath/libOpenColorIO.2.3.dylib
```

`hd_moonray_debug.dylib` can show both direct dependencies because the debug
plugin links additional MoonRay runtime libraries. That is expected for the
debug plugin as long as SideFX headers are not compiled against the standard
OCIO dylib or vice versa.

## Config-Driven Policy

Renderer code must not silently invent ARRI, ACES, Rec.709, or Rec.2020 color
spaces. Known names are allowed in docs, tests, fixtures, and UI examples only
when they come from the active config or are validated against it.

Render/working space resolution order:

1. explicit authored color-space token, if the active OCIO config resolves it
   as an exact name, role, or alias;
2. OCIO role `rendering`;
3. OCIO role `scene_linear`;
4. OCIO role `default_float`;
5. OCIO role `reference`, only when it resolves to a concrete color space;
6. OCIO role `default`, as a final fallback with a warning.

Missing `rendering` is not an error when `scene_linear` exists. OCIO file rules
are used only for texture `auto`; they are never used to select render/working
space.

Texture source resolution:

- `auto`: use the active OCIO config file rules for the texture filename;
- `raw`, `data`, or `none`: bypass OCIO source conversion;
- any other string: resolve through the active config as exact name, role, or
  alias;
- unresolved explicit source spaces warn clearly and disable that conversion
  instead of guessing a replacement.

## Config Validation

Houdini 20.5.584 uses PyOpenColorIO/OpenColorIO `2.3.0`. Local API probing
confirmed the menu implementation can use `getColorSpaceNames`,
`getRoleNames`, `getRoleColorSpace`, `getColorSpace`, `getFileRules`,
`getColorSpaceFromFilepath`, `ColorSpace.getFamily`,
`ColorSpace.getCategories`, and `ColorSpace.getAliases`.

Validation configs:

| config | load result | profile | final render/working space | notes |
| --- | --- | --- | --- | --- |
| `/Users/j7s/Downloads/ARRI-Houdini-main/arri-CG.ocio` | OK | 2.3 | `Linear ARRI Wide Gamut 4` via role `rendering` | File rules map PNG/JPG to the config's texture space, `.tx` to `Raw`, EXR to AWG4. |
| `/Users/j7s/Downloads/ARRI-Houdini-main/config-2020.ocio` | OK | 2.0 | `Linear Rec.2020` via role `rendering` | Proves non-ARRI role resolution. |
| `/Users/j7s/Downloads/ARRI-Houdini-main/ACEScg-personal-v2.0_ocio-v2.4-.ocio` | FAIL | 2.4 | n/a | Expected incompatibility: OCIO 2.3 cannot load config minor version 2.4. |
| `/Users/j7s/Downloads/ARRI-Houdini-main/arri-original-nonCG-dontuse.ocio` | OK | 2.1 | `Linear ARRI Wide Gamut 4` via role `scene_linear` | Missing `rendering` is OK. Role `default = ARRI LogC4` and file-rule default `ACES2065-1` do not become render/working space. |

Representative role resolution for the ARRI CG validation config:

| role/name | resolved color space |
| --- | --- |
| `scene_linear` | `Linear ARRI Wide Gamut 4` |
| `rendering` | `Linear ARRI Wide Gamut 4` |
| `default` | `Linear ARRI Wide Gamut 4` |
| `reference` | `Linear ARRI Wide Gamut 4` |
| `default_float` | `Linear ARRI Wide Gamut 4` |
| `texture_paint` | `Gamma 2.4 Rec.709 - Texture` |
| `color_picking` | `Gamma 2.4 Rec.709 - Texture` |
| `data` | `Raw` |
The listed names above are validation results from that config, not fallback
names embedded in renderer-core logic.

## Runtime Diagnostics

`ColorManagement` reports:

- OCIO library version;
- process `$OCIO`;
- whether the `$OCIO` file exists;
- requested `renderingColorSpace`;
- resolved scene-linear color space;
- resolved rendering color space;
- resolution method;
- default-role fallback warning state;
- whether conversion is enabled;
- disabled reason.

`RenderDelegate` logs the diagnostic at delegate construction, when render
settings are applied, and when `renderingColorSpace` changes. If `$OCIO` is
unset, the warning is explicit:

```text
hdMoonray: OCIO is unset in this render process; color conversion is disabled.
Launch through the Houdini package environment or source setupHoudini.sh before
standalone husk/hd_usd2rdl.
```

Validated runtime examples:

| case | process OCIO | config exists | OCIO version | requested space | resolved space | result |
| --- | --- | --- | --- | --- | --- | --- |
| explicit config | `/Users/j7s/Downloads/ARRI-Houdini-main/arri-CG.ocio` | yes | 2.3.0 | `<default>` | `Linear ARRI Wide Gamut 4` via `role:rendering` | `hd_usd2rdl`, `hd_render -translate-only`, and tiny `husk` render validated. |
| setup fallback | Houdini package value when `$OCIO` is unset before sourcing setup | yes | 2.3.0 | `<default>` | active-config roles | setup-time behavior only; explicit `$OCIO` is not overwritten. |
| bad path | `/tmp/does_not_exist.ocio` | no | 2.3.0 | `<default>` | raw/default diagnostic | Logs missing path and disables conversion. |
| unset after setup | package fallback path | yes | 2.3.0 | `<default>` | active-config roles | Logs the package-resolved config path. |

## Native Texture Source Policy

Native MoonRay texture-source behavior is now OCIO-aware.

`ImageMap` adds:

```text
source_color_space = "auto"
```

Policy:

- `auto`: use the active OCIO config file rules for the texture filename;
- `raw`, `data`, or `none`: bypass OCIO source conversion;
- any other string: treat as an explicit OCIO color-space name, role, or alias;
- when OCIO source conversion is active, legacy `gamma` is bypassed;
- if OCIO is unavailable, missing, or the source cannot resolve, legacy gamma
  behavior remains the fallback.

`UsdUVTexture` keeps the USD-compatible enum:

```text
sourceColorSpace = raw | sRGB | auto
```

and adds an optional override:

```text
source_color_space = ""
```

Policy:

- empty override preserves the USD enum;
- `sourceColorSpace = raw`: no conversion;
- `sourceColorSpace = sRGB`: resolves through the active config as an exact
  name, role, or alias; if unsupported by the config, conversion is disabled
  with a warning rather than replaced with a guessed space;
- `sourceColorSpace = auto`: use OCIO file rules;
- non-empty override follows the same `auto` / `raw` / explicit-name policy as
  `ImageMap`.

Utility and normal-map paths that use the shared texture helper pass `raw`:
`ImageNormalMap`, `ProjectCameraNormalMap`, `ProjectPlanarNormalMap`, and
glitter flake pattern textures. Color projection/triplanar texture helpers use
`auto`.

## Houdini UI Exposure

The MoonRay Render Settings LOP exposes a Color Management folder with:

- `renderingColorSpace`;
- notes for active OCIO policy;
- notes for texture-source policy;
- notes that no display/view transform is baked into EXR output.

Blank or `auto` clears `UsdRender.Settings.renderingColorSpace`; explicit values
author the USD RenderSettings attribute. Blank/default is resolved at render
time from the active config roles in the order listed above.

The Houdini `ImageMap` DS and `moonray_nodes.json` expose
`source_color_space`. The live HDA also exposes a visible
`source_color_space` parameter with a Python item generator that builds entries
from the active OCIO config. The base static metadata contains only policy
tokens:

- `auto`;
- `raw`;
- `data`;

Config color spaces such as AWG4, LogC4, ACEScg, Rec.709, Rec.2020, or texture
spaces appear in the menu only when the active config contains or aliases them.

There is no separate source `moonray_UsdUVTexture.ds` in this tree; the native
`UsdUVTexture` coredata and shader JSON expose the override for translated USD
and direct RDL use.

## Output Policy

MoonRay output remains scene-linear/render-space data. This pass does not bake a
display/view transform into EXR output and does not alter AgX/OpenDRT/ARRI view
looks. Display transforms remain a Houdini/Nuke/viewer responsibility unless a
future explicit feature adds display-referred output.

## Validation Artifacts

Temporary validation logs were written under:

```text
/tmp/moonray_ocio_runtime_logs
```

Key checks:

- live Houdini loads installed Render Settings and ImageMap HDAs from
  `/Applications/MoonRay/installs/openmoonray/plugin/houdini/otls`.
- Render Settings `renderingColorSpace` is visible and uses
  `ocio_render_space_menu()`.
- ImageMap `source_color_space` is visible and uses
  `ocio_texture_source_menu()`.
- ARRI CG, ARRI original/non-CG, and config-2020 load and resolve through roles;
  the OCIO 2.4 ACES config fails honestly under OCIO 2.3.
- `hd_usd2rdl`, `hd_render -translate-only`, and tiny `husk` render log OCIO
  2.3.0 and resolve the working space from active config roles.
- bad `$OCIO` logs the missing path and disables conversion instead of guessing.
- installed binary scan found no stale references to the old
  `BasicTexture::update` signature after rebuilding dependent map/material
  libraries.

## Remaining Limits

- Houdini viewport/IPR was not manually GUI-tested in this pass.
- MaterialX image networks were not exhaustively tested.
- This pass does not change texture conversion scripts, maketx rules, display
  transforms, EXR metadata policy, AOV color behavior, or artistic looks.
