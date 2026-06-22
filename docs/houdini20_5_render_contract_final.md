# Houdini 20.5 Final Render Contract

This document defines the target USD/Hydra/Houdini/hdMoonray/MoonRay contract
for the final Step 3 repair pass. It is based on the local Houdini 20.5.584
headers and current hdMoonray source, not on UI intuition.

## Local Evidence

Primary headers:

- `pxr/usd/usdRender/settings.h`
- `pxr/usd/usdRender/settingsBase.h`
- `pxr/usd/usdRender/product.h`
- `pxr/usd/usdRender/var.h`
- `pxr/usdImaging/usdImaging/delegate.h`
- `pxr/usdImaging/usdImaging/renderSettingsAdapter.h`
- `pxr/usdImaging/usdImaging/renderProductAdapter.h`
- `pxr/usdImaging/usdImaging/renderVarAdapter.h`
- `pxr/usdImaging/usdImaging/renderSettingsFlatteningSceneIndex.h`
- `pxr/imaging/hd/renderSettings.h`
- `pxr/imaging/hd/renderSettingsSchema.h`
- `pxr/imaging/hd/renderDelegate.h`
- `pxr/imaging/hd/renderPassState.h`
- `pxr/imaging/cameraUtil/framing.h`

Primary hdMoonray files:

- `lib/hydramoonray/RenderDelegate.cc`
- `lib/hydramoonray/RenderPass.cc`
- `lib/hydramoonray/RenderBuffer.cc`
- `lib/hydramoonray/Camera.cc`

## Contract by Layer

### DCC

The MoonRay Render Settings LOP authors coherent USD intent only:

- one `UsdRenderSettings`
- one default `UsdRenderProduct`
- one or more `UsdRenderVar` prims
- MoonRay scene-variable attrs on RenderSettings

The DCC layer must not compensate for backend camera/framing bugs by hardcoding
camera aperture, product camera, data window, or project-specific aspect ratios.

### USD RenderSettings

RenderSettings owns:

- `camera`
- `products`
- default `resolution`
- default `pixelAspectRatio`
- renderer settings / namespaced renderer settings
- MoonRay `moonray:sceneVariable:*` attrs

`dataWindowNDC` and `aspectRatioConformPolicy` should be authored only if a
Houdini/OpenUSD contract or runtime diagnostic proves they are required. Current
marker probes did not show a MoonRay result difference when explicit full-frame
data window or aperture policy was authored.

### USD RenderProduct

RenderProduct owns:

- `productName`
- `productType`
- `orderedVars`
- product resolution and pixel aspect ratio matching the default RenderSettings

Product values that are true overrides:

- `camera`
- product-specific resolution
- product-specific pixel aspect
- product-specific data window / aspect policy

The default MoonRay beauty product must not author Product.camera. The local
tests proved husk falls back to `RenderSettings.camera` when Product.camera is
absent, and that authored Product.camera is a real product-specific override.

### USD RenderVar

RenderVar owns:

- `sourceName`
- `sourceType`
- `dataType`
- AOV/driver metadata
- optional MoonRay RenderOutput metadata

RenderVar must not be represented as a fake hdMoonray Bprim unless the Houdini
20.5 headers prove that is expected. They currently do not.

### Houdini USD Render ROP

The ROP owns execution:

- selected LOP branch
- selected RenderSettings path
- renderer
- frame/range execution
- `outputimage` override

`RenderProduct.productName` remains valid USD product intent and fallback.
ROP `outputimage` is the Houdini execution override path.

### Houdini/Solaris Viewport/IPR

Solaris/IPR owns live stage composition, render task state, viewport/framing
state, camera selection, and live Hydra invalidation. The final user-visible
unsupported-prim warning is in this path, so command-tool success is not enough.

### Husk

husk owns batch execution and should consume RenderSettings/Product/Var through
Houdini/OpenUSD render settings flattening or equivalent render-task state.

### Hydra / SceneIndex

Hydra owns:

- `HdRenderSettings` Bprim or `HdRenderSettingsSchema`
- flattened product/var metadata
- render pass framing
- camera sprims
- AOV bindings
- render buffers

Local header evidence:

- `UsdImagingRenderProductAdapter` and `UsdImagingRenderVarAdapter` are
  SceneIndex-only adapters.
- In legacy `UsdImagingDelegate`, `UsdImagingRenderSettingsAdapter` handles
  flattening products and vars.
- `UsdImagingRenderSettingsFlatteningSceneIndex` adds flattened
  `HdRenderSettingsSchema` and dependencies from settings to targeted products
  and vars.
- `HdRenderSettings` is a Bprim containing flattened `RenderProducts`.

### hdMoonray

hdMoonray must consume the Hydra state, not raw USD product prims:

- RenderSettings Bprim / namespaced settings -> MoonRay render settings and RDL
  SceneVariables.
- render pass framing -> RDL `image_width` / `image_height`.
- render pass camera -> RDL primary camera.
- render aspect -> camera conform path.
- AOV bindings -> `RenderBuffer::bind()` -> MoonRay beauty / RenderOutput.

hdMoonray should support `renderSettings` as a Bprim. It should not support
Product/Var as fake Bprims.

### MoonRay RDL

MoonRay RDL receives:

- `SceneVariables`
- `image_width`
- `image_height`
- primary camera
- RenderOutput/beauty binding
- layer, materials, geometry, lights

## Answers Required by This Pass

| Question | Contract Answer |
|----------|-----------------|
| What should RenderSettings author? | Camera, products, default resolution, default pixel aspect, namespaced renderer settings, MoonRay scene variables. |
| What should RenderProduct author? | Product name/type, orderedVars, matching product resolution and pixel aspect for the default product. |
| What should RenderVar author? | Source name/type/data type and AOV/driver/MoonRay output metadata. |
| Which values inherit from Settings to Product? | Camera falls back from Settings when Product.camera is absent. Product resolution/pixel aspect are authored to match for default product to avoid stale product opinions. |
| Which Product values are overrides? | Camera, resolution, pixel aspect, data window, aspect policy when explicitly authored. |
| Should Product.camera be authored by default? | No. It is a product-specific override. |
| Should Product.resolution/pixelAspectRatio be authored by default? | Yes, matching RenderSettings for the default product. |
| Should dataWindowNDC be authored by default? | No, not until proven necessary. |
| Should aspectRatioConformPolicy be authored by default? | No, not until proven necessary. |
| How does husk flatten settings/products/vars? | Through Houdini/OpenUSD render settings flattening / render-task state; clean husk test did not leak raw Product warnings. |
| How does Solaris/IPR flatten or traverse them? | Still under diagnostic. User sees raw unsupported `RenderSettings`/`RenderProduct` warning in fresh IPR. |
| How do command tools differ? | They use legacy `UsdImagingDelegate` and app-owned render tasks; they are comparison tools, not proof for IPR. |
| Should hdMoonray support RenderSettings as Bprim? | Yes: `HdRenderSettings` is a Bprim. |
| Should hdMoonray support Product/Var as Bprims? | No, not with current H20.5 header evidence. |
| How do RenderVars become AOV bindings? | Through product/var flattening into Hydra render pass AOV bindings. |
| How do AOV bindings become RenderOutputs? | `RenderPass.cc` iterates AOV bindings and calls `RenderBuffer::bind()`. |
| How do resolutions become framing? | Client render task supplies render pass framing/viewport; hdMoonray writes RDL image dimensions from that. |
| How does framing become image dimensions? | `RenderPass.cc` uses `GetFraming().dataWindow` or viewport dimensions. |
| How does aspect become camera window? | `RenderPass.cc` passes `w/h` to `Camera::setAsPrimaryCamera`; `Camera.cc` applies `CameraUtilConformedWindow`. |
| Division of responsibility? | DCC authors USD intent; Houdini/Solaris/husk flatten/execute; Hydra provides state; hdMoonray translates; RDL renders. |

## Historical GUI Diagnostic Gap

The command-tool Product/Var warning path was cleaned, and the current headless
`husk`, `hd_render`, and `hd_usd2rdl` validations are clean. Earlier Houdini
Solaris/IPR sessions showed:

- `Selected hydra renderer doesn't support prim type 'RenderSettings'`
- `Selected hydra renderer doesn't support prim type 'RenderProduct'`

Fresh graphical Houdini/Solaris IPR confirmation is still required before
claiming that the GUI path is also clean. If the warning returns in a refreshed
scene, use `HDMR_PRIMTYPE_DIAG=1` to prove whether the raw type reaches
hdMoonray or is emitted by an upstream Houdini/Solaris bridge.

## 2026-06-20 Shipping Pass Update

This section records the final local implementation state after the Step 3
shipping pass. It supersedes the older command-tool diagnostic-gap notes above
for `husk`, `hd_render`, and `hd_usd2rdl`. Fresh graphical Houdini/Solaris IPR
visual confirmation is still a manual validation item.

### Implemented Changes

#### hdMoonray Runtime

- `RenderDelegate` has real `HdRenderSettings` Bprim support and keeps
  RenderProduct/RenderVar out of fake no-op Bprim support.
- Render stats/progress are exposed through Houdini-facing flat keys:
  `percentDone`, `totalClockTime`, and `renderProgressAnnotation`.
- Terminal renderer errors are no longer reported as 100% complete. When the
  Arras/MCRT session stops unexpectedly, `percentDone` remains at the current
  renderer progress and `renderProgressAnnotation` carries a `Render Error:`
  message.
- `RenderPass` writes RDL `image_width` and `image_height` from
  `HdRenderPassState::GetFraming().dataWindow` when available, or from the
  viewport fallback.
- `RenderPass` passes render aspect to `Camera::setAsPrimaryCamera`, and
  `Camera` uses the Hydra/camera-util window-policy conform path for the RDL
  camera.
- `RenderBuffer` / AOV binding behavior remains the Beauty/AOV transport path;
  Beauty remains protected as RGBA/color4f at the DCC RenderVar layer.
- `ArrasRenderer` now shuts sessions down explicitly, waits for disconnect,
  falls back to disconnect, and suppresses only expected shutdown socket
  exceptions while the renderer is intentionally tearing down.
- `ArrasRenderer` no longer treats initial status-only MCRT messages as a
  finished image frame, and it refuses to resolve buffers before any image data
  has arrived.

#### Command Tools

- `hd_render` and `hd_usd2rdl` now accept `-settings`.
- The command tools resolve the camera in this order:
  1. explicit `-camera`
  2. first RenderProduct camera
  3. RenderSettings camera
  4. legacy free/framing camera fallback
- The command tools resolve resolution in this order:
  1. explicit `-size`
  2. first RenderProduct resolution
  3. RenderSettings resolution
  4. legacy default
- Raw `RenderProduct` and `RenderVar` prims are excluded from the legacy
  `UsdImagingDelegate::Populate` root list, so the tools no longer emit
  misleading unsupported RenderProduct/RenderVar traversal warnings.
- The command tools use raw USD prim type and relationship inspection rather
  than adding a hard dependency on `UsdRender`.
- `FreeCamera` can be constructed from `UsdGeomCamera`.
- `hd_render` and `hd_usd2rdl` install with the Houdini Python framework rpath
  needed by H20.5.584 on macOS.

#### Houdini Plugin Metadata

- `aovsupport` remains `true`.
- The failed `husk.stats_metadata`, `husk.metadata`, `peakMemory`, and nested
  `system_memory` / `system_time` workaround was removed.
- `statsdatapaths` maps only the proven useful flat keys:
  `percentDone`, `totalClockTime`, and `renderProgressAnnotation`.

#### SDR Parser Metadata

- `moonrayShaderParser/parserPlugin.cpp` now emits dynamic vector defaults and
  metadata correctly, including `iridescence_colors` as a dynamic `float3[]`
  / `Vt.Vec3fArray` value. This fixes the prior `DwaBaseMaterial`
  default-value type mismatch warning.

#### DCC Render Settings LOP

- RenderSettings and RenderProduct resolution are authored from the same
  MoonRay Render Settings LOP tuple.
- RenderSettings and RenderProduct pixel aspect ratio are authored
  consistently.
- Product.camera remains absent by default because it is a product-specific
  override; the default product uses RenderSettings.camera.
- `dataWindowNDC` and `aspectRatioConformPolicy` remain schema fallback until a
  future UI/policy explicitly exposes them.
- The owned USD Render ROP continues to use Houdini execution authority:
  selected owner branch, owner RenderSettings path, renderer, frame/range, and
  `outputimage`.

### Installed Runtime Provenance

The final Release build was installed and ad-hoc signed into
`/Applications/MoonRay/installs/openmoonray`.

| Artifact | Installed UUID |
|----------|----------------|
| `lib/libhydramoonray.dylib` | `01BE0818-E34E-35C1-8AFD-817E7935B6E1` |
| `plugin/hd_moonray.dylib` | `6BF06096-50B2-311A-A7F5-2CD7ED2FDA01` |
| `plugin/hd_moonray_debug.dylib` | `C7BCE629-C68F-372A-9FF7-A2A7569D0232` |
| `bin/hd_render` | `51BB2162-6A4F-398A-837F-A8D64BD07A53` |
| `bin/hd_usd2rdl` | `212E2045-2471-34B5-AB40-50B6A8F022EB` |

`codesign --verify --verbose=2` passed for the installed runtime artifacts.
`husk --list-renderers` listed both `HdMoonrayRendererPlugin` and
`HdMoonrayRendererDebugPlugin`.

### DCC Source/Install Provenance

| File | SHA-256 |
|------|---------|
| `moonray_render_settings.py` source/install | `79e6564036fe759adcf34e830570d66aa6120834b2c1c25c3f79fa7d1b154a92` |
| `Lop::DW_MOONRAY::moonrayrendersettings::1.hda` source/install | `42e5403d53040afdafe8dd2af5fcd0c35f55e4d667c51caa778cbbaa5180311b` |
| `UsdRenderers.json` source/install | `fce8438e8c006cf3e80ca655e8ea299dc634651b1c37629c7eb4cf8aaa06e312` |

Installed H20.5 DCC validation passed:

```text
SUMMARY PASS=121 FAIL=0 SKIP=6
```

### Final Runtime Validation

Clean generated USD probes:

| Probe | Result |
|-------|--------|
| base | `96 x 72` RGBA float, nonconstant EXR, no unsupported RenderSettings/Product/Var warnings, no shutdown socket noise |
| square | `96 x 96` RGBA float, nonconstant EXR, no unsupported RenderSettings/Product/Var warnings, no shutdown socket noise |
| wide | `160 x 90` RGBA float, nonconstant EXR, no unsupported RenderSettings/Product/Var warnings, no shutdown socket noise |

Command-tool probes:

| Tool | Result |
|------|--------|
| `hd_usd2rdl -settings /Render/rendersettings` | RC 0; RDLA contains `SceneVariables`, `image_width = 96`, `image_height = 72`, and `PerspectiveCamera("primaryCamera")`; no unsupported prim, socket, import, or iridescence warnings |
| `hd_render -settings /Render/rendersettings` | RC 0; `96 x 72` RGBA float nonconstant EXR; no unsupported prim, socket, import, or iridescence warnings |

Lifecycle probes:

| Probe | Result |
|-------|--------|
| Refreshed real scene, low-res, displacement disabled | `RC=0`, EXR written at `96 x 70`, stage shading completed, no stale `husk`/`execComp`/`mcrt`/Arras processes remained. |
| Refreshed real scene, low-res, displacement enabled | MCRT still exits during render prep with signal 11, but husk no longer hangs. The output metadata reports `percentDone = 0` and `renderProgressAnnotation = "Render Error: compExited: mcrt exited due to signal 11"`. |

The second probe is a remaining displacement/backend scene-data crash, not a
finite-render lifecycle hang and not a RenderSettings/Product/Var architecture
failure.

Build/check commands completed successfully:

- `cmake --build /Applications/MoonRay/build --config Release -j 8`
- `git diff --check` in parent, hdMoonray, DCC, and SDR
- `husk --list-renderers`

### Remaining Boundaries

- Fresh graphical Houdini/Solaris IPR visual confirmation is still required for
  viewport-final framing and color appearance. Headless `husk`, `hd_render`,
  and `hd_usd2rdl` now prove the render contract and command-tool path.
- H20.5 husk still writes suspicious `renderMemory` / `renderMemory_s` metadata
  such as approximately `417 GB` even when MoonRay delegate metadata is limited.
  The local MoonRay memory workaround was removed because it did not control the
  tag; experiments show husk writes this metadata outside the renderer metadata
  map. Treat this as a SideFX/H20.5 husk writer issue unless a later local hook
  is found.
- GUI-only GLD texture and Qt `QNSWindow` warnings were not reproduced in
  headless `husk`/command-tool paths. They remain external/GUI warnings until a
  fresh Houdini GUI session proves a MoonRay-specific trigger.
- The earlier `SpotLight may not be compatible with USD light type 'rectLight'`
  log was not reproduced in current headless tests. The refreshed export shows
  the relevant RectLight prims with authored `moonray:class` opinions that
  compose to no active SpotLight override, so the warning is tracked as stale
  authored scene state unless a current export reproduces it.
