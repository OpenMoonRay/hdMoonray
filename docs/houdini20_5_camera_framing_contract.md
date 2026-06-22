# Houdini 20.5 Camera / Framing Contract

The camera/framing issue is part of the final render contract. It must not be
solved by speculative DCC authoring, Product.camera, hardcoded aspect ratios, or
camera aperture hacks.

## Local Header Evidence

`pxr/imaging/cameraUtil/framing.h` defines `CameraUtilFraming` as the state that
maps camera filmback to pixels:

- `displayWindow`
- `dataWindow`
- `pixelAspectRatio`

The header explicitly says render-buffer size can differ from framing. It also
says the rasterizer must use the framed projection matrix and set the viewport to
the data window.

`pxr/imaging/hd/renderPassState.h` exposes:

- `GetFraming()`
- `GetViewport()`
- `GetCamera()`
- `GetAovBindings()`

## Current hdMoonray Path

`RenderPass.cc`:

- reads `renderPassState->GetFraming()`.
- if framing is valid, uses `framing.dataWindow` width/height.
- otherwise falls back to viewport width/height.
- writes RDL `image_width` and `image_height`.
- passes `double(width) / height` to `Camera::setAsPrimaryCamera()`.
- iterates AOV bindings and sends them to `RenderBuffer::bind()`.

`Camera.cc`:

- reads Hydra camera projection/window state.
- derives aperture, offsets, focal, near, and far.
- applies `CameraUtilConformedWindow(adjustedAperture, GetWindowPolicy(), renderAspect)`.
- writes MoonRay RDL camera attributes including film aperture and offsets.

## Diagnostic Required

Use:

```sh
export HDMR_CAMERA_FRAMING_DIAG=1
```

`RenderPass.cc` diagnostics must log:

- viewport
- framing valid yes/no
- displayWindow
- dataWindow
- pixelAspectRatio
- computed width/height
- computed render aspect
- RDL image_width/image_height
- camera path/id
- render tags
- AOV binding count

`Camera.cc` diagnostics must log:

- camera id/path
- projection class
- window policy
- horizontal aperture
- vertical aperture or derived aperture height
- horizontal/vertical offsets
- focal length
- clipping range
- input render aspect
- `CameraUtilConformedWindow` input aperture
- `CameraUtilConformedWindow` output aperture
- final MoonRay RDL camera attributes affected by aspect/framing

## Patch Policy

Only patch after diagnostics identify the failed layer:

- DCC authoring fix only if exported USD is wrong.
- RenderPass fix only if Hydra gives correct framing but hdMoonray computes
  width/height or render aspect incorrectly.
- Camera.cc fix only if render aspect is correct but conform/window output is
  applied incorrectly to RDL.
- Houdini/Solaris path fix only if IPR and husk supply different Hydra framing
  state for the same USD intent.
- Documentation only if behavior is correct and manual comparison used
  mismatched camera gate/policy.

## Comparison Matrix

Required test cases after diagnostics are installed:

| Case | Purpose |
|------|---------|
| camera-native aspect, e.g. 1828x1332 | Baseline matching camera aperture aspect. |
| changed aspect, e.g. 1024x1024 | Reproduce aspect/framing mismatch. |
| user manual compensation that visually matches | Identify which layer the manual change compensates for. |

For each case, save:

- USD RenderSettings/Product/camera dump.
- `HDMR_CAMERA_FRAMING_DIAG` log.
- RDLA dump if available.
- EXR or stats if render completes.

## 2026-06-16 Headless Husk Aspect Matrix

Controlled `husk --settings /Render/rendersettings` probes were rendered with
matching RenderSettings/Product resolution and the same USD camera:

- `horizontalAperture = 20.954999923706055`
- `verticalAperture = 15.290800094604492`
- camera aperture aspect is approximately `1.370`.

For all headless husk cases:

- `RenderPassState::GetFraming()` was invalid.
- hdMoonray fell back to `GetViewport()`.
- The viewport matched the requested output resolution.
- RDL `image_width` and `image_height` matched the requested output resolution.
- `CameraUtilConformedWindow` was called with `HdCamera` window policy `0`.

Observed matrix:

| Case | Viewport / RDL size | Aspect passed to camera | Conformed aperture | Interpretation |
|------|----------------------|-------------------------|--------------------|----------------|
| camera-aspect `137x100` | `137 x 100` | `1.37` | `(20.9484, 15.2908)` | Nearly preserves the authored camera gate. |
| square `100x100` | `100 x 100` | `1.0` | `(15.2908, 15.2908)` | Narrows horizontal film aperture for square output. |
| wide `160x90` | `160 x 90` | `1.77778` | `(27.1836, 15.2908)` | Widens horizontal film aperture for wide output. |

Current interpretation:

- Headless husk is propagating output dimensions to hdMoonray.
- hdMoonray is applying aspect through `CameraUtilConformedWindow`.
- The square-output composition change is therefore not caused by lost
  RenderSettings/Product resolution in the batch path.
- The remaining parity question is whether Solaris viewport/IPR supplies the
  same render pass viewport/framing/window policy as husk for the same authored
  USD and camera gate.

No camera behavior patch is evidence-backed yet. The next proof is the
user-run Solaris/IPR diagnostic log with `HDMR_CAMERA_FRAMING_DIAG=1` so the
viewport/IPR render-pass state can be compared against the batch husk state.
