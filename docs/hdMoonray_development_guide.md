# hdMoonray Development Guide

This guide captures practical development rules from recent successful hdMoonray and Houdini Solaris integration work. It is not a complete user manual. It is a working guide for future changes in this multi-repo, submodule-heavy MoonRay/Houdini environment.

See also:

- `hdMoonray_feature_implementation_pattern.md`
- `hdMoonray_validation_workflow.md`
- `hdMoonray_stable_vs_wip_matrix.md`
- `hdMoonray_aov_audit.md`
- `units-and-scale-notes.md`
- `houdini20_5_hdMoonrayAdapters_compat.md`

## Source Of Truth

Public PRs are useful context, but local submodule state is the source of truth for development:

- Parent checkout: `/Applications/MoonRay/source/openmoonray`
- hdMoonray: `/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray`
- Houdini DCC plugin: `/Applications/MoonRay/source/openmoonray/moonray/moonray_dcc_plugins`
- MoonRay core: `/Applications/MoonRay/source/openmoonray/moonray/moonray`
- scene_rdl2: `/Applications/MoonRay/source/openmoonray/moonray/scene_rdl2`
- mcrt_dataio / client receiver: `/Applications/MoonRay/source/openmoonray/moonray/moonray_arras/mcrt_dataio`

The symlink `/Applications/MoonRay/openmoonray` may resolve to `/Applications/MoonRay/source/openmoonray`. Verify it before assuming which checkout is active.

Useful public anchors:

- OpenMoonRay/openmoonray PR #228: compatibility and superproject context.
- OpenMoonRay/moonray_dcc_plugins PR #3: material builder context.

Useful local checkpoints:

- hdMoonray SpotLight backend checkpoint: `27201a47dae82a7df1eb4588dafa2c4c046a6623`
- dcc SpotLight UI/helper checkpoint: `9bf4945815b3e6c4a39a62702ef89be0453ecc28`
- parent pin checkpoint: `be43cbe6a77ec584ecea005101cdd78b0cd1e663`

## Repo Ownership

Pick the owning repo before editing.

| Change type | Usually belongs in | Stable examples |
|---|---|---|
| Hydra translation, RDL object creation, SceneVariables, render buffers | `moonray/hydra/hdMoonray` | SpotLight class validation, RectLight spread, geometry settings |
| Houdini Solaris DS/UI, shelf tools, Python HDA generation | `moonray/moonray_dcc_plugins` | Material builder, SpotLight helper UI, Render Settings LOP prototype |
| MoonRay renderer core behavior, transport, scene_rdl2 classes | MoonRay core / scene_rdl2 / mcrt_dataio | AOV investigation touched these only diagnostically; do not edit without proof |
| Parent submodule pins | parent `openmoonray` | Checkpoint commits only after submodule changes are committed and pushed |

Do not solve a backend problem with a UI-only change. Do not solve a UI authoring problem by changing renderer semantics unless the backend contract is actually wrong.

For Render Settings and AOV work, split evidence into Track A and Track B. Track A covers DCC/UI/USD-contract work such as the custom Render Settings LOP generator, regenerated HDA, validation scripts, docs, and install/runtime source alignment. Track B covers backend forensic work such as source tracing, temporary diagnostics, logs, render proof, EXR stats, viewport/IPR lifecycle proof, and runtime symptom proof.

Track A and Track B may run in parallel when UI/USD authoring and backend runtime behavior are coupled. Parallel work is allowed; unsupported blending is not. Reports must keep DCC/USD evidence separate from backend/runtime evidence. Prefer separate commits for DCC cleanup/docs and backend fixes. Use one mixed commit only when the backend root cause is proven, the fix is narrow, and the diff explains why both sides must change together.

Backend files such as `RenderBuffer.cc`, `ArrasRenderer.cc`, `RenderPass.cc`, `RenderDelegate.cc`, and `UsdRenderers.json` may be inspected or temporarily instrumented when runtime evidence points there. They must not receive behavioral changes without source-path proof, exported USD proof, log proof, and render/EXR or runtime symptom proof.

## Evidence Gates And Claim Discipline

Before accepting a plan or implementation, classify important statements as `PROVEN`, `OBSERVED`, `HYPOTHESIS`, `UNKNOWN`, or `OUT OF SCOPE`.

A `PROVEN` claim must include exact evidence, such as a source path/function, commit hash, command output, exported USD/RDLA, installed-runtime checksum, render output, or EXR stats.

A `HYPOTHESIS` must include the exact test that would prove or disprove it.

Do not promote a claim from `HYPOTHESIS` to `PROVEN` because it sounds plausible or matches generic renderer intuition. Use MoonRay metadata/source, OpenUSD schemas, SideFX/Houdini evidence, exported USD, RDLA, logs, render output, and EXR stats.

Current hard rules:

- No claim without evidence.
- No recommendation without a reproducer, source-path proof, exported USD proof, log proof, RDLA proof, render proof, or EXR proof.
- UI visibility is not functional proof.
- Authored USD is not render proof.
- RDLA declaration is not image-buffer proof.
- Debug renderer success is not production renderer success.
- Houdini 21 behavior does not prove Houdini 20.5 behavior.
- Black render output means the path is functionally broken until render proof says otherwise.
- UI cleanup must not be used to hide backend lifecycle, dirtying, render-buffer, or AOV transport bugs.
- If evidence conflicts, report the conflict instead of choosing the convenient explanation.

Primary source-of-truth hierarchy:

1. Local runtime behavior in the target Houdini version, currently H20.5.
2. Exported USD from the exact scene state being tested.
3. Installed hdMoonray source and binaries currently loaded by Houdini.
4. MoonRay native metadata, especially `/Applications/MoonRay/installs/openmoonray/coredata/SceneVariables.json` and `/Applications/MoonRay/installs/openmoonray/coredata/RenderOutput.json`.
5. MoonRay docs and source.
6. OpenUSD RenderSettings/Product/Var schemas.
7. SideFX Houdini/HDK docs and local Houdini headers.
8. Local project docs and prior audit notes.

## Baseline Before Feature Work

Before implementing:

1. Identify pre-feature behavior.
2. Identify the native MoonRay/RDL concept.
3. Identify the USD/Hydra/Solaris concept.
4. Identify existing hdMoonray translation patterns.
5. Identify Houdini DS/HDA/runtime behavior if UI is involved.
6. Decide which repo owns the smallest correct change.

Successful examples:

- DomeLight support began with the Solaris/Hydra prim type alias problem and mapped `DomeLight_1` / `UsdLuxDomeLight_1` to native MoonRay `EnvLight`.
- RectLight shaping preserved the authored RectLight class and mapped cone angle to native `RectLight.spread`.
- SpotLight override validation checked installed MoonRay SceneClass metadata instead of accepting any `moonray:class` token.
- Geometry settings used native MoonRay mesh/subdivision attributes and avoided stomping authored primvars.

## Source / Build / Install / Runtime Mapping

Always prove the runtime is loading the code you changed.

Minimum checks:

```sh
pwd -P
git status --short
git rev-parse --abbrev-ref HEAD
git rev-parse HEAD
```

For compiled code, also verify:

- CMake source directory.
- CMake build directory.
- Installed plugin or dylib path.
- Build artifact checksum versus installed artifact checksum.
- Diagnostic strings are absent after cleanup if diagnostics were used.

The AOV audit showed why this matters: stale build/install/runtime artifacts can make a correct source conclusion look wrong, or worse, leave crashing diagnostics installed.

## Houdini 20.5 And Houdini 21 Evidence

Keep Houdini-version evidence separate.

The current production validation target for recent Solaris/AOV work has been Houdini 20.5. Do not use Houdini 21 behavior to prove a Houdini 20.5 production feature. PR #228 is valuable compatibility context, but H20.5 production behavior must be validated with the H20.5 build.

## HDA And DS Source Of Truth

Generated or installed UI artifacts are not the design source.

Rules:

- For generated HDAs, update the generation script first.
- Regenerate the HDA from the script.
- Confirm the regenerated HDA preserves the intended UI.
- Do not hand-polish only the `.hda`.
- Keep install-tree sync explicit and validated.

Stable evidence:

- `moonray_render_settings.py` became the source of truth for the custom MoonRay Render Settings LOP.
- Manual HDA-only drift was identified as a problem during Render Settings LOP iteration.

For DS files:

- Preserve original MoonRay-authored labels, defaults, ranges, menus, and help text unless the change is explicitly required.
- Do not use a default value to fake a UI range.
- Keep standard USD controls separate from renderer-specific overrides.

### Renderer-Specific LOP DS Discovery

Camera controls exposed a useful DCC integration trap. Houdini's MoonRay
`pythonrc.py` overrides `loputils.addRendererParmFolders()` and searches for
renderer parameter files using:

```text
soho/parameters/<renderer>_<parmgroup>.ds
```

Because the hook prepends the forced renderer name `moonray`, Camera LOP
renderer properties look for:

```text
soho/parameters/moonray_Camera.ds
```

That file is the expected source DS file for the normal Solaris Camera LOP
Moonray folder. Light and geometry follow the same pattern with
`moonray_Light.ds`, `HdMoonrayRendererPlugin_Light.ds`,
`moonray_Geometry.ds`, and `HdMoonrayRendererPlugin_Geometry.ds`.

Source-path HOM validation is useful but not sufficient. During the native
camera controls pass, source-path validation found `moonray_Camera.ds`, but a
normal H20.5 session loaded MoonRay DCC files from:

```text
/Applications/MoonRay/installs/openmoonray/plugin/houdini
```

The installed tree initially did not contain `moonray_Camera.ds`, so the normal
Camera LOP UI had no Moonray folder. The existing DCC install script synced the
source file into the active package path:

```sh
cmake -P /Applications/MoonRay/build/moonray/moonray_dcc_plugins/houdini/cmake_install.cmake
```

After that sync, the installed DS matched source byte-for-byte and a fresh
H20.5 Camera LOP showed the Moonray folder. Existing Houdini sessions may need
a restart or a fresh node because parameter templates can be cached at node
creation time.

## Commit And Parent Pin Discipline

For submodule work:

1. Commit and push the submodule first.
2. Validate the submodule branch and commit hash.
3. Commit the parent submodule pin only after the submodule commit is pushed.
4. Do not update unrelated submodule pins.

Good checkpoint examples:

- `moonray_dcc_plugins` SpotLight UI commit `9bf4945815b3e6c4a39a62702ef89be0453ecc28`.
- hdMoonray SpotLight backend/lifecycle commits ending in `f5331a8`.
- parent pin checkpoint `be43cbe6a77ec584ecea005101cdd78b0cd1e663`.

## Stable Versus WIP Areas

Use `hdMoonray_stable_vs_wip_matrix.md` before choosing an example to copy.

Stable/proven patterns:

- Material builder / native material subnet.
- DomeLight / EnvLight alias handling.
- Explicit native SpotLight override.
- RectLight ShapingAPI to native spread.
- Geometry/subdivision/tessellation exposure.
- Native MoonRay camera controls for `mb_shutter_bias` and bokeh parameters.

Partial/WIP patterns:

- Render Settings LOP.
- Beauty buffer support.
- Unit scale policy.

Render Settings and AOV caveats:

- The current custom Render Settings LOP default is `aov_beauty = 0`: no authored Beauty RenderVar, `RenderProduct` with `productName`, `productType = raster`, and empty `orderedVars`.
- The `aov_beauty` parm name is preserved for compatibility, but the UI label is `Experimental Beauty RenderVar / AOV Path` under Advanced / Debug.
- Generic Houdini Render Settings with a MoonRay folder is UI integration evidence only. If flattened USDA has empty `rel products` and no RenderProduct/productName/productType, it is not a complete MoonRay output setup.
- Do not claim AOV support from authored RenderVars, metadata, EXR channels, RDLA RenderOutput declarations, or debug renderer success alone.

Failure/process contrast:

- Non-beauty AOVs and `cameraDepth` transport.

## What Future Work Should Avoid

Avoid these patterns:

- Inventing renderer names, AOV names, or scene classes before checking MoonRay metadata/source.
- Default-authoring renderer-specific overrides for standard Solaris lights.
- Exposing Houdini controls before backend/RDLA/render proof exists.
- Trusting source-path HOM UI validation without also testing the installed package path used by artists.
- Using a hardcoded allowlist where installed MoonRay SceneClass metadata can be queried.
- Rewriting broad renderer transport code from a single failing AOV.
- Treating debug renderer success as production renderer success.
- Mixing H20.5 and H21 validation evidence.
- Leaving temporary diagnostics in installed runtime binaries.
