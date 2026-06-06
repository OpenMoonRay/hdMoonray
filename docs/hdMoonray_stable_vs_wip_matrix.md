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
| H20.5 Solaris architecture / infrastructure | Stable infrastructure | `77a673e` | `PrimTypeUtils.*`, `ValueConverter.cc`, `RenderSettings.*`, `UsdRenderers.json`, adapter compatibility doc, fixtures | Adds H20.5 compatibility, canonical prim type handling, value conversion, render settings plumbing, and test fixtures. | Reuse utilities and compatibility patterns, but do not treat the whole commit as a narrow feature recipe. |
| Render Settings LOP | Partial / WIP | `moonray_dcc_plugins` `8c995f8` through `6cd0361` | `moonray_render_settings.py`, generated HDA, `moonray_render_settings_lop_audit.md`, validation script | Source-generated HDA, USD RenderSettings/Product/RenderVar foundation, and owned USD Render ROP lifecycle are useful. AOV integration and final UX remain unsettled. | Good process evidence for source-of-truth HDA generation; not yet a stable renderer-settings pattern. |
| Beauty buffer / USD Render ROP support | Partial / WIP | hdMoonray `2eb0808` | `ArrasRenderer.cc`, `UsdRenderers.json` | Beauty path is useful progress, but non-beauty AOVs remain blocked. | Do not claim full image-buffer/AOV support from this commit. |
| AOV / cameraDepth investigation | Experimental / failure contrast | `docs/hdMoonray_aov_audit.md` plus scratch artifacts | `RenderBuffer.cc`, `RenderPass.cc`, `ArrasRenderer.cc`, MoonRay transport source, audit doc | `cameraDepth` debug path works, production payload arrives zero-filled; PackTiles/weight path remains unresolved. | Use as a process warning: authoring metadata and RDLA creation are not enough; production EXR stats are required. |
| Unit policy / normalized lights / SSS scale | WIP / deferred | `8d1ded4` | `docs/units-and-scale-notes.md` | Documents that `metersPerUnit` is ignored, `nonrayscene_scale` behavior is unresolved, and SSS distances pass raw. | Do not fix during render settings or AOV work without a dedicated unit-policy pass. |

## What Is Safe To Use As A Pattern

Use these as primary examples:

- Material builder / native material subnet.
- DomeLight alias-to-EnvLight support.
- Explicit native SpotLight override and UI helper.
- RectLight ShapingAPI-to-`spread` preservation.
- Geometry/subdivision/tessellation prim-level exposure.

Use these only as supporting infrastructure examples:

- H20.5 Solaris architecture commit `77a673e`.
- RenderSettings source-generated HDA mechanics.

Use these only as cautionary examples:

- Non-beauty AOV and `cameraDepth` transport work.
- Any behavior validated only in debug renderer but not production H20.5.

## Evidence Rules For Changing Status

An area may move to stable only when the relevant layer has proof:

- USD authoring or Hydra input is correct.
- hdMoonray maps it to native MoonRay/RDL concepts.
- RDLA/RDL proves the intended SceneObject, attribute, SceneVariable, or RenderOutput.
- H20.5 production render or Houdini UI behavior proves the user-facing result.
- EXR stats prove image data when the feature is image-buffer/AOV related.
- The repo, build, install, and runtime path were verified.

