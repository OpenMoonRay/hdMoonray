# hdMoonray Feature Implementation Pattern

This document extracts implementation rules from recent stable hdMoonray and Houdini Solaris work. Every rule below is tied to an audited feature or commit range; it should be updated when future evidence changes.

## Pattern 1: Start With The Native MoonRay/RDL Contract

Rule: identify the real MoonRay scene class, attribute, SceneVariable, or RenderOutput before writing translation code or UI.

Evidence:

- SpotLight backend commits `65e457b`, `27201a4`, and `f5331a8` validate explicit `moonray:class` using installed MoonRay SceneClass metadata and `INTERFACE_LIGHT`.
- RectLight shaping commit `4396439` maps USD cone angle to the native MoonRay `RectLight.spread` attribute.
- Geometry/subdivision commit `d85ec55` maps to native MoonRay mesh settings instead of inventing global renderer controls.
- Material builder commit `952eea3` creates real MoonRay VOP material/displacement nodes rather than a fake material wrapper.
- Native Camera LOP controls use MoonRay `PerspectiveCamera` /
  `OrthographicCamera` metadata for `mb_shutter_bias` and bokeh attributes, with
  no new backend translation needed.

Why it worked:

- The renderer received attributes it actually understands.
- Invalid classes such as `PointLight` stayed invalid.
- RDLA/RDL could prove the final MoonRay object and values.

Future work should copy:

- Check installed metadata such as `coredata/*.json`.
- Check MoonRay source and scene class interfaces.
- Use semantic validation when possible.

Future work should avoid:

- Hardcoding stale class lists.
- Adding fake classes or aliases.
- Exposing UI controls that author unconsumed attributes.

## Pattern 2: Respect USD/Hydra/Solaris As The Input Contract

Rule: preserve standard USD/Solaris semantics unless the user explicitly chooses a renderer-specific override.

Evidence:

- DomeLight support uses canonical type handling for `DomeLight_1` / `UsdLuxDomeLight_1` and maps to MoonRay `EnvLight`.
- SpotLight UI commit `9bf4945` adds an explicit native MoonRay SpotLight helper instead of making all shaped USD lights native MoonRay SpotLights through UI defaults.
- RectLight shaping commit `4396439` preserves RectLight semantics and maps shaping to `spread`.
- Render Settings LOP WIP authors standard USD RenderSettings/Product prims, with RenderVar authoring only for explicit experimental/debug paths, instead of inventing a MoonRay-only render contract.

Why it worked:

- Solaris-authored scenes stayed renderer-neutral where possible.
- Explicit renderer-specific behavior remained inspectable in USD.
- Generic USD Render ROP / husk workflows remained viable.

Future work should copy:

- Treat USD schemas as the source input model.
- Translate schema attributes to MoonRay equivalents only when the equivalent is proven.
- Keep renderer-specific overrides explicit and namespaced.

Future work should avoid:

- Treating UsdLux ShapingAPI as automatically meaning native MoonRay SpotLight in all cases.
- Converting RectLight with cone shaping to SpotLight when RectLight has a native spread equivalent.
- Mapping point-light intent to nonexistent MoonRay `PointLight`.

## Pattern 3: hdMoonray Is A Translation Bridge

Rule: make hdMoonray translate between established contracts; do not make it an independent invention layer.

Evidence:

- `PrimTypeUtils` from `77a673e` provides canonical prim type handling for Houdini/Solaris aliases.
- `ValueConverter.cc` improvements support robust VtValue conversion, including later float conversion fixes in `24bac7e`.
- `Light.cc` maps Hydra light params to MoonRay light attributes and validates class choices.
- `Mesh.cc` handles geometry/subdivision prim-level attributes.

Why it worked:

- Each layer stayed understandable: USD/Hydra input, hdMoonray translation, MoonRay/RDL output.
- Fixes stayed narrow and testable.

Future work should copy:

- Add helpers when they clarify translation boundaries, like `PrimTypeUtils`.
- Keep object-specific behavior near the object translator.
- Use existing RenderDelegate/Light/Mesh/RenderSettings patterns before adding new infrastructure.

Future work should avoid:

- Broad cross-cutting conversions without feature proof.
- Global unit or scale multipliers before a unit policy exists.
- Backend changes that only hide invalid UI authoring.

## Pattern 4: Backend First, UI Second

Rule: expose UI only after the backend consumes the authored value and RDLA/render evidence proves it.

Evidence:

- SpotLight native controls were restored only after verifying MoonRay SpotLight attributes and backend consumption.
- The native SpotLight helper authors `moonray:class_control = set` and `moonray:class = SpotLight` only when explicitly enabled.
- Geometry settings DS exposure followed backend Mesh translation.
- Native Camera controls followed existing `Camera.cc` generic `moonray:*`
  pass-through and `primaryCamera` copy support; the DCC DS only exposed already
  consumed native attrs.
- Render Settings LOP remains partial/WIP because AOV UI must not expose non-working production AOVs.

Why it worked:

- Users did not get dead controls.
- Houdini UI stayed tied to inspectable USD/RDLA state.

Future work should copy:

- For every UI parm, list the authored USD property and consumed MoonRay attribute.
- Preserve original MoonRay metadata labels/help where possible.
- Use explicit controls for renderer-specific modes.
- For DCC/DS work, prove the folder is visible in a fresh normal H20.5 session
  using the installed package path.

Future work should avoid:

- UI checkboxes for AOVs whose production buffers are zero.
- Restoring old controls only because they used to exist.
- Default-authoring `moonray:class` for standard Solaris lights.
- Treating source-path HOM validation as installed UI proof.

## Pattern 5: Validate Semantic Classes And Attributes

Rule: validate class and attribute names against the runtime metadata/source rather than guessing.

Evidence:

- Explicit `moonray:class = SpotLight` became valid after checking the installed MoonRay SceneClass registry.
- Invalid `moonray:class = PointLight` remains invalid and falls back instead of creating missing DSO errors.
- SpotLight UI menus use valid installed light classes.
- Geometry and render settings work uses real MoonRay metadata names.

Why it worked:

- Normal Solaris lights stopped producing missing `PointLight` DSO errors.
- Valid native overrides could work without weakening validation.

Future work should copy:

- Query or inspect installed metadata first.
- Keep invalid explicit overrides warning and falling back cleanly.

Future work should avoid:

- Silent replacement of invalid classes with arbitrary classes.
- Fake PointLight support unless MoonRay core gains a real PointLight SceneClass.

## Pattern 6: Reuse Existing Lifecycle Patterns

Rule: before inventing object deletion/rebuild behavior, inspect how hdMoonray and RDL2 expect object lifetimes to work.

Evidence:

- SpotLight class swap cleanup moved into `resetLightObject()` in `f5331a8`.
- `resetLightObject()` is shared by `Sync()` and `Finalize()`.
- Old RDL light objects are made inert/unlinked rather than assuming direct delete/remove support.

Why it worked:

- Live class changes no longer required toggling unrelated Solaris spotlight controls.
- Cleanup followed an existing inert-object lifecycle expectation.

Future work should copy:

- Search for existing cleanup/finalize code first.
- Build narrow helpers when the same cleanup is needed in live update and finalization.

Future work should avoid:

- Creating class-suffixed replacement objects without cleanup.
- Assuming SceneObjects can be deleted from RDL2 during interactive Hydra updates without source proof.

## Pattern 6A: Separate DCC Cleanup From Backend Forensics

Rule: when a feature has both UI/USD authoring problems and runtime/backend symptoms, split the evidence into separate tracks.

Track A covers DCC/UI/USD-contract work: generators, HDAs, validation scripts, docs, and installed/runtime source alignment.

Track B covers backend forensic work: source tracing, logs, temporary diagnostics, render proof, EXR stats, viewport/IPR lifecycle proof, and runtime symptom proof.

Track A and Track B may run in parallel when the UI/USD contract and backend runtime behavior are coupled. Parallel work is allowed. Unsupported blending is not.

Reports must keep DCC/USD evidence separate from backend/runtime evidence. A UI/export result can prove authoring; it cannot prove render-buffer behavior. A backend log can prove runtime behavior; it cannot by itself prove the artist-facing UI contract.

For Render Settings/AOV work, Track B may inspect or instrument:

- `RenderBuffer.cc`
- `ArrasRenderer.cc`
- `RenderPass.cc`
- `RenderDelegate.cc`
- `UsdRenderers.json`
- Beauty/AOV binding lifecycle
- render settings dirtying/versioning
- viewport/IPR refresh behavior

Prefer separate commits for Track A cleanup/docs and Track B backend fixes. Use one mixed commit only if the backend root cause is proven, the fix is narrow, and the diff explains why UI/USD and backend behavior must change together.

Do not ship UI-only cleanup as a substitute for backend/runtime proof.

Do not flip broad renderer capability flags, such as `aovsupport`, because they appear related. Prove the minimal targeted change first.

Do not restart `cameraDepth` or broaden non-beauty AOV transport during Render Settings cleanup unless that is the reviewed task.

## Pattern 7: Prove With RDLA, Render, And UI Evidence

Rule: a feature is not proven by authored USD alone.

Evidence:

- DomeLight and SpotLight work used authored USD, RDLA/RDL class proof, render proof, and no missing DSO errors.
- Geometry/subdivision work includes a tracked fixture and README.
- AOV work showed that RenderVar metadata and RDLA RenderOutput creation were not enough; production buffers were still zero.

Why it worked:

- Multi-layer validation caught failures that single-layer checks missed.

Future work should copy:

- Validate USD, RDLA/RDL, render output, and Houdini UI behavior when relevant.
- For image buffers, require EXR stats proving nonzero/nonconstant data.

Future work should avoid:

- Promoting a feature because EXR channels exist but pixels are empty.
- Treating debug renderer behavior as production renderer behavior.

## Pattern 7A: Prove Installed Houdini UI Discovery

Rule: for DS/HDA work, the UI is not proven until Houdini loads the same
package/runtime path artists use.

Evidence:

- Native MoonRay Camera controls were first validated with a source
  `HOUDINI_PATH`, but a normal H20.5 Camera LOP still had no Moonray folder.
- `pythonrc.py` searches for `soho/parameters/<renderer>_<parmgroup>.ds`; for
  the forced renderer name `moonray` and Camera LOP parm group this means
  `soho/parameters/moonray_Camera.ds`.
- Normal H20.5 loaded DCC files from
  `/Applications/MoonRay/installs/openmoonray/plugin/houdini`, where
  `moonray_Camera.ds` was initially absent.
- Running
  `cmake -P /Applications/MoonRay/build/moonray/moonray_dcc_plugins/houdini/cmake_install.cmake`
  synced the source DS into the installed package. A fresh normal Camera LOP
  then showed the Moonray folder and exported the expected USD attrs.

Why it worked:

- The validation target changed from "can source find the DS file?" to "does
  normal Houdini load and display the DS file?".
- The fix used the existing source/install packaging path instead of a manual
  installed-file edit.

Future work should copy:

- Run `hou.findFile()` for the expected DS file in the normal package path.
- Create a fresh node after install sync, because parameter templates can be
  cached at node creation time.
- Confirm source and installed DS files match after the install step.

Future work should avoid:

- Adding a correct DS file only in source and reporting UI success from a custom
  `HOUDINI_PATH`.
- Adding duplicate renderer DS files without understanding whether
  `pythonrc.py` will load both.

## Pattern 8: Keep WIP Work Clearly Marked

Rule: partial progress should be documented, but not presented as supported behavior.

Evidence:

- Render Settings LOP has useful source-generated HDA and RenderSettings/Product foundation, but remains WIP.
- Beauty buffer support is partial because non-beauty AOVs remain unresolved.
- `cameraDepth` is explicitly documented as a diagnostic target and failure contrast.
- Unit scale policy is documented as deferred.

Why it worked:

- Future work can build from useful findings without accidentally promising unsupported behavior.

Future work should copy:

- Maintain status tables.
- Separate stable evidence from scratch/manual evidence.
- Record exact blockers.

Future work should avoid:

- Over-documenting AOV support.
- Exposing unresolved AOVs in artist UI.
- Adding broad fixes during unrelated foundation work.

## Feature Pattern Checklist

Before implementing:

- What is the native MoonRay/RDL concept?
- What is the USD/Hydra/Solaris concept?
- Where does the existing hdMoonray translation boundary live?
- Which repo owns the change?
- What is the pre-change baseline?
- What files should remain untouched?
- What would be the unsafe broad fix?
- What evidence will prove the change?

Before exposing UI:

- Is the backend implemented?
- Is the authored USD inspectable?
- Does RDLA/RDL prove the value?
- Does H20.5 production behavior prove it?
- Does a fresh normal H20.5 session using the installed package path show the
  intended parameter folder and controls?
- Are original MoonRay labels/defaults/help preserved where possible?
- Are defaults and UI ranges separate?

Before committing:

- Are generated files regenerated from source?
- Are only intended files changed?
- Are submodule pins left alone until the submodule commit is pushed?
- Is WIP explicitly marked?
