# hdMoonray Validation Workflow

This workflow records the validation levels that actually caught or proved recent hdMoonray changes. Use it before promoting a feature from WIP to stable.

## Validation Layers

| Layer | What it proves | Tools / evidence | Required for |
|---|---|---|---|
| Source audit | The change maps to real code paths and native concepts. | `rg`, `git show`, local metadata, MoonRay/OpenUSD/SideFX docs | All changes |
| USD authoring | Houdini/Solaris authored the intended prims, attrs, rels, or custom properties. | `usdcat`, `usdview`, hython/HOM, exported USDA | UI, RenderSettings, lights, geometry |
| Hydra/hdMoonray translation | hdMoonray receives and translates the intended values. | source tracing, logs, debug fixtures, `hd_usd2rdl` if applicable | Backend changes |
| RDLA/RDL proof | MoonRay scene objects and attributes exist with expected values. | debug RDLA/RDL output, `rdlOutput`, renderer logs | Lights, geometry, render settings, AOV declarations |
| Render proof | The feature affects an actual render correctly. | husk, USD Render ROP, image output | Lights, materials, geometry, render settings |
| EXR/image stats | Pixels are filled and plausible. | `oiiotool --stats`, channel inspection | AOVs, render buffer work |
| Houdini UI proof | The installed UI behaves correctly without unlocking or hidden stale state. | H20.5 fresh session, HOM, screenshots where useful | DS/HDA/shelf work |
| Runtime provenance | The installed runtime loaded the rebuilt code. | shasum, `strings`, `otool`, branch/hash checks | Compiled code, diagnostics |

## Minimum Source Checks

Run these in the relevant repo before and after work:

```sh
git status --short
git rev-parse --abbrev-ref HEAD
git rev-parse HEAD
git diff --stat
git diff
```

For submodules, also check the parent:

```sh
git -C /Applications/MoonRay/source/openmoonray status --short
git -C /Applications/MoonRay/source/openmoonray diff --submodule
```

## Runtime Provenance Checks

Use these whenever compiled code or installed Houdini plugins are involved:

```sh
pwd -P
cmake --build /Applications/MoonRay/build --target <target> --config Release
shasum <build-artifact>
shasum <installed-artifact>
strings <installed-artifact> | rg "<temporary diagnostic marker>"
```

The AOV audit showed that stale installed dylibs can invalidate an otherwise careful investigation. Remove diagnostic strings and prove they are gone before final reporting.

For narrow hdMoonray testing, prefer rebuilding only the affected plugin targets
instead of running the full OpenMoonRay install target when unrelated MoonRay
core build issues would obscure the feature being validated:

```sh
cd /Applications/MoonRay/build
cmake --build . --target hydramoonray --config Release -j8
cmake --build . --target hd_moonray --config Release -j8
cmake --build . --target hdMoonrayAdapters --config Release -j8
```

Houdini loads hdMoonray from the installed package under
`/Applications/MoonRay/installs/openmoonray`, not directly from the build tree.
Until the install workflow is made cleaner, manually copy the rebuilt dylibs
when validating this narrow path:

```sh
cp /Applications/MoonRay/build/moonray/hydra/hdMoonray/lib/hydramoonray/Release/libhydramoonray.dylib \
   /Applications/MoonRay/installs/openmoonray/lib/libhydramoonray.dylib

cp /Applications/MoonRay/build/moonray/hydra/hdMoonray/plugin/hd_moonray/Release/hd_moonray.dylib \
   /Applications/MoonRay/installs/openmoonray/plugin/hd_moonray.dylib

cp /Applications/MoonRay/build/moonray/hydra/hdMoonray/plugin/adapters/Release/hdMoonrayAdapters.dylib \
   /Applications/MoonRay/installs/openmoonray/plugin/hdMoonrayAdapters.dylib
```

This manual copy step is temporary testing workflow, not the desired long-term
install solution.

For Houdini DCC/DS work, also prove the installed package path. Source-path HOM
validation can find a new DS file that artists do not actually load. In the
native Camera controls pass, a source `HOUDINI_PATH` found
`moonray_Camera.ds`, but a normal H20.5 session loaded:

```text
/Applications/MoonRay/installs/openmoonray/plugin/houdini
```

and initially had no `moonray_Camera.ds`. The existing install step:

```sh
cmake -P /Applications/MoonRay/build/moonray/moonray_dcc_plugins/houdini/cmake_install.cmake
```

synced the source DS into the installed package. The final UI proof came from a
fresh normal H20.5 Camera LOP, not from the source-path check alone.

## Light Validation

For light work, validate each layer:

1. Export the Solaris-authored USD.
2. Confirm standard USD attributes are present and renderer-specific overrides are absent unless explicitly enabled.
3. Render or dump RDLA.
4. Confirm the RDL light class.
5. Confirm translated attributes.
6. Confirm no missing DSO errors.
7. Confirm live updates when the UI changes class-affecting state.

Stable examples:

- Default Sphere/Point intent no longer authors `moonray:class = PointLight`.
- Explicit `moonray:class = SpotLight` validates and creates MoonRay `SpotLight`.
- Invalid `moonray:class = PointLight` warns and falls back.
- RectLight plus ShapingAPI stays `RectLight` and maps to `spread`.
- DomeLight aliases map to `EnvLight`.

Special cases:

- SpotLight orientation and class swaps require render or transform/RDLA proof.
- SphereLight/DiskLight ShapingAPI behavior should be re-audited before broad policy changes.
- RectLight ShapingAPI should not become SpotLight unless the native override is explicit.

## Geometry / Subdivision Validation

For geometry settings:

1. Check USD topology/subdivision inputs.
2. Check `primvars:moonray:*` or namespaced attrs if used.
3. Confirm backend does not override explicitly authored primvars.
4. Dump RDLA and inspect mesh attributes.
5. Render a small fixture.

Stable evidence:

- `d85ec55` adds Mesh translation and a tracked fixture under `testSuite/geometry/moonray_geometry_settings`.
- `24bac7e` improves float conversion evidence for SceneVariables/geometry settings.
- `78ca757` exposes geometry controls in Houdini DS.

Avoid:

- Global render settings for prim-specific geometry behavior.
- Defaults that stomp authored primvars.

## Material Builder Validation

For material subnet work:

1. Create the tool in a fresh H20.5 session.
2. Confirm it creates a Houdini-native editable material subnet.
3. Confirm real MoonRay VOP nodes exist.
4. Confirm surface and displacement outputs are connected.
5. Render or generate a fixture through Solaris.

Stable evidence:

- `moonray_dcc_plugins` `952eea3` adds `moonray_material_builder.py` and shelf tool support.
- hdMoonray `77a673e` includes `generate_moonray_material_builder_fixture.py`.

Avoid:

- Fake material wrappers.
- Locked HDAs when a native editable subnet pattern fits Houdini better.

## Camera Controls Validation

Native Camera controls are stable only for the first exposed MoonRay attrs:

- `moonray:mb_shutter_bias`
- `moonray:bokeh`
- `moonray:bokeh_sides`
- `moonray:bokeh_image`
- `moonray:bokeh_angle`
- `moonray:bokeh_weight_location`
- `moonray:bokeh_weight_strength`

For camera UI work:

1. Check native MoonRay camera metadata, such as
   `coredata/PerspectiveCamera.json` and `coredata/OrthographicCamera.json`.
2. Check `Camera.cc` for existing standard USD camera mapping and generic
   `moonray:*` pass-through.
3. Confirm the selected attrs are copied to `primaryCamera` if the active render
   path uses `primaryCamera`.
4. Identify the correct Camera LOP renderer DS file. With the current
   `pythonrc.py` hook, Camera LOP Moonray properties resolve to
   `soho/parameters/moonray_Camera.ds`.
5. Validate with the normal installed H20.5 package path:
   `hou.findFile("soho/parameters/moonray_Camera.ds")` should resolve under
   `/Applications/MoonRay/installs/openmoonray/plugin/houdini`.
6. Create a fresh Camera LOP after install sync and confirm the Moonray folder
   and controls are visible.
7. Export default USD and confirm no `moonray:*` camera attrs are authored.
8. Explicitly set the controls and confirm the expected custom USD attrs.
9. Dump RDLA/RDL and confirm `PerspectiveCamera("primaryCamera")` receives
   `mb_shutter_bias`, `dof`, `dof_aperture`, `dof_focus_distance`, and the
   bokeh attrs when USD DOF is enabled.

DOF dependency:

- Bokeh controls require standard USD DOF, usually `fStop` plus
  `focusDistance`, to affect `primaryCamera` and the render visibly.
- With DOF off, bokeh attrs may exist on the authored camera object but are not
  copied to `primaryCamera`; do not present them as visibly effective in that
  state.

Deferred camera work:

- `moonray:class` validation against `INTERFACE_CAMERA`.
- FisheyeCamera, SphericalCamera, DomeMaster3DCamera, and BakeCamera.
- Stereo controls.
- `pixel_sample_map`.
- Medium/material/projector/bake camera workflows.

## Render Settings Validation

Render Settings LOP is partial/WIP. Use this workflow before declaring any part stable:

1. Confirm the generation script is source of truth.
2. Regenerate the HDA.
3. Confirm the HDA loads in H20.5.
4. Confirm no manual HDA-only drift.
5. Export USDA and inspect:
   - RenderSettings prim.
   - RenderProduct prim.
   - RenderVar prims.
   - camera relationship.
   - products relationship.
   - orderedVars relationship.
   - resolution.
   - productName.
   - `moonray:sceneVariable:*` attrs.
6. Render via direct husk or USD Render ROP.
7. Confirm output path and resolution.

Do not validate viewport/IPR resolution with offline RenderSettings assumptions. Viewport/IPR framing is viewport-driven unless a supported Houdini/Hydra mechanism proves otherwise.

## AOV Validation

AOVs require the strictest validation.

An AOV is not production-working unless:

- USD RenderVar authoring is correct.
- RenderProduct `orderedVars` contains the RenderVar.
- hdMoonray creates the corresponding render buffer and MoonRay RenderOutput.
- RDLA declares the expected RenderOutput.
- Production H20.5 render writes the EXR channel/subimage.
- EXR stats prove nonzero/nonconstant plausible data.

Do not promote an AOV based only on:

- EXR metadata.
- Channel names.
- Debug renderer success.
- RDLA RenderOutput declaration.
- Local PackTiles probes.

Known WIP/failure contrast:

- `cameraDepth` debug path works.
- Production `cameraDepth/Pz` has existed but been constant zero.
- Sender-side mcrt had finite depth values.
- Transport/weight/decode semantics remain unresolved.
- The next AOV baseline should start with native mapped targets such as `depth`, `Z`, `N`, `P`, and `Ng`, not diagnostic `cameraDepth`.

## Unit / Scale Validation

Unit behavior is deferred. Do not mix it into unrelated feature work.

Known facts from `units-and-scale-notes.md`:

- Houdini Solaris defaults to `metersPerUnit = 1.0`.
- Houdini `unitlength` changes authored USD `metersPerUnit`.
- hdMoonRay currently does not read/apply `metersPerUnit`.
- `metersPerUnit` changes alone produced identical RDL values and EXR stats.
- SSS/material distance attrs are passed raw.
- Houdini `nonrayscene_scale` / Apply Scene Scale for normalized lights remains unresolved.

Future unit work must use controlled fixtures and compare Houdini/Karma expectations against hdMoonRay/MoonRay behavior.

## Reporting Template

Every feature report should include:

- Repos and branches.
- Exact commits/ranges inspected.
- Source files changed.
- Native MoonRay/RDL source or metadata consulted.
- USD/Hydra/Solaris schema or Houdini source consulted.
- Authored USD evidence.
- RDLA/RDL evidence.
- Render or EXR evidence.
- H20.5 UI evidence if applicable.
- Files intentionally untouched.
- WIP/deferred items.
- Final git statuses.

## Cleanup Checklist

Before ending a pass:

- Temporary diagnostics removed.
- Installed runtime is non-crashing.
- Diagnostic strings absent from installed binaries.
- Scratch artifacts kept outside the repo.
- Only intended files changed.
- No generated files are stale.
- Parent submodule pins untouched unless explicitly requested.
