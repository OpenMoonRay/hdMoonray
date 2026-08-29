# Houdini 20.5 Adapter Runtime Fixture

This fixture validates only the `hdMoonrayAdapters` runtime translation path.
It does not validate AOVs, ROPs, resolution handling, geometry settings, or
general Solaris UI authoring.

Fixture:

```text
/Applications/MoonRay/openmoonray/moonray/hydra/hdMoonray/testSuite/light/adapter_runtime/moonray_h20_adapter_fixture.usda
```

## Runtime Paths Under Test

### MoonrayMeshLight

USD input:

```usda
def MoonrayMeshLight "meshLight"
{
    uniform token moonray:class = "MeshLight"
    rel inputs:geometry = </World/emitterMesh>
}
```

Expected path:

```text
USD MoonrayMeshLight prim
-> MoonrayMeshLightAdapter::Populate()
-> UsdImagingIndexProxy::InsertSprim("geometryLight", ...)
-> hdMoonray RenderDelegate::CreateSprim("geometryLight", ...)
-> hdMoonray Light::syncParams()
-> sceneDelegate->Get(id, "inputs:geometry")
-> MoonrayMeshLightAdapter::Get() returns SdfPath </World/emitterMesh>
-> Mesh::geometryForMeshLight()
-> native MoonRay MeshLight.geometry
```

Source files:

```text
plugin/adapters/MoonrayMeshLightAdapter.cc
lib/hydramoonray/RenderDelegate.cc
lib/hydramoonray/Light.cc
lib/hydramoonray/Mesh.cc
```

### MoonrayLightFilter projector relationship

USD input:

```usda
def MoonrayLightFilter "cookie"
{
    uniform token moonray:class = "CookieLightFilter"
    rel moonray:projector = </World/cookieProjector>
}
```

Expected path:

```text
USD MoonrayLightFilter prim
-> MoonrayLightFilterAdapter registration for primTypeName "MoonrayLightFilter"
-> Hydra lightFilter sprim
-> hdMoonray RenderDelegate::CreateSprim(lightFilter, ...)
-> hdMoonray LightFilter::syncProjector()
-> sceneDelegate->Get(id, "moonray:projector")
-> MoonrayLightFilterAdapter::Get() returns SdfPath </World/cookieProjector>
-> Camera::createCamera()
-> native MoonRay CookieLightFilter.projector
```

Source files:

```text
plugin/adapters/MoonrayLightFilterAdapter.cc
lib/hydramoonray/RenderDelegate.cc
lib/hydramoonray/LightFilter.cc
lib/hydramoonray/Camera.cc
```

### MoonrayLightFilter combine relationship

USD input:

```usda
def MoonrayLightFilter "combine"
{
    uniform token moonray:class = "CombineLightFilter"
    rel moonray:light_filters = [</World/cookie>, </World/rod>]
}
```

Expected path:

```text
USD MoonrayLightFilter prim
-> MoonrayLightFilterAdapter registration for primTypeName "MoonrayLightFilter"
-> Hydra lightFilter sprim
-> hdMoonray LightFilter::syncCombineFilters()
-> sceneDelegate->Get(id, "moonray:light_filters")
-> MoonrayLightFilterAdapter::Get() returns SdfPathVector
-> LightFilter::getFilter() for each target
-> native MoonRay CombineLightFilter.light_filters
```

Source files:

```text
plugin/adapters/MoonrayLightFilterAdapter.cc
lib/hydramoonray/LightFilter.cc
```

### Light to filter-set relationship

USD input:

```usda
def DiskLight "keyLight"
{
    rel light:filters = [</World/combine>]
}
```

Expected path:

```text
USD light:filters relationship
-> Hydra HdTokens->filters
-> hdMoonray Light::syncFilterList()
-> LightFilter::getFilter()
-> native MoonRay Light.light_filters
-> RenderDelegate category assignment
-> native MoonRay LightFilterSet creation when linked geometry assignment is evaluated
```

Source files:

```text
lib/hydramoonray/Light.cc
lib/hydramoonray/LightFilter.cc
lib/hydramoonray/RenderDelegate.cc
```

## Preflight Plugin Discovery

Run before `husk`:

```zsh
source /Applications/MoonRay/openmoonray/scripts/macOS/setupHoudini.sh
PYTHONPATH=/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources/houdini/python3.11libs:$PYTHONPATH \
  /Applications/Houdini/Houdini20.5.684/Frameworks/Python.framework/Versions/3.11/bin/python3.11 - <<'PY'
import os
from pxr import Plug
reg = Plug.Registry()
print(reg.RegisterPlugins(os.environ["PXR_PLUGINPATH_NAME"]))
plugin = reg.GetPluginWithName("hdMoonrayAdapters")
print(plugin.name if plugin else None)
print(plugin.path if plugin else None)
print(plugin.resourcePath if plugin else None)
PY
```

Pass criteria:

```text
name=hdMoonrayAdapters
path=/Applications/MoonRay/installs/openmoonray/plugin/hdMoonrayAdapters.dylib
resourcePath=/Applications/MoonRay/installs/openmoonray/plugin/pxr/hdMoonrayAdapters/
```

## Husk Command

```zsh
source /Applications/MoonRay/openmoonray/scripts/macOS/setupHoudini.sh

export HDM_LOG_FILE=/tmp/moonray_h20_adapter_fixture.hdm.log
export HDMOONRAY_RDLA_OUTPUT=/tmp/moonray_h20_adapter_fixture.rdla

/Applications/Houdini/Houdini20.5.684/Frameworks/Houdini.framework/Versions/Current/Resources/bin/husk \
  -V4 \
  -R HdMoonrayRendererPlugin \
  -f 1 \
  /Applications/MoonRay/openmoonray/moonray/hydra/hdMoonray/testSuite/light/adapter_runtime/moonray_h20_adapter_fixture.usda
```

## Inspection Points

`HDM_LOG_FILE` should contain sync entries for the adapter-provided sprims:

```text
SyncStart Mesh /World/emitterMesh
SyncStart Light /World/meshLight
SyncStart LightFilter /World/cookie
SyncStart LightFilter /World/rod
SyncStart LightFilter /World/combine
SyncStart Light /World/keyLight
```

The RDL dump should contain native MoonRay objects and relationships:

```zsh
grep -nE 'MeshLight|CookieLightFilter|RodLightFilter|CombineLightFilter|LightFilterSet|geometry|projector|light_filters' \
  /tmp/moonray_h20_adapter_fixture.rdla
```

Expected evidence:

- `MeshLight` object for `/World/meshLight`.
- `MeshLight.geometry` points at the RDL geometry generated from
  `/World/emitterMesh`.
- `CookieLightFilter` object for `/World/cookie`.
- `CookieLightFilter.projector` points at the camera generated from
  `/World/cookieProjector`.
- `CombineLightFilter` object for `/World/combine`.
- `CombineLightFilter.light_filters` includes the cookie and rod filters.
- `DiskLight`/`SpotLight` for `/World/keyLight` references the combined filter
  through native MoonRay light-filter data.

## Pass Criteria

Pass:

- `husk` can load `HdMoonrayRendererPlugin` and `hdMoonrayAdapters`.
- No unsupported prim-type warning for `MoonrayMeshLight`.
- No unsupported prim-type warning for `MoonrayLightFilter`.
- `HDM_LOG_FILE` shows `Light` sync for `/World/meshLight`.
- `HDM_LOG_FILE` shows `LightFilter` sync for `/World/cookie`,
  `/World/rod`, and `/World/combine`.
- RDL output contains `MeshLight`, `CookieLightFilter`, `RodLightFilter`, and
  `CombineLightFilter`.
- RDL output proves the `geometry`, `projector`, and `light_filters`
  relationships were populated with scene objects rather than left empty.

Fail:

- `MoonrayMeshLight` does not populate as a `geometryLight` sprim.
- `MoonrayLightFilter` does not populate as a `lightFilter` sprim.
- `Light::syncParams()` logs missing geometry for `/World/meshLight`.
- `LightFilter::syncProjector()` logs `moonray:projector: must be a path` or
  `not found`.
- `LightFilter::syncCombineFilters()` logs `moonray:light_filters: must be a
  list of paths` or target filters not found.
- RDL output lacks the native MoonRay objects or relationship attributes.
