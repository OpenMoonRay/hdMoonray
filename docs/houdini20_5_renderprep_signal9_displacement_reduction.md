# Houdini 20.5 RenderPrep Signal 9 Displacement Reduction

Date: 2026-06-20

This note records the reduction and fix for the real sculpt-practice scene where
`mcrt` exited during renderPrep when MoonRay displacement was enabled. It is
intended to preserve the local evidence so this does not get reinterpreted as a
generic displacement, subdivision, texture, light, or macOS memory problem.

## Scope

Primary scene:

```text
/Users/j7s/houdini-projects/sculpt-practice/stage-test/sculpt-practice-init6-test.usd_rop2.usda
```

The pass intentionally did not change:

- DCC/HDA authoring.
- RenderSettings/Product/Var architecture.
- camera/framing.
- AOVs.
- OCIO/color.
- setup scripts.
- the removed `b466c68` post-RDL displacement-null workaround.

The only runtime patch made in this pass is in:

```text
lib/hydramoonray/Material.cc
```

The existing scalar-array primvar override fix in:

```text
lib/hydramoonray/Primvars.cc
```

remained in place, but it is not the signal-9 fix described here.

## Preserve Path

The dirty stack was preserved before the reduction under:

```text
/tmp/moonray_renderprep_signal9_20260620_105127
```

The folder contains binary diffs and status for the parent, hdMoonray, DCC, and
SDR repositories.

The final hardening pass was preserved under:

```text
/tmp/moonray_final_displacement_cleanup_20260620_160145
```

## Original Failure

The unpatched real scene failed during renderPrep:

```text
Error: Arras session stopped, compExited: mcrt exited due to signal 9
```

Representative log:

```text
/Users/j7s/houdini-projects/sculpt-practice/render-test/renderprep_signal9_original_20260620_105212.husk.log
```

The failure happened after `stage renderPrep start` and before `stage shading
start`; no EXR was written. Husk stayed alive until the timeout wrapper killed
it.

Process and macOS unified-log sampling did not show a clear macOS jetsam or
memory-pressure kill of `mcrt`. The pre-patch process RSS was moderate for the
machine and did not explain the immediate renderPrep death.

## Important Scene Facts

The real scene contains a valid sculpt displacement material:

```text
/materials/moonraymaterial.outputs:moonray:displacement
    -> /materials/moonraymaterial/normal_displacement.outputs:out

/materials/moonraymaterial/normal_displacement
    inputs:height = 0
    inputs:height.connect = /materials/moonraymaterial/ImageMap6.outputs:out
    inputs:height_multiplier = 0.01
    inputs:zero_value = 1

/materials/moonraymaterial/ImageMap6.inputs:texture
    ../textures/1_tx/displacement.<UDIM>.tx
```

The scene also contains two no-op displacement terminals:

```text
/materials/moonraymaterial1.outputs:moonray:displacement
/materials/moonraymaterial2.outputs:moonray:displacement
```

Those networks are `NormalDisplacement` terminals with:

- no height input connection,
- no height multiplier input connection,
- scalar `height = 0`,
- scalar `height_multiplier = 0`.

Global displacement is enabled:

```text
/Render/rendersettings.moonray:sceneVariable:enable_displacement = true
```

The sculpt mesh authors MoonRay subdivision primvars:

```text
primvars:moonray:is_subd = true
primvars:moonray:mesh_resolution = 2
primvars:moonray:subd_scheme = "bilinear"
primvars:moonray:adaptive_error = 1
```

The displacement textures are present and OIIO-readable:

```text
/Users/j7s/houdini-projects/sculpt-practice/textures/1_tx/displacement.1001.tx
/Users/j7s/houdini-projects/sculpt-practice/textures/1_tx/displacement.1002.tx
/Users/j7s/houdini-projects/sculpt-practice/textures/1_tx/displacement.1003.tx
```

Each inspected TX was a 4096 x 4096 single-channel float mipmapped TIFF/TX.

## Reduction Matrix Before Patch

All overlays were temporary USD layers under `/tmp`; the real scene was not
edited.

| Case | Result | Interpretation |
| --- | --- | --- |
| original real scene | `mcrt` signal 9 during renderPrep | Failing baseline. |
| global displacement off | renderPrep passed, reached shading | Failure requires global displacement. |
| sculpt `height_multiplier = 0` | signal 9 | Real sculpt multiplier is not the sole trigger. |
| sculpt `is_subd = false` | signal 9 | Sculpt subdivision is not the sole trigger. |
| sculpt `mesh_resolution = 0` | signal 11 | Still fails during renderPrep. |
| sculpt `mesh_resolution = 1` | signal 11 | Still fails during renderPrep. |
| sculpt `mesh_resolution = 2` | signal 9 | Matches original failure class. |
| sculpt mesh inactive | signal 9 | Failure persists without the sculpt mesh. |
| spot/rect light overrides inactive | signal 9 | Light override warnings are not the trigger. |
| sculpt displacement texture empty | signal 9 | TX files are not the sole trigger. |
| no-op displacements blocked | renderPrep passed, real sculpt displacement retained | Strong isolation. |
| sculpt displacement blocked only | signal 9 | Remaining no-op displacement terminals still trigger failure. |
| all displacements blocked | renderPrep passed | Confirms displacement terminal path. |

The decisive cases are:

- blocking only the no-op displacement terminals made renderPrep pass while
  preserving the real sculpt displacement;
- blocking only the real sculpt displacement still failed because the no-op
  terminals remained.

## Root Cause Classification

The signal-9 renderPrep failure was caused by no-op `NormalDisplacement`
material terminals reaching the RDL layer as real displacement bindings while
global displacement was enabled.

It was not primarily:

- macOS memory pressure,
- corrupt or missing displacement TX files,
- sculpt subdivision alone,
- mesh resolution alone,
- light compatibility warnings,
- or the valid sculpt displacement network.

## Patch

The fix filters no-op `NormalDisplacement` terminals at the Hydra material
network level before they become RDL displacement objects or layer bindings.

Patch location:

```text
lib/hydramoonray/Material.cc
```

Predicate:

- terminal is `HdMaterialTerminalTokens->displacement`;
- the material network has exactly one terminal node;
- that terminal node is `NormalDisplacement`;
- `height` has no input connection;
- `height_multiplier` has no input connection;
- scalar `height` exists and is numerically zero;
- scalar `height_multiplier` exists and is numerically zero.

If all conditions hold, `Material::updateTerminal()` returns `nullptr` for that
displacement terminal.

The hardening pass verifies connections by checking relationships whose
`outputId`/`outputName` target the terminal input. A node is treated as the
terminal only if it is not the input/source side of any relationship. Unknown,
missing, connected, nonzero, custom, or non-`NormalDisplacement` networks are
left intact.

This is intentionally different from the removed `b466c68` workaround. The old
workaround inspected an already-created RDL displacement object and nulled a
valid layer displacement after translation. This fix rejects only an explicitly
no-op Hydra terminal before it is translated.

Valid displacement remains intact. In the real scene RDLA after the patch:

```text
Layer("defaultLayer") {
    {RdlMeshGeometry("/sopcreate1/sculpt"), "",
     DwaBaseMaterial("/materials/moonraymaterial/dwa_base"),
     LightSet(...),
     NormalDisplacement("/materials/moonraymaterial/normal_displacement"),
     ...},
}

NormalDisplacement("/materials/moonraymaterial/normal_displacement") {
    ["height"] = bind(ImageMap("/materials/moonraymaterial/ImageMap6.out"), 1),
    ["zero_value"] = 1,
    ["height_multiplier"] = bind(undef(), 0.00999999978),
}
```

The floor/glass no-op displacement bindings are absent after the patch.

## Validation After Patch

Post-patch matrix:

| Case | Result |
| --- | --- |
| original real scene | renderPrep passed, reached `stage shading start` |
| sculpt displacement blocked only | renderPrep passed, reached `stage shading start` |
| no-op displacements blocked | renderPrep passed, reached `stage shading start` |
| all displacements blocked | renderPrep passed, reached `stage shading start` |

Representative logs:

```text
/Users/j7s/houdini-projects/sculpt-practice/render-test/renderprep_signal9_original_20260620_160457.husk.log
/Users/j7s/houdini-projects/sculpt-practice/render-test/renderprep_signal9_only_sculpt_displacement_blocked_20260620_160851.husk.log
/Users/j7s/houdini-projects/sculpt-practice/render-test/renderprep_signal9_noop_displacements_blocked_20260620_161059.husk.log
/Users/j7s/houdini-projects/sculpt-practice/render-test/renderprep_signal9_all_displacements_blocked_20260620_161319.husk.log
```

The logs show `Render prep time = ...` followed by `stage shading start`. The
later signal 15 in those bounded tests is from the timeout wrapper terminating
the render after renderPrep had passed.

## EXR Proof

A low-resolution temporary overlay of the real scene was rendered with global
displacement still enabled:

```text
/tmp/moonray_signal9_overlays/original_lowres_quick.usda
/tmp/renderprep_signal9_original_lowres_quick_final.exr
```

The render completed:

```text
RC=0
Render prep time = 00:01:27.301
stage shading start ...
stage shading complete ...
```

OIIO reported:

```text
256 x 256, 4 channel, float openexr
channel list: R, G, B, A
__delegate: "HdMoonrayRendererPlugin"
percentDone: 100
renderProgressAnnotation: "Complete"
Constant: No
Monochrome: No
```

This proves the real scene data can render after the no-op terminal filter,
without disabling global displacement and without removing the valid sculpt
displacement network.

## Build, Install, and Signing Proof

Built targets:

```text
hydramoonray
hd_moonray
hd_moonray_debug
```

Installed artifacts:

```text
/Applications/MoonRay/installs/openmoonray/lib/libhydramoonray.dylib
/Applications/MoonRay/installs/openmoonray/plugin/hd_moonray.dylib
/Applications/MoonRay/installs/openmoonray/plugin/hd_moonray_debug.dylib
```

Build/install UUIDs:

```text
libhydramoonray:       3E5F18D9-C2D5-34A1-ACAD-62194556B1CB
hd_moonray:            01E2FDFB-06DA-340F-889C-49D75ECC19E5
hd_moonray_debug:      6A5F3276-99E8-36DA-865E-DF4CF26C694D
```

`codesign --verify --verbose=2` passed for all three installed artifacts.

`husk --list-renderers` showed:

```text
HdMoonrayRendererPlugin (Moonray)
HdMoonrayRendererDebugPlugin (Moonray (debug))
```

`git diff --check` passed in:

- parent `openmoonray`,
- `moonray/hydra/hdMoonray`,
- `moonray/moonray_dcc_plugins`,
- `moonray/hydra/moonray_sdr_plugins`.

## Sculpt Subdivision Attribute Elision

The real USD authors:

```text
primvars:moonray:is_subd = true
primvars:moonray:mesh_resolution = 2
primvars:moonray:subd_scheme = "bilinear"
primvars:moonray:adaptive_error = 1
```

The generated RDLA after this pass shows:

```text
RdlMeshGeometry("/sopcreate1/sculpt") {
    ["subd_scheme"] = "bilinear",
    ["adaptive_error"] = 1,
}
```

but it does not show:

```text
["is_subd"] = true
["mesh_resolution"] = 2
```

This is expected sparse/default RDLA output, not missing translation.

Installed `RdlMeshGeometry.json` declares:

```text
is_subd default = true
mesh_resolution default = 2.0
adaptive_error default = 0.0
subd_scheme default = catclark
```

The original sculpt values match the defaults for `is_subd` and
`mesh_resolution`, so they can be omitted by sparse RDLA. A non-default overlay
that authored:

```text
primvars:moonray:is_subd = false
primvars:moonray:mesh_resolution = 7
```

generated:

```text
RdlMeshGeometry("/sopcreate1/sculpt") {
    ["is_subd"] = false,
    ["subd_scheme"] = "bilinear",
    ["mesh_resolution"] = 7,
    ["adaptive_error"] = 1,
}
```

This proves the current `Primvars.cc` scalar-array unwrapping path is needed for
non-default constant MoonRay mesh primvar overrides and that the original
`is_subd=true` / `mesh_resolution=2` omission is default elision.

## Memory Metadata Note

The low-resolution successful EXR still contains:

```text
renderMemory_s: "418.39 GB"
```

This confirms the failed husk memory-metadata workaround should stay removed.
The bogus memory tag remains a Houdini 20.5 husk-side metadata behavior, not a
MoonRay-authored metadata value controlled by the removed workaround.

## Commit Recommendation

Do not commit the whole current dirty stack as one change.

The narrow `Material.cc` no-op displacement terminal filter is validated for the
renderPrep signal-9 failure and can be prepared as its own focused hdMoonray
commit after review.

The `Primvars.cc` scalar-array unwrapping is also validated by the non-default
subdivision overlay and should be kept as a separate focused commit if it remains
in the final stack.
