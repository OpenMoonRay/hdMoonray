# Houdini 20.5 SceneIndex Dirty Stack Audit

Date: 2026-06-15

Scope: audit only. No runtime code, build files, installed payloads, generated
HDAs, or user scenes were changed during this pass before this document was
written.

## Preserved State

Current dirty diffs were preserved before further inspection:

```text
/tmp/moonray_sceneindex_audit_preserve_20260615_233253/parent.diff
/tmp/moonray_sceneindex_audit_preserve_20260615_233253/hdmoonray.diff
/tmp/moonray_sceneindex_audit_preserve_20260615_233253/sdr_plugins.diff
/tmp/moonray_sceneindex_audit_preserve_20260615_233253/dcc.diff
```

Repository heads at preservation time:

| Repo | Branch | HEAD | Dirty status |
| --- | --- | --- | --- |
| parent `openmoonray` | `Moonray-Houdini21-macOS` | `d169571e144d42d7542bcfcc0d9a4f3fc61a119d` | submodules dirty |
| `moonray/hydra/hdMoonray` | `h20.5-solaris-building-testing` | `b466c68c0b162d2ae46b2cbe216ccb0a1069233b` | 6 modified files |
| `moonray/hydra/moonray_sdr_plugins` | `h20.5-sdr-python-stubs` | `643f962d8fcebbc44a7e47113e07c53d50c424ff` | 1 modified file |
| `moonray/moonray_dcc_plugins` | `houdini20.5-materialx-vopmask` | `bf2543c1273c3cb5ff84265a4c372e0289cfbb05` | 4 modified files, 1 tracked pyc deletion |

The committed displacement fix baseline is `b466c68`
(`hdMoonray: skip no-op NormalDisplacement bindings`). It is not part of the
current uncommitted stack and is treated as protected unless direct regression
evidence appears.

## Dirty Stack Ledger

| File | Repo | Category | Affects | Risk / action | Evidence |
| --- | --- | --- | --- | --- | --- |
| `cmd/hd_cmd/hd_render/CMakeLists.txt` | hdMoonray | toolchain/build compatibility | command-line tools only | commit separately if retained | Adds Apple/Python rpath when `Python_ROOT_DIR` exists; intended to fix `@rpath/Python` loading for `hd_render`. |
| `cmd/hd_cmd/hd_usd2rdl/CMakeLists.txt` | hdMoonray | toolchain/build compatibility | command-line tools only | commit separately if retained | Same rpath repair for `hd_usd2rdl`. |
| `lib/hydramoonray/PrimTypeUtils.cc` | hdMoonray | SceneIndex / plugin light filter support | Hydra delegate support contract, RDL light filters | investigate before commit | Adds `PluginLightFilter` and `UsdLuxPluginLightFilter` aliases to canonical `lightFilter`; unrelated to camera framing. |
| `lib/hydramoonray/RenderDelegate.cc` | hdMoonray | RenderSettings/SceneIndex runtime sync plus stats | Hydra Bprim support, SceneVariables, progress metadata | investigate before commit | Adds real `HdRenderSettings` Bprim, live scene-variable update hook, flat Houdini progress keys, and memory stats. No fake Product/Var support is present. |
| `lib/hydramoonray/RenderDelegate.h` | hdMoonray | RenderDelegate lifetime / live setting sync | delegate ABI and renderer ownership | investigate before commit | Converts renderer pointer to `std::unique_ptr`, adds render-pass execution marker. Requires ABI-aligned install if committed. |
| `lib/hydramoonray/RenderPass.cc` | hdMoonray | render lifecycle | render settings live-apply guard | investigate before commit | Marks render-pass execution before settings application. Not directly a framing fix. |
| `moonrayShaderParser/parserPlugin.cpp` | SDR | SDR metadata | shader discovery, USD shader metadata | commit separately if proven | Fixes vector default/type metadata, including dynamic array Sdr USD definition types. Related to `iridescence_colors`, not framing. |
| `houdini/python3.11libs/moonray_render_settings.py` | DCC | DCC Python authoring | authored USD RenderSettings SceneVariables, UI | investigate before commit | Adds `texture_cache_size` and `texture_file_handles` SceneVariables and UI controls. Not a camera/framing fix. |
| `houdini/otls/Lop::DW_MOONRAY::moonrayrendersettings::1.hda` | DCC | HDA binary regeneration | Houdini UI/runtime HDA | investigate before commit | Binary regenerated to match DCC Python changes. Source/install SHA currently match, but no new regeneration should happen until approved. |
| `houdini/tests/dev_validate_moonray_render_settings_lop.py` | DCC | validation | DCC validation | investigate before commit | Updates expected SceneVariable count to 52 and checks texture cache controls. |
| `houdini/docs/moonray_render_settings_lop_audit.md` | DCC | docs/evidence update | docs only | investigate before commit | Documents texture cache controls as exposed. |
| `houdini/python3.11libs/__pycache__/moonray_lightfilter_nodes.cpython-314.pyc` | DCC | generated junk deletion | source tree cleanliness | likely keep deletion, but separate hygiene | Tracked pyc deletion in source repo. Installed pyc files still exist. |

No dirty file in this stack directly changes `Camera.cc`, camera aperture
translation, RenderProduct resolution authoring, or `moonray_Camera.ds`.

## Python / DCC / HDA Provenance

Hython was run after sourcing Houdini 20.5.584 from its Resources directory and
then sourcing `/Applications/MoonRay/source/openmoonray/scripts/macOS/setupHoudini.sh`.

Observed active import paths:

```text
PYTHONPATH=/Applications/MoonRay/installs/openmoonray/lib/python:/Applications/MoonRay/installs/openmoonray/lib/python
PXR_PLUGINPATH_NAME=/Applications/MoonRay/installs/openmoonray/plugin/pxr:/Applications/MoonRay/installs/openmoonray/plugin/pxr
moonray_render_settings => /Applications/MoonRay/installs/openmoonray/plugin/houdini/python3.11libs/moonray_render_settings.py
moonray_material_builder => /Applications/MoonRay/installs/openmoonray/plugin/houdini/python3.11libs/moonray_material_builder.py
moonray_lightfilter_nodes => /Applications/MoonRay/installs/openmoonray/plugin/houdini/python3.11libs/moonray_lightfilter_nodes.py
active Render Settings HDA => /Applications/MoonRay/installs/openmoonray/plugin/houdini/otls/Lop::DW_MOONRAY::moonrayrendersettings::1.hda
```

Source/install SHA-256 matches were confirmed for:

| Payload | SHA status |
| --- | --- |
| `moonray_render_settings.py` | source and installed match: `0c8855c95fc9c270b8107625f2a67fd30ed7dd745957365144987e83f04b0058` |
| `moonray_material_builder.py` | source and installed match: `513c54860075b403e6700434fbdce0c8c067a4b6c167888c30770f094cf595f2` |
| `moonray_lightfilter_nodes.py` | source and installed match: `08deaccaabc847e4171cf12c1e99d82d0296c515f710435da5d0226666417382` |
| Render Settings HDA | source and installed match: `d511f5d6d63cd822721b022a02ade32c5f0fdbaef115ddb211d7d28f605d1bd5` |
| `moonray_Camera.ds` | source and installed match: `619e2f72facb577fcd7dfc81f6df75a03ce75e55c0731304b2cf8c3b4182f051` |

Verdict: the active H20.5 process loads installed plugin Python/HDA, but the
installed payload currently matches source for the inspected DCC files. Stale
source/install mismatch is not proven as the current framing cause.

The broad `scripts/setup.sh` PYTHONPATH line added in parent commit `63570f0`
still exists:

```text
${omr_root}/lib/python:${install_root}/lib/python:/usr/local/lib/python:${install_root}/lib64/python3.11/site-packages/
```

However, current `scripts/macOS/setupHoudini.sh` snapshots and restores
`PYTHONPATH` immediately after sourcing installed `setup.sh`, then prepends only
installed `lib/python`. In the audited H20.5 shell, the old `/usr/local/lib/python`
and `lib64/python3.11/site-packages` entries were not active. The setup commit
is therefore a provenance risk in generic shell use, but not proven to explain
this H20.5 Render ROP framing mismatch.

Installed junk risk remains: `/Applications/MoonRay/installs/openmoonray/plugin/houdini/otls/backup`
contains many HDA backup files, and installed `python3.11libs/__pycache__`
contains pyc files. These were observed only; nothing was deleted.

## DCC Authoring Verdict

Active `moonray_render_settings.py::author_from_node()` authors:

- `UsdRender.Settings` at `render_settings_prim`.
- `settings.products` targeting the product.
- `settings.camera` when the camera parm is non-empty.
- `settings.resolution` from the LOP `resolutionx/resolutiony` parms.
- `UsdRender.Product.productName`.
- `UsdRender.Product.productType = "raster"`.
- `RenderProduct.orderedVars`.
- RenderVars for Beauty/native/material AOV toggles.
- `moonray:sceneVariable:*` attributes for curated scene variables.

It does not author `RenderProduct.resolution`, `RenderProduct.pixelAspectRatio`,
`RenderProduct.dataWindowNDC`, or `RenderProduct.aspectRatioConformPolicy`.
Therefore the `RenderProduct.resolution = (2048, 1080)` opinion in the real
USDA is not authored by this custom MoonRay Render Settings LOP function. It is
either inherited from USD schema fallback, authored by a native Houdini
RenderProduct/ROP layer, or composed from another source.

Current DCC verdict: the wrong framing is already suspect in authored/composed
USD because the real scene contains competing aspect authorities before
hdMoonray translates to RDL.

## USD Contract Comparison

Files inspected:

```text
OLD=/Users/j7s/houdini-projects/sculpt-practice/stage2/sculpt-practice-init6-test.usd_rop1.usda
NEW=/Users/j7s/houdini-projects/sculpt-practice/stage-test/sculpt-practice-init6-test.usd_rop2.usda
```

Detailed dumps were written to:

```text
/Users/j7s/houdini-projects/sculpt-practice/render-test/sceneindex_usd_old.txt
/Users/j7s/houdini-projects/sculpt-practice/render-test/sceneindex_usd_new.txt
/Users/j7s/houdini-projects/sculpt-practice/render-test/sceneindex_usd_diff.txt
```

Key facts:

| Field | OLD | NEW | Status |
| --- | --- | --- | --- |
| `/Render/rendersettings.camera` | `/cameras/camera7` | `/cameras/camera7` | same |
| `/Render/rendersettings.resolution` | `(1828, 1332)` | `(1024, 1024)` | differs |
| `/Render/Products/renderproduct.resolution` | `(2048, 1080)` | `(2048, 1080)` | same product override/fallback |
| camera7 focal/aperture/xform | equivalent through USD API | equivalent through USD API | same |
| `aspectRatioConformPolicy` | `expandAperture` on settings and product | `expandAperture` on settings and product | same |
| `dataWindowNDC` | `(0,0,1,1)` on settings and product | `(0,0,1,1)` on settings and product | same |
| `pixelAspectRatio` | `1` on settings and product | `1` on settings and product | same |
| Beauty RenderVar | `color4f`, source `color`, sourceType `raw` | same | protected stable AOV state |

The camera aperture aspect is approximately:

```text
20.954999923706055 / 15.290800094604492 = 1.370
```

OLD `RenderSettings.resolution` aspect:

```text
1828 / 1332 = 1.372
```

NEW `RenderSettings.resolution` aspect:

```text
1024 / 1024 = 1.000
```

Product fallback/override aspect:

```text
2048 / 1080 = 1.896
```

PROVEN: NEW has three relevant aspect contexts: camera aperture about 1.370,
RenderSettings square 1.000, and RenderProduct/default 1.896. Because
OpenUSD RenderSettingsBase says resolution and pixel aspect determine image
aspect, and `aspectRatioConformPolicy` resolves camera/image aspect mismatch,
this authored state is sufficient to explain a crop/zoom/framing mismatch if
IPR and husk choose different resolution authority.

Reference: OpenUSD RenderSettings docs state that camera aperture aspect comes
from Camera aperture attrs, image aspect comes from `resolution` and
`pixelAspectRatio`, and `aspectRatioConformPolicy` tells the renderer how to
resolve mismatch. OpenUSD RenderProduct docs state RenderProduct inherits the
same RenderSettingsBase properties and that `resolution` samples the render
camera screen window into a raster image.

## RDLA / Command Tool Findings

The requested old/new camera-specific `hd_usd2rdl` generation did not complete:

```text
hd_usd2rdl -in OLD -out sceneindex_old.rdla -camera /cameras/camera7 -time 47
hd_usd2rdl -in NEW -out sceneindex_new.rdla -camera /cameras/camera7 -time 47
```

failed with:

```text
Selected hydra renderer doesn't support prim type 'RenderProduct'
Cannot append absolute path </cameras/camera7> to another path </>.
Could not find camera /cameras/camera7 in 1
```

Using relative `-camera cameras/camera7` removed the absolute-path warning but
still failed with:

```text
Could not find camera cameras/camera7 in 1
```

The pre-existing RDLA:

```text
/Users/j7s/houdini-projects/sculpt-practice/render-test/camera_debug_rop2.rdla
```

was inspected. It is not valid camera7 framing proof because it contains:

```text
PerspectiveCamera("/app_scene/free_cam")
PerspectiveCamera("primaryCamera")
```

with identical free-camera transforms, and `SceneVariables["camera"]` points to
`PerspectiveCamera("primaryCamera")`. This looks like the command tool free
camera path, not the real `/cameras/camera7` render camera path.

RDLA verdict: camera-specific RDLA parity remains blocked by `hd_usd2rdl`
camera lookup for this stage. The existing `camera_debug_rop2.rdla` must not be
used as proof for Houdini IPR vs ROP camera7 framing.

## hdMoonray Source Ownership Map

| Contract area | Source path / function | Current evidence |
| --- | --- | --- |
| Camera sprim sync | `lib/hydramoonray/Camera.cc::Camera::Sync` and `Camera::updateCamera` | Reads Hydra camera params and writes MoonRay camera attrs. |
| Active render camera | `lib/hydramoonray/Camera.cc::Camera::setAsPrimaryCamera` | Copies selected camera to `PerspectiveCamera("primaryCamera")` and sets `SceneVariables.camera`. |
| Render-pass aspect | `lib/hydramoonray/RenderPass.cc::RenderPass::_Execute` | Uses `renderPassState->GetFraming().dataWindow` or viewport width/height, then calls `setAsPrimaryCamera(renderDelegate, double(w)/h)`. |
| MoonRay camera aperture | `lib/hydramoonray/Camera.cc::updateCamera` | Computes aperture from Hydra projection/camera attrs; only writes `film_width_aperture`, focal length, and offsets to RDL. |
| Window/conform policy | `Camera.cc::updateCamera` | Uses `CameraUtilConformedWindow(adjustedAperture, GetWindowPolicy(), mDesiredAspectRatio)`. Correct output aspect is therefore critical. |
| SceneVariables | `lib/hydramoonray/RenderSettings.cc::RenderSettings::apply` | Applies `moonray:sceneVariable:<name>` or `sceneVariable_<name>` except excluded image width/height. |
| Real RenderSettings Bprim | dirty `RenderDelegate.cc::RenderSettingsPrim::_Sync` | Forwards `renderingColorSpace` and namespaced settings through `SetRenderSetting`. Needs separate validation before commit. |
| RenderProduct/RenderVar Bprims | no fake support in dirty stack | Product/Var warnings still appear in `hd_usd2rdl`; no fake Product/Var Bprim was reintroduced. |
| PluginLightFilter | dirty `PrimTypeUtils.cc` | Adds aliases to existing `lightFilter` support; unrelated to camera framing. |

PROVEN source-path implication: hdMoonray derives MoonRay camera framing from
the render pass state aspect, not directly from `RenderProduct.productName`.
If Houdini/husk supplies a render pass dataWindow based on Product resolution
while IPR supplies viewport/framing based on another authority, `Camera.cc`
will conform the same USD camera differently.

## Camera Controls Commit Check

Parent commit `73b84c7094ba745bfc65824ecd5ea091118bff23` pins DCC commit
`b12bd04448f337fddfdad5fbd5269fa501921196`.

`b12bd04` adds `houdini/soho/parameters/moonray_Camera.ds`, exposing:

- `moonray:mb_shutter_bias`
- `moonray:bokeh`
- `moonray:bokeh_sides`
- `moonray:bokeh_image`
- `moonray:bokeh_angle`
- `moonray:bokeh_weight_location`
- `moonray:bokeh_weight_strength`

It does not author camera aperture, resolution, data window, pixel aspect, or
aspect conform policy. It is not proven to cause the current framing mismatch.

## Preliminary Verdict

Current audit verdict: **C. Mixed issue is most likely, with DCC/native USD
authoring conflict proven and hdMoonray translation dependence identified.**

What is PROVEN:

1. Active installed DCC Python/HDA matches source for the inspected files.
2. The new real scene USD has conflicting framing authorities before hdMoonray:
   camera aperture aspect about 1.370, `RenderSettings.resolution = 1024x1024`,
   and `RenderProduct.resolution = 2048x1080`.
3. The custom MoonRay Render Settings LOP function authors
   `RenderSettings.resolution` only; it does not author Product resolution or
   Product pixel aspect/data window/conform policy.
4. hdMoonray camera framing is controlled by render-pass framing/viewport aspect
   in `RenderPass::_Execute` and `Camera::setAsPrimaryCamera`.
5. `hd_usd2rdl` camera-specific RDLA generation for `/cameras/camera7` currently
   fails on this stage, so it cannot yet prove camera7 RDL parity.

What is OBSERVED:

1. Existing `camera_debug_rop2.rdla` uses a free camera path and is not suitable
   proof for the real camera7 mismatch.
2. `hd_usd2rdl` still emits `RenderProduct` unsupported warnings in this
   command-tool path.
3. OCIO conversion is disabled in the audited command-tool runs because
   `scene_linear` / `Linear Rec.709` was not found in the active config.

Current HYPOTHESIS:

The Houdini IPR vs USD Render ROP framing mismatch is caused by different
framing authorities being selected by different execution paths. IPR likely
uses viewport/live render-pass framing, while USD Render ROP/husk may use
RenderProduct resolution or RenderSettings resolution differently. Because
hdMoonray conforms the camera to the render-pass aspect, any mismatch in the
selected width/height produces a different MoonRay `film_width_aperture` and
visible crop/zoom.

Test to prove or disprove:

1. Capture the real Houdini IPR temp `__render__.usd` from a live working IPR.
2. Dump its RenderSettings/Product/Camera contract.
3. Compare its render-pass/output resolution authority against the new USD
   Render ROP USDA.
4. Produce camera7 RDLA from both paths, either through a fixed `hd_usd2rdl`
   camera lookup or via a production delegate RDLA output path that uses the
   actual RenderSettings camera relationship.

No runtime patch should be made until that IPR temp USD comparison is available
or until the user explicitly approves a targeted DCC/USD resolution-authority
patch based on the already-proven authored conflict.

