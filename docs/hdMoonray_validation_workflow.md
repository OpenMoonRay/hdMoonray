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

