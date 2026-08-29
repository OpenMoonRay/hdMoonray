# Houdini 20.5 USD/Hydra Render Contract

This note records the target render-contract architecture for Houdini 20.5.584,
Solaris/USD, Hydra, hdMoonray, and MoonRay RDL. It is intentionally stricter than
the small smoke tests: the usable contract must hold for husk, Houdini USD Render
ROP, viewport/IPR, `hd_render`, and `hd_usd2rdl`.

## Primary Local Evidence

The contract below is based on the Houdini 20.5 installed USD/Hydra headers and
the current hdMoonray source:

- `toolkit/include/pxr/usd/usdRender/settings.h`
- `toolkit/include/pxr/usd/usdRender/settingsBase.h`
- `toolkit/include/pxr/usd/usdRender/product.h`
- `toolkit/include/pxr/usd/usdRender/var.h`
- `toolkit/include/pxr/usdImaging/usdImaging/renderSettingsAdapter.h`
- `toolkit/include/pxr/usdImaging/usdImaging/renderProductAdapter.h`
- `toolkit/include/pxr/usdImaging/usdImaging/renderVarAdapter.h`
- `toolkit/include/pxr/usdImaging/usdImaging/renderSettingsFlatteningSceneIndex.h`
- `toolkit/include/pxr/imaging/hd/renderSettings.h`
- `toolkit/include/pxr/imaging/hd/renderSettingsSchema.h`
- `lib/hydramoonray/RenderDelegate.cc`
- `lib/hydramoonray/RenderPass.cc`
- `lib/hydramoonray/RenderBuffer.cc`
- `lib/hydramoonray/Camera.cc`
- `cmd/hd_cmd/hd_render`
- `cmd/hd_cmd/hd_usd2rdl`

The key Houdini 20.5/OpenUSD facts are:

- `UsdImagingRenderProductAdapter` and `UsdImagingRenderVarAdapter` state that
  Product/Var adapter support is for the Scene Index 2.0 API only.
- For the legacy `UsdImagingDelegate` path, `UsdImagingRenderSettingsAdapter`
  handles flattening of products and vars.
- `UsdImagingRenderSettingsFlatteningSceneIndex` adds a flattened
  `HdRenderSettingsSchema` representation for downstream Hydra backends and
  forwards dependencies from settings to the targeted products and vars.
- `HdRenderSettings` is a Hydra Bprim that stores flattened render products.
  Each product carries product path, type, product name, resolution, render vars,
  camera path, pixel aspect ratio, aspect policy, aperture size, data window, and
  namespaced settings.

## Target Contract

### DCC Authored USD

The MoonRay Render Settings LOP authors USD intent:

- `/Render/rendersettings`
- `/Render/Products/renderproduct`
- `/Render/Products/Vars/*`

`UsdRenderSettings` owns:

- camera relationship
- products relationship
- resolution
- pixel aspect ratio
- optional data window and aspect policy, if exposed later
- `moonray:sceneVariable:*` attributes

`UsdRenderProduct` owns:

- `productName`
- `productType`
- `orderedVars`
- resolution matching the active RenderSettings
- pixel aspect ratio matching the active RenderSettings
- no product camera by default

`UsdRenderProduct.camera` is a product-specific override. For the default beauty
product, leaving it absent is correct when `RenderSettings.camera` is the
authority. Tests with explicit matching and intentionally different product
cameras proved that husk falls back to `RenderSettings.camera` when product
camera is absent.

`UsdRenderVar` owns:

- `sourceName`
- `sourceType`
- `dataType`
- driver/AOV metadata
- optional MoonRay RenderOutput metadata expressed through namespaced attrs

### Houdini/USD Render ROP

The USD Render ROP owns execution:

- selected LOP branch
- selected RenderSettings prim path
- output image override
- renderer selection
- frame/range execution

The ROP `outputimage` is the execution output authority when set. The
RenderProduct `productName` remains valid USD intent and fallback. This split is
needed because raw husk without Houdini node context can resolve Houdini
variables such as `$HIPNAME` and `$OS` differently than the ROP UI.

### Hydra/Husk

Hydra/husk owns render settings flattening:

- RenderSettings, RenderProduct, and RenderVar must be consumed as render
  settings/product/var metadata, not as fake geometry/light/camera prims.
- RenderProduct and RenderVar should not be added as fake hdMoonray Bprims.
- In the scene-index path, Product/Var are represented as data sources and
  flattened into `HdRenderSettingsSchema`.
- In the legacy `UsdImagingDelegate` path, `UsdImagingRenderSettingsAdapter`
  performs the product/var flattening.

### hdMoonray

hdMoonray owns translation from Hydra state to MoonRay RDL:

- `HdRenderSettings` namespaced settings become MoonRay render settings and RDL
  `SceneVariables`.
- `renderingColorSpace` is consumed as a render setting.
- Hydra render-pass framing determines `image_width`, `image_height`, and the
  render aspect passed to the RDL camera conform path.
- Hydra camera sprim data becomes the RDL camera.
- Hydra AOV bindings become MoonRay RenderOutput/beauty bindings.

The current source maps these paths as follows:

- `RenderDelegate.cc::RenderSettingsPrim::_Sync()` calls `HdRenderSettings::_Sync`
  and forwards `renderingColorSpace` plus namespaced settings to
  `RenderDelegate::SetRenderSetting`.
- `RenderDelegate.cc::GetSupportedBprimTypes()` advertises `renderSettings`,
  `renderBuffer`, and `openvdbAsset`.
- `RenderPass.cc::Execute()` reads `HdRenderPassState::GetFraming()` or viewport
  size, writes RDL `image_width`/`image_height`, and calls
  `Camera::setAsPrimaryCamera(renderAspect)`.
- `RenderPass.cc::Execute()` iterates `HdRenderPassAovBindingVector`; each
  binding is handed to `RenderBuffer::bind()`.
- `RenderBuffer.cc` reads `sourceName`/`sourceType` from AOV binding settings
  and maps them to MoonRay beauty or RenderOutput names.

### MoonRay RDL

MoonRay RDL owns final render execution:

- `SceneVariables`
- primary camera
- layer, geometry, materials, and lights
- RenderOutput objects / beauty buffer

The RDL image dimensions are not authored directly from USD
`RenderSettings.resolution` by hdMoonray. They enter RDL through the Hydra render
pass framing supplied by the client.

## Structural Answers

1. RenderSettings is a Hydra Bprim for hdMoonray.
2. RenderProduct and RenderVar are not hdMoonray Bprims in the target contract.
3. Product/Var are flattened through SceneIndex or RenderSettingsAdapter.
4. husk uses the clean render settings path when invoked with `--settings`.
5. `hd_render` and `hd_usd2rdl` currently use legacy `UsdImagingDelegate`.
6. Houdini viewport/IPR uses Houdini's Hydra integration and render pass state.
7. hdMoonray should not advertise Product/Var Bprim support unless OpenUSD ABI
   and source prove that to be correct for this Houdini version.
8. Raw Product/Var traversal warnings in command tools should be filtered or
   avoided at the legacy traversal layer, not hidden by fake backend support.
9. Default Product.camera should stay absent; product-specific camera override is
   a future explicit UI feature if needed.
10. Settings/Product resolution and pixel aspect ratio should match when the DCC
    LOP authors both Settings and Product.
11. `dataWindowNDC` and `aspectRatioConformPolicy` should remain schema fallback
    until exposed deliberately; marker probes showed no difference from authoring
    explicit full-frame values for the current MoonRay path.
12. OrderedVars become Hydra AOV bindings through the USD/Houdini render product
    path.
13. AOV bindings become MoonRay RenderOutput/beauty bindings in
    `RenderBuffer::bind`.
14. Scene variables become RDL `SceneVariables` through namespaced render
    settings.
15. The selected USD camera becomes the Hydra camera sprim, then the RDL primary
    camera in `RenderPass`.
16. `image_width` and `image_height` enter RDL from render-pass framing.
17. RenderProduct `productName` is USD product intent; ROP `outputimage` or
    command-line `-o` is execution override.
18. Duplicate DCC authors of the same RenderSettings/Product paths are scene
    poison and should be warned about by the DCC layer.
19. The owned USD Render ROP should point to its owner branch and owner
    `render_settings_prim`.
20. husk Product/Var warnings are structural if present with `--settings`.
    Command-tool Product/Var warnings are legacy traversal noise if RDL/output
    proof shows the CLI path never consumes USD Product/Var metadata.
21. Unsupported RenderSettings warnings are structural for all paths because
    hdMoonray supports `HdRenderSettings`.

## Historical Runtime Matrix Summary

| Path | RenderSettings warning | RenderProduct warning | Contract status |
|------|------------------------|-----------------------|-----------------|
| husk clean scene with `--settings` | No | No | Clean. EXR written, 512x512 RGBA nonconstant. |
| `hd_usd2rdl` clean scene | No | Yes | Legacy traversal sees raw RenderProduct; RDLA still written. |
| `hd_render` clean scene | No | Yes | Legacy traversal sees raw RenderProduct; EXR still written. |
| real-scene husk probe | No before interrupt | No before interrupt | Started rendering; no Product/Settings warning before manual timeout. |
| real-scene `hd_usd2rdl` | No | Yes | Legacy traversal warning plus unrelated light compatibility warnings. |

After the 2026-06-16 command-tool legacy traversal patch, clean `hd_render`,
clean `hd_usd2rdl`, and real-scene `hd_usd2rdl` no longer log unsupported
RenderProduct/RenderVar warnings. husk remained warning-free on the same clean
scene.

## 2026-06-20 Shipping Pass Contract Update

The final local shipping pass implemented the target contract for the validated
headless paths. The important architectural point remains unchanged:
`RenderSettings` is supported as a real Hydra Bprim, while `RenderProduct` and
`RenderVar` are consumed through flattened render-settings/product/var metadata
or through client AOV bindings. They are not implemented as fake no-op Bprims.

### Updated Path Matrix

| Path | RenderSettings/Product/Var warning state | Contract proof |
|------|------------------------------------------|----------------|
| `husk -R HdMoonrayRendererPlugin --settings /Render/rendersettings` | Clean for base, square, and wide probes | EXRs written at `96x72`, `96x96`, and `160x90`; RGBA float; nonconstant; no shutdown socket noise |
| `hd_usd2rdl -settings /Render/rendersettings` | Clean after legacy traversal filtering | RDLA contains `SceneVariables`, `image_width = 96`, `image_height = 72`, and `PerspectiveCamera("primaryCamera")` |
| `hd_render -settings /Render/rendersettings` | Clean after legacy traversal filtering | EXR written at `96x72`; RGBA float; nonconstant |
| Houdini USD Render ROP | DCC contract validated headlessly; fresh GUI render remains manual | Owned ROP points at owner branch, owner RenderSettings path, MoonRay renderer, and owner `product_name` output authority |
| Solaris viewport/IPR | Manual fresh GUI confirmation still required | Runtime support paths and installed binaries match the validated headless contract; use `HDMR_PRIMTYPE_DIAG=1` if the old unsupported-prim warning returns |

### Command Tool Alignment

`hd_render` and `hd_usd2rdl` now take the same core inputs as the main render
contract:

- `-settings` selects the USD RenderSettings prim.
- Camera selection resolves explicit command-line camera first, then first
  RenderProduct camera, then RenderSettings camera, then the legacy free camera.
- Resolution resolves explicit command-line `-size` first, then first
  RenderProduct resolution, then RenderSettings resolution, then the legacy
  default.
- Raw Product/Var prims are excluded from legacy `UsdImagingDelegate`
  population, avoiding unsupported traversal warnings without pretending that
  Product/Var are hdMoonray Bprims.

### RDL Realization

The validated RDLA path proves that:

- flattened/settings-derived dimensions reach RDL `SceneVariables` as
  `image_width` and `image_height`;
- the selected USD camera reaches the RDL `primaryCamera`;
- command tools and husk now agree on the same camera/resolution contract for
  clean probes.

### Metadata And GUI Boundaries

H20.5 husk still writes suspicious memory metadata outside the renderer metadata
map, even with MoonRay memory metadata/workarounds removed. This is tracked as a
husk writer issue, not as RenderSettings/Product/Var contract failure.

`setupHoudini.sh` now mirrors a Houdini package-authored OCIO default into the
shell environment when `OCIO` is otherwise unset. Direct `husk`, `hd_render`,
and `hd_usd2rdl` probes sourced through the Houdini/MoonRay setup no longer emit
`Color management disabled` or missing `scene_linear` / `Linear Rec.709`
warnings with the local PixelManager config. Fresh graphical Solaris/IPR
validation is still needed for final visual framing and display/color matching.
Headless validation proves the USD/Hydra/hdMoonray contract path and
command-tool parity, not every GUI viewport presentation path.

## Rejected Approaches

- Do not add no-op Product/Var Bprims to quiet logs.
- Do not hide warnings by setting `aovsupport=false`.
- Do not treat Product.camera as required for the default product.
- Do not hardcode project cameras, resolution, or aspect ratios.
- Do not make USD Render ROP output path the only product contract; ProductName
  remains valid USD fallback.
