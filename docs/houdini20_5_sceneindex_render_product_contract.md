# Houdini 20.5 Render Product SceneIndex Contract

This note narrows the RenderSettings/Product/Var issue to the exact Houdini 20.5
USD/Hydra split that affects hdMoonray.

## Contract Split

Houdini 20.5 ships both legacy `UsdImagingDelegate` support and SceneIndex data
source support. Render products and vars live on the boundary between the two.

Local header evidence:

- `usdImaging/renderProductAdapter.h`: Product adapter support is for the Scene
  Index 2.0 API only. In legacy `UsdImagingDelegate`, RenderSettingsAdapter
  handles flattening of products and vars.
- `usdImaging/renderVarAdapter.h`: same contract for RenderVar.
- `usdImaging/renderSettingsFlatteningSceneIndex.h`: creates flattened
  `HdRenderSettingsSchema` data and dependencies from RenderSettings to Product
  and Var targets.
- `hd/renderSettings.h`: `HdRenderSettings` has `RenderProducts`, and each
  product contains render vars, resolution, camera path, pixel aspect, aspect
  policy, aperture, data window, and namespaced settings.

Therefore:

- `UsdRenderSettings` may become an `HdRenderSettings` Bprim and should be
  supported by hdMoonray.
- `UsdRenderProduct` and `UsdRenderVar` should not be implemented as fake
  hdMoonray Bprims.
- Product/Var data should arrive through flattened render settings and/or the
  Hydra render-pass AOV binding state.

## Current hdMoonray Support

`RenderDelegate.cc` advertises and creates a real `renderSettings` Bprim.
`RenderSettingsPrim::_Sync()` forwards `renderingColorSpace` and namespaced
settings to the render delegate. This is the correct place for
`moonray:sceneVariable:*` settings.

`RenderPass.cc` consumes render-pass framing and AOV bindings. This is the
current place where output resolution and RenderVar/AOV selections affect RDL:

- framing or viewport -> RDL `image_width` / `image_height`
- render pass camera -> RDL primary camera
- AOV bindings -> `RenderBuffer::bind()` -> MoonRay beauty/RenderOutput

The current hdMoonray code does not directly inspect
`HdRenderSettings::GetRenderProducts()`. That is acceptable for the proven husk
path as long as Houdini/husk resolves products into render-pass state and AOV
bindings before hdMoonray renders.

## Command Tools

`hd_render` and `hd_usd2rdl` use legacy `UsdImagingDelegate::Populate()` on the
pseudo-root. They also build their own app render task, render buffer, camera,
and AOV binding from command-line options.

That means:

- The command tools do not execute USD RenderProduct output paths.
- The command tools do not use USD Product `productName` as output authority.
- The command tools do not use USD RenderProduct orderedVars for their app AOV
  binding today.
- Raw RenderProduct prims reaching delegate support lookup produce misleading
  warnings in these tools.

The command-tool fix should be legacy traversal hygiene: avoid raw Product/Var
population in the legacy delegate path while leaving RenderSettings available for
flattening. This keeps warnings aligned with the real data path without adding
fake backend support.

## Warning Classification

| Warning | Path | Classification |
|---------|------|----------------|
| unsupported RenderSettings in husk/IPR | Structural bug; hdMoonray supports renderSettings and this should not appear. |
| unsupported RenderProduct in husk with `--settings` | Structural bug if present; product flattening/AOV binding path is leaking. |
| unsupported RenderProduct in `hd_render`/`hd_usd2rdl` legacy populate | Legacy traversal noise if output/RDLA prove the command path is using its own app task and not Product metadata. |
| unsupported RenderVar in any production render path | Structural unless isolated to the same legacy command-tool traversal. |

## Validation Targets

After any command-tool legacy traversal patch:

- husk clean scene remains warning-free and writes a nonconstant EXR.
- `hd_render` clean scene stops logging unsupported RenderProduct/RenderVar and
  still writes a nonconstant EXR.
- `hd_usd2rdl` clean scene stops logging unsupported RenderProduct/RenderVar and
  still writes RDLA.
- Real-scene husk remains free of RenderSettings/Product warnings before render
  prep.
- Real-scene `hd_usd2rdl` no longer shows misleading RenderProduct support
  warnings; unrelated light compatibility warnings remain separately tracked.

## 2026-06-16 Command-Tool Patch Result

The legacy command tools now exclude raw USD `RenderProduct` and `RenderVar`
prims from `UsdImagingDelegate::Populate()` while leaving `RenderSettings`
available to the delegate. This keeps the tools aligned with their actual app
task model: they create their own render task, render buffer, camera/framing, and
AOV binding from command-line options rather than executing USD product output
metadata.

Implementation:

- `cmd/hd_cmd/hd_render/hd_render.cc`
- `cmd/hd_cmd/hd_usd2rdl/hd_usd2rdl.cc`

The exclusion uses authored USD type names (`RenderProduct`, `RenderVar`) instead
of typed `UsdRenderProduct` / `UsdRenderVar` schema queries. That avoids adding a
new `libpxr_usdRender` link dependency to these command tools for a traversal
filter.

Validation:

| Path | Result |
|------|--------|
| clean `hd_usd2rdl` | `RC=0`, RDLA written, no unsupported RenderSettings/Product/Var warning. |
| clean `hd_render` | `RC=0`, 512x512 RGBA EXR written, nonconstant, no unsupported RenderSettings/Product/Var warning. |
| clean husk with `--settings` | `RC=0`, 512x512 RGBA EXR written, nonconstant, no unsupported RenderSettings/Product/Var warning. |
| real-scene `hd_usd2rdl` | `RC=0`, RDLA written, no unsupported RenderSettings/Product/Var warning. |

Remaining warnings seen in older command-tool or scene logs are not Step 3
Product/Var support leaks:

- OCIO scene-linear role / Linear Rec.709 warnings were environment handoff
  problems. `setupHoudini.sh` now mirrors the Houdini package-authored OCIO
  default into direct shells when `OCIO` is unset, and clean `husk`,
  `hd_render`, and `hd_usd2rdl` probes no longer emit those warnings with the
  local PixelManager config.
- Real-scene rectLight prims with MoonRay `SpotLight` class override
  compatibility warnings were not reproduced in the refreshed headless probes.
  A current USD scan showed the affected RectLight prims compose to no active
  SpotLight override, so this remains stale scene-state evidence unless a fresh
  export reproduces it.

The current validated state is:

| Path | Result |
|------|--------|
| clean `hd_usd2rdl -settings /Render/rendersettings` | `RC=0`, RDLA written, no unsupported RenderSettings/Product/Var warning. |
| clean `hd_render -settings /Render/rendersettings` | `RC=0`, 512x512 RGBA EXR written, nonconstant, no unsupported RenderSettings/Product/Var warning. |
| clean husk with `--settings` | `RC=0`, 512x512 RGBA EXR written, nonconstant, no unsupported RenderSettings/Product/Var warning. |

No fake/no-op Product or Var Bprim is present in the final implementation.
