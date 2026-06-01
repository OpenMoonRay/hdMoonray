# hdMoonRay Units and Scale Notes

This note records the current Houdini/Solaris, USD, hdMoonRay, and MoonRay unit
behavior observed during the Houdini 20.5 Solaris integration work. It is a
future-work audit note only; it does not define a completed unit conversion
policy.

## Current Audit Findings

- Houdini Solaris defaults to `metersPerUnit = 1.0`.
- Changing Houdini `unitlength` changes the authored USD `metersPerUnit`.
- hdMoonRay currently does not read or apply USD `metersPerUnit`.
- Changing `metersPerUnit` alone produced identical RDL values and identical
  EXR stats in the audit fixtures.
- hdMoonRay currently passes numeric USD/Hydra values to MoonRay as raw scene
  units.
- USD `inputs:normalize` maps to MoonRay `normalized`.
- DwaBaseMaterial `scattering_radius` and similar SSS distance controls are
  passed as raw numeric scene-unit values.

## Separate Scale Concepts

### USD Stage Units

USD `metersPerUnit` is stage-level unit metadata. In the current hdMoonRay path,
it is not consumed. Changing only `metersPerUnit` does not currently alter the
MoonRay RDL values or render statistics produced by hdMoonRay.

### Houdini Light Apply Scene Scale

Houdini light UI includes a light-specific normalized scale option:

- label: `Apply Scene Scale`
- parameter: `nonrayscene_scale`
- help: `Whether to apply scene scale while normalized.`

User testing showed visible issues around normalized light behavior when this
option is enabled. This is separate from the USD `metersPerUnit` metadata audit
and is not fully mapped by hdMoonRay today.

### MoonRay Normalized Lights

MoonRay lights have a native `normalized` attribute. The installed MoonRay RDL
metadata describes this as surface-area/radiance normalization, allowing light
size to change without changing total emitted energy.

The current hdMoonRay translation preserves authored light size attributes such
as radius, width, and height, and passes transform scale through `node_xform`.
The audit render probes were useful for confirming RDL values, but were not
reliable enough for final photometric claims because fixture framing, emitter
position, and emitter/object intersection can dominate brightness comparisons.

### MoonRay Material Distance Controls

DwaBaseMaterial `scattering_radius` is a physical distance-like material
control. hdMoonRay currently passes this and similar SSS distance values raw.
Object scale changes the geometry transform in RDL; it does not change material
distance attributes. USD `metersPerUnit` also does not currently affect these
values.

## Known Limitation

hdMoonRay currently has no explicit, renderer-wide unit policy.

This means:

- USD `metersPerUnit` is not consumed.
- Houdini `nonrayscene_scale` / `Apply Scene Scale` behavior for normalized
  lights is not fully mapped.
- SSS and material distance parameters are passed raw.
- No global conversion layer exists for physical distance or size attributes.

## Decision

Do not patch this during the Render Settings or AOV foundation work.

Any unit-policy change could broadly affect:

- normalized light brightness
- area light size behavior
- SSS and material distance interpretation
- camera focus distances
- physical camera quantities
- existing scene compatibility

This should be handled as a later dedicated unit-policy task with controlled
comparison fixtures, ideally comparing Houdini/Karma expectations against
hdMoonRay/MoonRay behavior.

## Future Unit-Policy Test Matrix

A later unit-policy task should test:

- `metersPerUnit = 1.0` vs `0.01`
- Houdini `Apply Scene Scale` / `nonrayscene_scale` on and off
- USD `inputs:normalize` on and off
- object and light transform scale `1` vs `10`
- SphereLight, DiskLight, and RectLight
- SSS material distance behavior
- camera focus distance / depth-of-field distance, if relevant

