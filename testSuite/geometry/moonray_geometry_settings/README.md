# MoonRay Geometry Settings Fixture

This fixture validates the Houdini/Solaris `primvars:moonray:*` geometry
settings path for live hdMoonray Hydra mesh rendering.

The expected bridge is:

`USD Mesh primvars:moonray:*` -> `Hydra primvars` -> `GeometryMixin`
attribute overrides -> `RdlMeshGeometry` attributes -> RDL/render output.

Run after sourcing the Houdini and MoonRay environments:

```zsh
export HDMOONRAY_RDLA_OUTPUT=/tmp/moonray_geometry_settings.rdla
husk -V4 -R HdMoonrayRendererPlugin -f 1 geometry_settings.usda
grep -n "adaptive_error\\|mesh_resolution\\|is_subd\\|subd_scheme\\|subd_boundary\\|subd_fvar_linear\\|smooth_normal\\|motion_blur_type\\|label\\|shadow_receiver_label\\|side_type\\|reverse_normals" /tmp/moonray_geometry_settings.rdla
```

Expected RDL class:

- `RdlMeshGeometry`

Expected user-authored attributes on `/World/subd_quad`:

- `subd_scheme = "bilinear"`
- `subd_boundary = "edge only"`
- `subd_fvar_linear = "all"`
- `mesh_resolution = 6`
- `adaptive_error = 0.5`
- `smooth_normal = false`
- `motion_blur_type = "static"`
- `label = "geometry_settings_subd"`
- `shadow_receiver_label = "geometry_settings_shadow"`
- `side_type = "force two-sided"`
- `reverse_normals = false`

Expected user-authored attribute on `/World/forced_polygon_quad`:

- `is_subd = false`

Crease/corner arrays and topology arrays are intentionally not represented as
user-facing renderer settings here. hdMoonray receives them from USD mesh
topology/subdivision tags when present.

`motion_blur_type` is validated here as a correctly authored and consumed
RDL geometry attribute. Its visible render behavior also requires appropriate
velocity/acceleration or multi-sample position data plus enabled render motion
blur settings, so that behavior belongs in a separate motion blur fixture.
