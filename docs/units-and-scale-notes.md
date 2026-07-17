# hdMoonRay Units and Scene Scale Notes

This note records the current Houdini 20.5 / Solaris / USD / hdMoonRay /
MoonRay unit policy after the 2026-06-21 scene-scale pass.

The current policy is intentionally narrow:

- USD stage units flow to MoonRay through `SceneVariables.scene_scale`.
- Raw geometric, light, camera, material, displacement, and volume attributes
  remain in authored scene units.
- hdMoonRay does not pre-multiply individual radius, width, height, material
  distance, displacement height, or camera distance attributes by
  `metersPerUnit`.

## Implemented Contract

Authority order:

1. An explicit authored `moonray:sceneVariable:scene_scale` opinion wins.
2. Otherwise hdMoonRay uses `UsdGeomGetStageMetersPerUnit(stage)`.
3. Otherwise MoonRay's internal `SceneVariables.scene_scale` default is `1.0`.

The MoonRay core default was changed in:

- `/Applications/MoonRay/source/openmoonray/moonray/scene_rdl2/lib/scene/rdl2/SceneVariables.cc`

Installed generated coredata now matches:

- `/Applications/MoonRay/installs/openmoonray/coredata/SceneVariables.json`

The installed coredata value is:

```text
SceneVariables.scene_scale default = 1.0
comment = "(in meters): one unit in world space = 'scene scale' meters"
```

The Houdini `.ds` defaults were also updated so generic render settings UI does
not offer the old `0.01` default:

- `houdini/soho/parameters/HdMoonrayRendererPlugin_Global.ds`
- `houdini/soho/parameters/moonray_Global.ds`

Both source and installed copies now use:

```text
default { "1" }
```

## USD Stage Units

USD `metersPerUnit` is stage-level metadata. The expected source of truth is
`UsdGeomGetStageMetersPerUnit(stage)`.

Observed expected values:

| Stage metadata | `UsdGeomGetStageMetersPerUnit(stage)` |
|---|---:|
| authored `metersPerUnit = 1.0` | `1.0` |
| authored `metersPerUnit = 0.01` | `0.01` |
| no authored `metersPerUnit` | `0.01` USD fallback |

## Validation Evidence

Preserved validation folder:

```text
/tmp/moonray_scene_scale_default_radius_20260621_213325
```

Installed `libscene_rdl2.dylib` UUID after install:

```text
DB6CB901-D394-3DC6-8EE6-7686A33777D1
```

Scene scale validation:

| Fixture | `hd_usd2rdl` RDLA | `hd_render -translate-only` RDLA | `husk` RDLA |
|---|---:|---:|---:|
| `metersPerUnit = 1.0` | omitted, default `1.0` | omitted, default `1.0` | omitted, default `1.0` |
| `metersPerUnit = 0.01` | `0.00999999978` | `0.00999999978` | `0.00999999978` |
| no authored `metersPerUnit` | `0.00999999978` | `0.00999999978` | `0.00999999978` |
| explicit `scene_scale = 0.5` | `0.5` | `0.5` | `0.5` |

The `husk` fixtures were tiny 16x16 scenes and exited with `rc=0`.

Viewport/IPR was not GUI-tested in this pass. If IPR diverges while RDLA from
`hd_usd2rdl`, `hd_render`, and tiny `husk` agrees, treat it as a viewport/runtime
sync issue, not as evidence to pre-scale individual attributes.

## Radius / Size Audit

Installed coredata semantics:

| Scene class | Attribute | Default | Comment |
|---|---:|---:|---|
| `SphereLight` | `radius` | `1.0` | Radius of the sphere. |
| `DiskLight` | `radius` | `1.0` | The radius of the DiskLight. |
| `RectLight` | `width` | `1.0` | Full size in local X. |
| `RectLight` | `height` | `1.0` | Full size in local Y. |
| `SpotLight` | `lens_radius` | `1.0` | Radius of the SpotLight lens. |
| area lights / SpotLight | `apply_scene_scale` | `true` | Whether to apply scene scale variable when normalized. |

RDLA audit result across `metersPerUnit = 1.0`, `0.01`, and unauthored USD
fallback:

| Authored case | Final RDL value |
|---|---|
| `SphereLight.radius = 1` | default-elided, installed default `1` |
| `SphereLight.radius = 0.5` | `radius = 0.5` |
| `DiskLight.radius = 1` | default-elided, installed default `1` |
| `DiskLight.radius = 0.5` | `radius = 0.5` |
| `RectLight.width = 2`, `height = 2` | `width = 2`, `height = 2` |
| `RectLight.width = 1`, `height = 1` | default-elided, installed defaults `1` |
| native `SpotLight.lens_radius = 1` | default-elided, installed default `1` |
| native `SpotLight.lens_radius = 0.5` | `lens_radius = 0.5` |

Verdict:

- No radius/diameter mismatch was proven in `hd_usd2rdl` or
  `hd_render -translate-only`.
- No full-width/half-width mismatch was proven for `RectLight`.
- Raw radii, widths, heights, and lens radii remain raw scene-unit values.
- `apply_scene_scale` remains MoonRay-side behavior tied to
  `SceneVariables.scene_scale`; hdMoonRay does not pre-apply it.

## Material Distance Audit

A known-good Dwa material fixture was translated after the scene-scale default
change.

RDLA kept material distances raw:

```text
["clearcoat_thickness"] = bind(undef(), 0.25)
["scattering_radius"] = bind(undef(), 0.5)
["transmission"] = bind(undef(), 0.5)
```

This confirms the current bridge does not multiply material distance-like
attributes by `metersPerUnit`.

## Decision

Do not add per-light or per-attribute scale fixes based only on visual size
perception. The global bridge is:

```text
USD metersPerUnit -> MoonRay SceneVariables.scene_scale
```

Only patch a specific attribute later if a fixture proves that its authored USD
semantic and MoonRay coredata semantic disagree.

## Remaining Work

- GUI viewport/IPR scene-scale behavior still needs a small manual validation
  pass.
- If native SpotLight lens radius still appears visually off in IPR, compare IPR
  runtime RDL/state against the validated tiny RDLA path before changing radius
  mapping.
- Do not mix this with OCIO, RenderProduct/RenderVar, AOV, displacement crash,
  or EXR artifact work.
