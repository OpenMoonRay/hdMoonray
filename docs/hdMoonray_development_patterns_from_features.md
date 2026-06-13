# hdMoonray Development Patterns From Features

This companion note maps individual audited features to the patterns they support. It exists because the stable examples span several repos and are easier to compare in one place than inside the broader guide.

## Material Builder / Native Material Subnet

Status: stable / proven.

Evidence:

- `moonray_dcc_plugins` commit `952eea3`.
- Files: `houdini/python3.11libs/moonray_material_builder.py`, `houdini/toolbar/MoonRayTools.shelf`.
- H20.5 fixture generation in hdMoonray commit `77a673e`.

Native contract:

- Houdini material workflows already support editable VOP/subnet material builders.
- MoonRay already provides real material and displacement VOP nodes.

Why it worked:

- The tool created an editable native Houdini subnet.
- It used real installed MoonRay VOP node types.
- It avoided a fake locked material HDA and avoided backend material translation hacks.

Copy this:

- Build on native Houdini node graph idioms.
- Use actual renderer node types.
- Keep shelf/tool code small and predictable.

Avoid this:

- Do not force DWA materials through unrelated MaterialX nodes as a workaround.
- Do not hide unsupported material behavior behind fake UI.

## DomeLight / EnvLight

Status: stable / proven.

Evidence:

- `moonray_dcc_plugins` commit `9cfd73f`.
- hdMoonray infrastructure commit `77a673e`.
- Files: `PrimTypeUtils.cc`, `PrimTypeUtils.h`, `Light.cc`, `moonray_Light.ds`, `HdMoonrayRendererPlugin_Light.ds`.
- H20.5 fixtures under `testSuite/runtime/h20_isolation` and `testSuite/light/env_schema_v1`.

Native contract:

- USD/Solaris may present dome lights as `DomeLight_1` or `UsdLuxDomeLight_1`.
- MoonRay native environment light class is `EnvLight`.

Why it worked:

- The change canonicalized the Solaris/Hydra type instead of patching a single UI symptom.
- The backend and DS files both learned the alias set.
- The native MoonRay class stayed `EnvLight`.

Copy this:

- Add canonicalization helpers for schema/runtime aliases.
- Keep alias handling close to prim type translation.

Avoid this:

- Do not treat one Houdini type spelling as the only valid USD type.
- Do not create fake light classes to match Houdini labels.

## SpotLight Native Override

Status: stable / proven for explicit native override and live class swapping.

Evidence:

- Backend commits `65e457b`, `27201a4`, `f5331a8`.
- UI commits `dbf22fa`, `b01d7e0`, `9bf4945`.
- Files: `Light.cc`, `Light.h`, `moonray_Light.ds`, `HdMoonrayRendererPlugin_Light.ds`.

Native contract:

- MoonRay has a real `SpotLight` SceneClass.
- MoonRay does not have a valid installed `PointLight` SceneClass in this setup.
- USD has renderer-neutral light types and UsdLux ShapingAPI, not a core USD SpotLight prim.

Why it worked:

- `moonray:class` overrides are validated against installed scene class metadata and `INTERFACE_LIGHT`.
- Invalid `PointLight` does not create missing DSO failures.
- The Houdini helper is explicit and ownership-safe.
- Class changes trigger light object reset rather than stale class state.

Copy this:

- Keep renderer-native modes explicit.
- Validate native classes semantically.
- Use shared cleanup helpers for class/lifecycle changes.

Avoid this:

- Do not default-author `moonray:class`.
- Do not make normal Solaris point intent a native MoonRay `PointLight`.
- Do not expose native SpotLight controls as if they were generic USD shaping controls.

## UsdLux Shaping And RectLight

Status: RectLight shaping stable; Sphere/Disk shaping policy needs future re-audit.

Evidence:

- `65e457b` introduced shaped Sphere/Disk to SpotLight behavior.
- `4396439` corrected RectLight behavior to preserve RectLight and map cone angle to `spread`.
- File: `Light.cc`.

Native contract:

- UsdLux ShapingAPI is renderer-neutral.
- USD cone angle is an off-axis angular limit.
- MoonRay SpotLight cone angles are full apex angles.
- MoonRay RectLight has native `spread`.

Why RectLight worked:

- The backend preserved rectangular emitter semantics.
- It used the native `spread` attribute instead of converting RectLight to SpotLight.

Copy this:

- Preserve the authored USD light type when MoonRay has a native shaped/spread equivalent.
- Convert units/angle conventions with comments and formulae.

Avoid this:

- Do not use native SpotLight as a universal fallback without proving no native equivalent exists.

## Geometry / Subdivision / Tessellation

Status: stable / proven.

Evidence:

- hdMoonray commits `d85ec55`, `24bac7e`.
- dcc commit `78ca757`.
- Files: `Mesh.cc`, `HdMoonrayRendererPlugin_Geometry.ds`, `testSuite/geometry/moonray_geometry_settings/*`.

Native contract:

- MoonRay mesh objects have native subdivision/tessellation settings.
- Geometry settings are prim-level, not global render settings.

Why it worked:

- Backend sets native attrs while respecting authored primvars.
- UI exposes renderer-specific geometry controls on geometry, not in the Render Settings LOP.
- Fixtures document the authored inputs and expected translation.

Copy this:

- Keep geometry behavior on geometry prims.
- Use primvar/attribute guards before applying defaults.

Avoid this:

- Do not put geometry tessellation controls in global render settings.
- Do not replace authored prim-specific values with renderer defaults.

## Native MoonRay Camera Controls

Status: stable / proven for the first low-risk Camera LOP controls.

Evidence:

- File: `moonray_dcc_plugins/houdini/soho/parameters/moonray_Camera.ds`.
- Backend source: `Camera.cc`.
- Installed metadata: `coredata/PerspectiveCamera.json` and
  `coredata/OrthographicCamera.json`.
- H20.5.584 validation in a normal installed package path after DCC install
  sync.

Native contract:

- MoonRay `PerspectiveCamera` and `OrthographicCamera` already have native
  attributes for `mb_shutter_bias`, `bokeh`, `bokeh_sides`, `bokeh_image`,
  `bokeh_angle`, `bokeh_weight_location`, and `bokeh_weight_strength`.
- hdMoonray already has generic `moonray:*` pass-through for camera attributes
  that exist on the active camera SceneClass.
- The `PerspectiveCamera("primaryCamera")` copy path already copies the selected
  motion-blur and bokeh attributes where needed.
- Standard USD camera attributes still own DOF through `fStop` and
  `focusDistance`.

Why it worked:

- The change stayed DCC-only because the backend was already consuming the
  selected native attributes.
- The UI controls default to `none`, so a default Camera LOP does not author
  `moonray:*` camera attributes.
- Explicit UI values author inspectable custom USD camera attrs such as
  `moonray:bokeh_sides` and `moonray:mb_shutter_bias`.
- RDLA proves both the authored camera and `primaryCamera` receive the values
  when USD DOF is enabled.
- The DOF-off probe showed bokeh attrs may remain on the authored camera object,
  but are not copied to `primaryCamera`; the controls should therefore be
  described as requiring USD DOF/fStop/focusDistance to be visibly effective.

Copy this:

- Use native camera metadata and existing backend pass-through before adding
  new camera translation code.
- Keep standard USD camera controls separate from renderer-specific `moonray:*`
  overrides.
- Use renderer DS control parms so defaults author nothing and explicit values
  remain inspectable in USD.
- Validate the visible folder in a fresh normal H20.5 session using the installed
  package path, not only a source-path HOM override.

Avoid this:

- Do not expose `moonray:class` or native camera class switching as part of this
  quick-win pattern.
- Do not expose FisheyeCamera, SphericalCamera, DomeMaster3DCamera, BakeCamera,
  stereo controls, `pixel_sample_map`, medium/material/projector, or bake-camera
  workflows without a separate backend/UI audit.
- Do not claim bokeh controls are visibly effective when USD DOF is off.

## Camera LOP DS Registration

Status: stable DCC discovery pattern.

Evidence:

- File: `moonray_dcc_plugins/houdini/python3.11libs/pythonrc.py`.
- Source DS: `moonray_dcc_plugins/houdini/soho/parameters/moonray_Camera.ds`.
- Installed package path:
  `/Applications/MoonRay/installs/openmoonray/plugin/houdini`.

Native contract:

- `pythonrc.py` overrides `loputils.addRendererParmFolders()`.
- The hook searches for `soho/parameters/<renderer>_<parmgroup>.ds`.
- With the forced renderer name `moonray`, Camera LOP renderer properties
  resolve to `soho/parameters/moonray_Camera.ds`.

Why it mattered:

- The first source-path HOM validation passed because the source
  `HOUDINI_PATH` could find `moonray_Camera.ds`.
- A normal Houdini session did not show the controls because it loaded the
  installed plugin tree, where the new DS file was missing.
- Running the existing DCC CMake install script synced the DS into the active
  installed package and made the normal Camera LOP folder visible.

Copy this:

- For renderer-specific LOP UI work, identify the exact DS filename Houdini
  expects before editing.
- Validate `hou.findFile()` in the same package/runtime path artists use.
- Confirm the installed DS matches source byte-for-byte after install sync.

Avoid this:

- Do not treat source-path HOM validation as enough proof for installed UI
  behavior.
- Do not manually hack installed DS files outside the source/install path.

## H20.5 Solaris Architecture Infrastructure

Status: stable infrastructure evidence.

Evidence:

- hdMoonray commit `77a673e`.
- Files include `PrimTypeUtils.*`, `ValueConverter.cc`, `RenderSettings.*`, `RenderDelegate.*`, adapter compatibility docs, and H20.5 fixtures.

Native contract:

- Houdini 20.5 uses its own USD/Hydra adapter environment and compatibility constraints.
- hdMoonray needs compatibility shims and careful value conversion to operate in that environment.

Why it worked:

- It centralized compatibility helpers and test fixtures.
- It added source-backed validation fixtures for runtime behavior.

Copy this:

- Add infrastructure when several features need the same bridge.
- Document compatibility work with fixtures.

Avoid this:

- Do not use a broad infrastructure commit as a template for small feature changes.

## Render Settings LOP

Status: partial / WIP.

Evidence:

- `moonray_dcc_plugins` commits `8c995f8` through `6cd0361`.
- Files: `moonray_render_settings.py`, generated HDA, `moonray_render_settings_lop_audit.md`, validation script.

Native contract:

- Solaris render setup should author USD RenderSettings, RenderProduct, and RenderVar prims.
- MoonRay renderer settings should be curated and namespaced.
- Generated HDA source must remain the source of truth.

What worked:

- Resolution and product path authoring.
- Source-generated HDA workflow.
- Owned USD Render ROP lifecycle policy.
- Curated SceneVariable authoring.
- Current H20.5 disk-output default: `aov_beauty = 1`, `UsdRender.Settings`, `UsdRender.Product`, `settings.products`, `settings.camera`, `settings.resolution`, `productName`, `productType = "raster"`, `orderedVars` targeting the Beauty RenderVar, and curated `moonray:sceneVariable:*`.
- The internal `aov_beauty` parm name is preserved for compatibility, but the UI label is `Beauty RenderVar / Disk Output Path`. Disabling it is diagnostic only.

What remains WIP:

- Final UI cadence.
- AOV integration.
- Renderer buffer/image-buffer behavior.
- Generic Houdini Render Settings with a MoonRay folder is UI integration evidence only; it is not complete render proof and is not a complete MoonRay output setup if flattened USDA has empty `rel products` and no RenderProduct/productName/productType.

Copy this:

- Keep generation source and HDA in sync.
- Keep standard USD render contracts valid.
- Keep the H20.5 disk-output Beauty RenderVar path unless fresh viewport/IPR, USD Render ROP/husk, and filled image/EXR proof justify another contract.

Avoid this:

- Do not hand-edit only the HDA.
- Do not expose unsupported AOV controls.
- Do not use the proven Beauty RenderVar path as evidence that non-beauty AOVs work.
- Do not claim AOV support from authored RenderVars, metadata, EXR channels, RDLA RenderOutput declarations, or debug renderer success alone.

## Render Settings / AOV Track Discipline

Status: process rule.

Track A and Track B may run in parallel when UI/USD authoring and backend runtime behavior are coupled. Parallel work is allowed; unsupported blending is not.

Track A covers DCC/UI/USD-contract work: the custom Render Settings LOP generator, regenerated HDA, validation scripts, docs, and installed/runtime source alignment.

Track B covers backend forensic work: `RenderBuffer.cc`, `ArrasRenderer.cc`, `RenderPass.cc`, `RenderDelegate.cc`, `UsdRenderers.json`, Beauty/AOV binding lifecycle, render settings dirtying/versioning, viewport/IPR refresh behavior, logs, render proof, and EXR stats.

Reports must keep DCC/USD evidence separate from backend/runtime evidence. Commits should stay separate unless a proven narrow fix requires both sides and the diff explains why.

Do not flip `aovsupport`, restart `cameraDepth`, broaden non-beauty AOV transport, or hide backend lifecycle bugs with UI cleanup.

## AOV / cameraDepth Investigation

Status: experimental / failure contrast.

Evidence:

- `hdMoonray_aov_audit.md`.
- hdMoonray `2eb0808` for beauty buffer support.
- Scratch artifacts under `/tmp/moonray_aov_audit`.

Native contract:

- MoonRay RenderOutput is native AOV output.
- USD RenderVar asks Hydra/render delegates for buffers.
- hdMoonray must bridge both and return filled production buffers.

What was useful:

- Beauty support progressed.
- The audit mapped RenderVar -> RenderBuffer -> RenderOutput -> transport -> EXR.
- It identified production transport/weight hazards.

What failed:

- `cameraDepth` was a diagnostic name, not the ideal product baseline.
- Production `cameraDepth/Pz` remained constant zero.
- Native `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight` generalized the same production failure: mapped and declared, meaningful in the debug renderer, but zero-filled in production H20.5 `HdMoonrayRendererPlugin`.
- Debug/local evidence did not prove production support.

Copy this:

- Keep detailed source/build/install/run evidence.
- Use EXR stats as the support gate.
- Keep non-beauty AOV UI hidden until production-filled payloads are proven.

Avoid this:

- Do not start UI exposure from authoring-only success.
- Do not claim AOV support from metadata/channels alone.
- Do not leave unsafe diagnostics installed.
