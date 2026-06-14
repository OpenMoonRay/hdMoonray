# hdMoonray Stable vs WIP Matrix

This matrix records the current evidence-backed status of recent hdMoonray and Houdini Solaris integration work. It is meant to keep future work honest about what is proven, what is partial, and what should only be used as cautionary evidence.

Public PRs are useful anchors, but local submodule history is the source of truth for the exact state audited here:

- OpenMoonRay/openmoonray PR #228: Houdini/macOS compatibility and superproject integration context.
- OpenMoonRay/moonray_dcc_plugins PR #3: Houdini 20.5 MoonRay material builder context.
- Parent checkpoint pin `be43cbe6a77ec584ecea005101cdd78b0cd1e663`.
- hdMoonray SpotLight checkpoint `27201a47dae82a7df1eb4588dafa2c4c046a6623`.
- dcc SpotLight UI checkpoint `9bf4945815b3e6c4a39a62702ef89be0453ecc28`.

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
| Native Camera controls: shutter bias and bokeh | Stable / proven for exposed controls only | Current local DCC quick win | `Camera.cc`, `moonray_Camera.ds`, `coredata/PerspectiveCamera.json`, `coredata/OrthographicCamera.json` | Camera LOP Moonray folder exposes `moonray:mb_shutter_bias` and bokeh attrs; defaults author nothing; explicit values author USD `moonray:*`; RDLA proves `PerspectiveCamera("primaryCamera")` receives shutter bias, USD DOF, and bokeh attrs when DOF is enabled. | Stable only for `mb_shutter_bias`, `bokeh`, `bokeh_sides`, `bokeh_image`, `bokeh_angle`, `bokeh_weight_location`, and `bokeh_weight_strength`. Camera class switching, alternate camera classes, stereo, `pixel_sample_map`, and bake/projector workflows remain deferred. |
| Camera LOP DS discovery / installed UI validation | Stable DCC infrastructure pattern | Current local DCC quick win | `pythonrc.py`, `moonray_Camera.ds`, DCC CMake install script | `pythonrc.py` searches `soho/parameters/<renderer>_<parmgroup>.ds`; Camera LOP with forced renderer `moonray` expects `moonray_Camera.ds`. Normal H20.5 loads `/Applications/MoonRay/installs/openmoonray/plugin/houdini`, so source-path HOM validation was insufficient until CMake install synced the DS file. | Future DS/HDA work must validate fresh normal H20.5 UI visibility against the installed package path. |
| H20.5 Solaris architecture / infrastructure | Stable infrastructure | `77a673e` | `PrimTypeUtils.*`, `ValueConverter.cc`, `RenderSettings.*`, `UsdRenderers.json`, adapter compatibility doc, fixtures | Adds H20.5 compatibility, canonical prim type handling, value conversion, render settings plumbing, and test fixtures. | Reuse utilities and compatibility patterns, but do not treat the whole commit as a narrow feature recipe. |
| Render Settings LOP | Partial / WIP | `moonray_dcc_plugins` `8c995f8` through current local repair | `moonray_render_settings.py`, generated HDA, `moonray_render_settings_lop_audit.md`, validation script | Source-generated HDA, USD RenderSettings/Product foundation, default Beauty RenderVar for H20.5 disk output, and owned USD Render ROP lifecycle are useful. Non-beauty AOV integration and final viewport/IPR UX remain unsettled. | Good process evidence for source-of-truth HDA generation; not yet a stable renderer-settings pattern. Current default authors `RenderProduct` with `productName`, `productType = raster`, and `orderedVars` targeting the Beauty RenderVar because empty `orderedVars` failed in H20.5 production `husk`. Disabling `aov_beauty` is diagnostic only. |
| Beauty buffer / USD Render ROP support | Partial / WIP | hdMoonray `2eb0808` plus current local `RenderBuffer.cc` repair | `RenderBuffer.cc`, `ArrasRenderer.cc`, `UsdRenderers.json` | Beauty path now has production H20.5 `husk` EXR proof for direct camera/default color, custom LOP default, and generic Render Settings plus built-in RenderProduct/RenderVar. Non-beauty AOVs remain blocked/deferred. | Do not claim full image-buffer/AOV support from this. The narrow proven backend fix maps Houdini's default `HdAovTokens->color` binding to MoonRay beauty before interpreting `sourceName/sourceType`; it does not prove non-beauty AOV transport. |
| AOV / native non-beauty investigation | Experimental / failure contrast | `docs/hdMoonray_aov_audit.md` plus scratch artifacts | `RenderBuffer.cc`, `RenderPass.cc`, `ArrasRenderer.cc`, MoonRay transport source, audit doc | `cameraDepth` and native `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight` are mapped/declared; debug renderer produces meaningful payloads; production payloads are zero-filled. | Use as a process warning: authoring metadata, RDLA creation, channel existence, and debug renderer success are not enough. Production H20.5 filled EXR stats are required before exposing non-beauty AOV UI. |
| OCIO / renderingColorSpace | Partial / WIP | 2026 H20.5 OCIO/color audit and working-space repair | `ColorManagement.cc`, `RenderDelegate.cc`, `ValueConverter.cc`, `Material.cc`, `Light.cc`, `LightFilter.cc`, MoonRay `UsdUVTexture`, `ImageMap`, scratch USD/RDLA/EXR probes | `RenderSettings.renderingColorSpace` is consumed for authored typed color values before MoonRay/RDL assignment. Constant-color RDLA values now differ for ACEScg and Linear Rec.2020. TX source-color behavior is separate and partially proven. | Do not claim complete texture destination gamut conversion, MaterialX image color management, AOV color management, or viewport/IPR match yet. Do not claim support from OIIO metadata or `husk` output alone. |
| Unit policy / normalized lights / SSS scale | WIP / deferred | `8d1ded4` | `docs/units-and-scale-notes.md` | Documents that `metersPerUnit` is ignored, `nonrayscene_scale` behavior is unresolved, and SSS distances pass raw. | Do not fix during render settings or AOV work without a dedicated unit-policy pass. |

OCIO / renderingColorSpace status notes:

- Before the repair, constant-color tests produced identical RDLA for no color
  space, Linear Rec.709, ACEScg, and Linear Rec.2020, while `husk` EXR pixels
  differed.
- After the repair, constant-color tests produce different RDLA material values
  for ACEScg and Linear Rec.2020, and direct MoonRay renders follow those values.
- TX tests proved `UsdUVTexture.sourceColorSpace = auto/sRGB` follows MoonRay
  8-bit gamma behavior and `raw` differs.
- Use TX textures, treat source texture color explicitly, and keep MaterialX
  image color-space behavior unknown until measured. Texture destination gamut
  conversion remains unimplemented.

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

- Non-beauty AOV and `cameraDepth` transport work.
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
