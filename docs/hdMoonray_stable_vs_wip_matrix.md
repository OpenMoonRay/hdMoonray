# hdMoonray Stable vs WIP Matrix

This matrix records the current evidence-backed status of recent hdMoonray and Houdini Solaris integration work. It is meant to keep future work honest about what is proven, what is partial, and what should only be used as cautionary evidence.

Public PRs are useful anchors, but local submodule history is the source of truth for the exact state audited here:

- OpenMoonRay/openmoonray PR #228: Houdini/macOS compatibility and superproject integration context.
- OpenMoonRay/moonray_dcc_plugins PR #3: Houdini 20.5 MoonRay material builder context.
- Current pushed H20.5 integration baseline: parent `55e5e6b`, hdMoonray `34d2d7e`, moonray_dcc_plugins `04bd9a8`, moonray_sdr_plugins `3c7f9b6`, moonray `e403e80`, moonshine `5bec7a4`, scene_rdl2 `b7aa377`.
- Earlier SpotLight checkpoint anchors: parent `be43cbe`, hdMoonray `27201a4`, dcc `9bf4945`.

## Status Categories

| Status | Meaning |
|---|---|
| Stable / proven | The behavior is implemented narrowly, maps to native MoonRay/USD/Houdini concepts, and has validation evidence from USD/RDLA/render/UI/source review. |
| Stable infrastructure | Useful foundational work that should be reused, but is too broad to copy as a single feature pattern. |
| Partial / WIP | Useful work exists, but behavior is incomplete, not fully validated, or still changing. |
| Experimental / failure contrast | Do not copy as an implementation pattern. Use only for lessons and future validation requirements. |

## Audited Areas

| Area | Status | Main commits / ranges | Primary files | Evidence | Notes |
|---|---|---|---|---|---|
| Material builder / native material subnet | Stable / proven | `moonray_dcc_plugins` `952eea3` | `houdini/python3.11libs/moonray_material_builder.py`, `houdini/toolbar/MoonRayTools.shelf` | Uses native Houdini VOP subnet workflow, creates real MoonRay VOP material/displacement nodes, and installs a shelf tool. H20.5 fixture generation exists in hdMoonray `77a673e`. | Good example of using native Houdini constructs instead of inventing a locked one-off material abstraction. |
| Material nodes | Stable / proven as existing dependency | Pre-existing MoonRay VOP node definitions plus material builder work | Houdini VOP node definitions and generated DS/OTL assets in dcc plugin | Material builder relies on real installed MoonRay node types such as DwaBaseMaterial and NormalDisplacement. | Do not document this as a new backend feature; document it as a stable dependency that the material subnet integrates. |
| DomeLight / EnvLight | Stable / proven | `moonray_dcc_plugins` `9cfd73f`, hdMoonray `77a673e` | `Light.cc`, `PrimTypeUtils.cc`, `PrimTypeUtils.h`, `moonray_Light.ds`, `HdMoonrayRendererPlugin_Light.ds` | Handles Houdini/USD dome aliases such as `DomeLight_1` and `UsdLuxDomeLight_1`, maps to native MoonRay `EnvLight`, includes H20.5 isolation fixtures. | Good example of canonicalizing Solaris/Hydra prim type aliases instead of adding ad hoc UI-only fixes. |
| SpotLight backend native override | Stable / proven for explicit override | `65e457b`, `27201a4`, `f5331a8` | `Light.cc`, `Light.h` | Explicit `moonray:class = "SpotLight"` validates against installed MoonRay SceneClass metadata; invalid `PointLight` remains invalid; class swaps share `resetLightObject()` cleanup used by `Sync()` and `Finalize()`. | Good example of validating semantic MoonRay classes instead of using a stale allowlist. |
| SpotLight UI/helper native mode | Stable / proven | `dbf22fa`, `b01d7e0`, `9bf4945` | `moonray_Light.ds`, `HdMoonrayRendererPlugin_Light.ds` | `moonray:class_control` default remains `none`; helper checkbox explicitly authors `moonray:class_control = set` and `moonray:class = SpotLight`; helper only clears its own override. | Good example of explicit renderer-specific override UI without default-authoring invalid renderer state. |
| SpotLight / UsdLux ShapingAPI connection | Partial but useful | `65e457b`, `4396439` | `Light.cc` | USD cone half-angle is converted to MoonRay SpotLight full apex angle with comments and formula; cone softness maps to inner cone. | Treat SphereLight/DiskLight shaping-to-SpotLight as current behavior to re-audit, not a final universal policy. |
| RectLight ShapingAPI behavior | Stable / proven | `4396439` | `Light.cc` | RectLight with authored ShapingAPI cone attrs remains MoonRay `RectLight`; cone angle maps to native `spread` as `clamp(angle, 0, 90) / 90`. | Strong example of preserving authored light class when MoonRay has a native equivalent. |
| Geometry render settings exposure | Stable / proven | `d85ec55`, `24bac7e`, `moonray_dcc_plugins` `78ca757` | `Mesh.cc`, geometry DS files, geometry fixture README/USD | Exposes real MoonRay geometry/subdivision attributes and preserves authored primvars through `isPrimvarUsed()` checks. | Good example of backend defaults plus prim-level override controls rather than global renderer settings. |
| Subdivision / tessellation exposure | Stable / proven | `d85ec55`, `24bac7e`, `78ca757` | `Mesh.cc`, `HdMoonrayRendererPlugin_Geometry.ds`, `testSuite/geometry/moonray_geometry_settings/*` | Maps USD subdivision/tessellation intent to native MoonRay mesh settings with tracked fixture evidence. | Future geometry work should follow this prim-level, metadata-backed pattern. |
| Native Camera controls: shutter bias and bokeh | Stable / proven for exposed controls only | parent `73b84c7`, dcc `b12bd04`, hdMoonray camera support in the pushed branch | `Camera.cc`, `moonray_Camera.ds`, `coredata/PerspectiveCamera.json`, `coredata/OrthographicCamera.json` | Camera LOP Moonray folder exposes `moonray:mb_shutter_bias` and bokeh attrs; defaults author nothing; explicit values author USD `moonray:*`; RDLA proves `PerspectiveCamera("primaryCamera")` receives shutter bias, USD DOF, and bokeh attrs when DOF is enabled. | Stable only for `mb_shutter_bias`, `bokeh`, `bokeh_sides`, `bokeh_image`, `bokeh_angle`, `bokeh_weight_location`, and `bokeh_weight_strength`. Camera class switching, alternate camera classes, stereo, `pixel_sample_map`, and bake/projector workflows remain deferred. |
| Camera LOP DS discovery / installed UI validation | Stable DCC infrastructure pattern | parent `73b84c7`, dcc `b12bd04` | `pythonrc.py`, `moonray_Camera.ds`, DCC CMake install script | `pythonrc.py` searches `soho/parameters/<renderer>_<parmgroup>.ds`; Camera LOP with forced renderer `moonray` expects `moonray_Camera.ds`. Normal H20.5 loads `/Applications/MoonRay/installs/openmoonray/plugin/houdini`, so source-path HOM validation was insufficient until CMake install synced the DS file. | Future DS/HDA work must validate fresh normal H20.5 UI visibility against the installed package path. |
| SDR parser type and ramp metadata | Stable / proven parser contract | parent `297217e`, moonray_sdr_plugins `3c7f9b6`, earlier stubs `643f962` | `moonrayShaderParser/parserPlugin.cpp`, `README.md`, parser/discovery CMake, `sitecustomize.py` | Houdini 20.5 SDR types now return valid Sdf types and matching defaults for bools, scalar colors/vectors/matrices, dynamic vector attrs, bindable-like values, and ramp metadata. Bool defaults are no longer inverted, `BoolVector` no longer goes through integer vector conversion, dynamic color/vector arrays use H20.5-accepted Sdf shapes, and ramp `structure_name/path/type` metadata is preserved. | Stable as parser metadata and discovery infrastructure. It does not by itself prove a shader network renders correctly; render proof still belongs to the material/texture/AOV-specific rows. |
| H20.5 Solaris architecture / infrastructure | Stable infrastructure | `77a673e` | `PrimTypeUtils.*`, `ValueConverter.cc`, `RenderSettings.*`, `UsdRenderers.json`, adapter compatibility doc, fixtures | Adds H20.5 compatibility, canonical prim type handling, value conversion, render settings plumbing, and test fixtures. | Reuse utilities and compatibility patterns, but do not treat the whole commit as a narrow feature recipe. |
| Setup and Houdini package environment pathing | Stable infrastructure | parent `63298dc`, `55e5e6b` | `scripts/setup.sh`, `scripts/macOS/setupHoudini.sh`, parent build-dependency pins | Setup no longer prepends brittle/nonexistent Python paths or hangs from source/symlink launch paths. `setupHoudini.sh` can provide setup-time `$OCIO` fallback from Houdini packages for terminal tools without overriding explicit user `$OCIO`. | Keep renderer/runtime OCIO resolution separate from shell setup. Future setup edits should prove installed, source, and symlink launch behavior. |
| Render Settings LOP | Partial / WIP | `moonray_dcc_plugins` `8c995f8` through pushed `04bd9a8`; parent pins `f374a65`, `9b35b25`, `0d8d2da`, `df238d3` | `moonray_render_settings.py`, generated HDA, `moonray_render_settings_lop_audit.md`, validation script | Source-generated HDA, USD RenderSettings/Product foundation, default Beauty RenderVar for H20.5 disk output, owned USD Render ROP lifecycle, output path wiring, sampling UI state, config-driven render-space UI, first native AOV controls, and first material/denoise AOV controls are useful. Broad AOV families and final viewport/IPR UX remain unsettled. | Good process evidence for source-of-truth HDA generation; not yet a fully general renderer-settings pattern. Current default authors `RenderProduct` with `productName`, `productType = raster`, and `orderedVars` targeting the Beauty RenderVar because empty `orderedVars` failed in H20.5 production `husk`. Disabling `aov_beauty` is diagnostic only. |
| Beauty buffer / USD Render ROP support | Partial / WIP | hdMoonray `2eb0808` plus current local `RenderBuffer.cc` repair | `RenderBuffer.cc`, `ArrasRenderer.cc`, `UsdRenderers.json` | Beauty path has production H20.5 `husk` EXR proof for direct camera/default color, custom LOP default, and generic Render Settings plus built-in RenderProduct/RenderVar. | Do not regress the narrow default-color fix: Houdini's default `HdAovTokens->color` binding must map to MoonRay beauty before interpreting `sourceName/sourceType`. |
| Native AOV production transport | Stable / proven for first explicit native set | `docs/hdMoonray_aov_audit.md`, scene_rdl2 Apple Silicon half-packing repair, MoonRay native half-output repair | `RenderBuffer.cc`, `ArrasRenderer.cc`, `PackTiles.cc`, `RenderOutputWriter.cc`, MoonRay transport source, audit doc | Explicit production H20.5 RenderVar tests produce filled `alpha`, `depth`, diagnostic `cameraDepth`, `Z`, `N`, `Ng`, `P`, `Wp`, and `St`; `weight` is constant nonzero in the simple fixture. The zero-fill root cause was H16 PackTiles half conversion on Apple Silicon, not USD authoring, RenderVar metadata, RDLA declaration, or final EXR writing. | Stable only for the enumerated explicit native set. `cameraDepth` remains diagnostic, not product-facing DCC UI. |
| DCC native AOV controls | Stable / proven for exposed opt-in set | `moonray_dcc_plugins` through pushed `04bd9a8`; see `moonray_render_settings_lop_audit.md` | `moonray_render_settings.py`, generated HDA, validation script | The Render Settings LOP exposes opt-in native AOVs `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight`, all default off. Beauty remains the default `color4f` disk-output RenderVar. | Do not infer support for arbitrary native RenderOutput targets from this set. Product-facing `cameraDepth` is intentionally not exposed. |
| Material / denoise AOV controls | Stable / proven for exposed opt-in set | `moonray_dcc_plugins` through pushed `04bd9a8`; see `moonray_render_settings_lop_audit.md` | `moonray_render_settings.py`, generated HDA, validation script | The exposed material/denoise set has production H20.5 EXR proof for Denoise Albedo, Denoise Normal, Material Albedo, Material Emission, Material Normal, Material Roughness, and Material PBR Validity. These controls are opt-in and default off. | Other material candidates such as `color`, `factor`, and `radius` remain deferred until a proof scene produces meaningful output. |
| Broad AOV families | Partial / WIP | AOV docs and DCC audit deferred lists | RenderOutput metadata, future UI/backend tests | LPE/light AOVs, visibility AOVs, primitive-attribute AOVs, arbitrary primvars, Cryptomatte, auxiliary adaptive buffers, display-filter outputs, motion vectors, multi-AOV products, and broad viewport/IPR AOV UX still need separate proof. | Keep this separate from the already proven native and material/denoise sets so the matrix does not understate shipped progress or overclaim general AOV coverage. |
| Config-driven OCIO render and texture source color | Partial / stronger WIP | hdMoonray `34d2d7e`, moonray `e403e80`, moonray_dcc_plugins `04bd9a8`, moonshine `5bec7a4`, parent `55e5e6b` | `ColorManagement.cc`, `RenderDelegate.cc`, native `ImageMap`/`UsdUVTexture`, shared `BasicTexture`/`UdimTexture`, Houdini Render Settings LOP, ImageMap DS, OCIO docs | Houdini-facing hdMoonray links SideFX OCIO 2.3.0; MoonRay native texture runtime links bundled standard OCIO 2.3.0. Render-space selection is config-driven by authored value, OCIO roles, and aliases; texture `auto` uses OCIO file rules only. RDLA proves `ImageMap.source_color_space` and `UsdUVTexture.source_color_space` reach RDL; tiny `hd_render -translate-only` and `husk` validate runtime diagnostics and EXR write. ARRI configs were validation inputs, not hidden renderer policy. | Viewport/IPR GUI proof, MaterialX image networks, AOV color behavior, display/view transforms, and full artistic look matching remain separate. |
| Unit policy / scene scale bridge / normalized lights / SSS scale | Partial / active bridge | scene_rdl2 `b7aa377`, hdMoonray `34d2d7e`, parent `55e5e6b` | `docs/units-and-scale-notes.md`, `SceneVariables.cc`, `RenderSettings.cc`, `hd_render.cc`, `hd_usd2rdl.cc`, generic Global `.ds` files | MoonRay `SceneVariables.scene_scale` default is now `1.0`; hdMoonRay bridges USD `UsdGeomGetStageMetersPerUnit(stage)` to `scene_scale`; explicit authored `moonray:sceneVariable:scene_scale` wins; raw radii, widths, lens radius, and material distances remain unscaled in RDLA. | Viewport/IPR scene-scale parity still needs manual validation. Do not add per-light radius scaling unless a focused fixture proves a radius/diameter or width/half-width mismatch. |

OCIO / renderingColorSpace status notes:

- Before the repair, constant-color tests produced identical RDLA for no color
  space, Linear Rec.709, ACEScg, and Linear Rec.2020, while `husk` EXR pixels
  differed.
- After the repair, constant-color tests produce different RDLA material values
  for ACEScg and Linear Rec.2020, and direct MoonRay renders follow those values.
- Native texture-source color is now explicit for MoonRay maps:
  `ImageMap.source_color_space = auto` uses OCIO file rules; `raw/data`
  bypass conversion; explicit names are honored; `UsdUVTexture.sourceColorSpace`
  remains `raw/sRGB/auto`; `UsdUVTexture.source_color_space` can override it.
- Legacy gamma remains available for old scenes, but is bypassed when an OCIO
  source transform is active.
- MaterialX image color-space behavior, AOV color behavior, and viewport/IPR
  color parity remain unknown until measured.

## What Is Safe To Use As A Pattern

Use these as primary examples:

- Material builder / native material subnet.
- DomeLight alias-to-EnvLight support.
- Explicit native SpotLight override and UI helper.
- RectLight ShapingAPI-to-`spread` preservation.
- Geometry/subdivision/tessellation prim-level exposure.
- Native Camera LOP shutter-bias and bokeh controls.

Use these only as supporting infrastructure examples:

- H20.5 Solaris architecture commit `77a673e`.
- RenderSettings source-generated HDA mechanics.
- Camera LOP DS discovery and installed-package validation.

Use these only as cautionary examples:

- Pre-repair non-beauty AOV zero-fill diagnostics and `cameraDepth`-specific hypotheses.
- Any behavior validated only in debug renderer but not production H20.5.

## Evidence Rules For Changing Status

Every status change must classify important supporting statements as `PROVEN`, `OBSERVED`, `HYPOTHESIS`, `UNKNOWN`, or `OUT OF SCOPE`. Do not move an area to stable from authored USD, UI visibility, RDLA declaration, or debug renderer proof alone.

No claim should be made without evidence. No recommendation should be made without a reproducer, source-path proof, exported USD proof, log proof, RDLA proof, render proof, or EXR proof. If a render is black, the path is functionally broken until render proof says otherwise. If evidence conflicts, record the conflict instead of choosing the convenient explanation.

For Render Settings and AOV work, Track A and Track B may run in parallel when UI/USD authoring and backend runtime behavior are coupled. Parallel work is allowed; unsupported blending is not. Keep DCC/USD evidence separate from backend/runtime evidence, and prefer separate commits unless a proven narrow fix requires both sides.

An area may move to stable only when the relevant layer has proof:

- USD authoring or Hydra input is correct.
- hdMoonray maps it to native MoonRay/RDL concepts.
- RDLA/RDL proves the intended SceneObject, attribute, SceneVariable, or RenderOutput.
- H20.5 production render or Houdini UI behavior proves the user-facing result.
- EXR stats prove image data when the feature is image-buffer/AOV related.
- The repo, build, install, and runtime path were verified.
