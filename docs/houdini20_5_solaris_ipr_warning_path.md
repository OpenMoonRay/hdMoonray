# Houdini 20.5 Solaris/IPR Unsupported Prim Warning Path

This note tracks the earlier Houdini Solaris viewport/IPR unsupported-prim
warning. The current validated headless paths are clean; a fresh graphical
Houdini session is still the boundary for claiming the GUI warning gone.

Observed user warning:

```text
Selected hydra renderer doesn't support prim type 'RenderSettings'
Selected hydra renderer doesn't support prim type 'RenderProduct'
```

## Why Command-Tool Success Is Insufficient

`hd_render` and `hd_usd2rdl` use legacy `UsdImagingDelegate` and build an
application-owned render task, render buffer, camera, and AOV binding from
command-line arguments. The command tools are useful comparison paths, but they
do not prove Solaris viewport/IPR behavior.

The command tools were previously cleaned to avoid raw Product/Var legacy
population warnings. That does not prove:

- Solaris/IPR uses the same traversal path.
- Solaris/IPR sends the same prim type tokens.
- Solaris/IPR has the same SceneIndex flattening behavior.
- Solaris/IPR warning happens inside hdMoonray.

## Diagnostic Required

Use:

```sh
export HDMR_PRIMTYPE_DIAG=1
```

Instrumentation must answer:

- Does hdMoonray see `RenderSettings`?
- Does hdMoonray see `renderSettings`?
- Does hdMoonray see `RenderProduct`?
- Does hdMoonray see `renderProduct`?
- Does hdMoonray see `RenderVar`?
- Does hdMoonray see `renderVar`?
- Does `CreateBprim()` get called for `RenderSettings` or `renderSettings`?
- Is the warning caused by a raw USD type token vs Hydra token mismatch?
- Is Product warning upstream adapter lookup noise before hdMoonray can act?
- Is Solaris/IPR using legacy delegate, SceneIndex, or a hybrid bridge?

## Expected Env-Gated Log Fields

Each diagnostic line should include:

- function name
- input type token
- canonical type token
- support result
- prim path if available
- equality flags for:
  - `RenderSettings`
  - `renderSettings`
  - `RenderProduct`
  - `renderProduct`
  - `RenderVar`
  - `renderVar`

## Patch Policy

Potential fixes are gated by diagnostic proof:

- If raw USD `RenderSettings` reaches hdMoonray Bprim support and local
  Houdini/OpenUSD behavior expects that, add a narrow canonical Bprim alias from
  `RenderSettings` to `HdPrimTypeTokens->renderSettings`.
- If the warning happens upstream before hdMoonray sees the token, delegate
  support changes cannot fix it. The next patch target is the Houdini/Solaris
  adapter/SceneIndex/filter path, if locally reachable.
- If raw `RenderProduct` reaches Solaris/IPR legacy traversal, do not add fake
  Product Bprim support. Product/Var must be flattened or deliberately filtered
  in the correct traversal layer.
- If Product/Var are expected to flatten but do not, fix the flattening path or
  authored relationship, not backend fake support.

## Manual User Confirmation Required

After rebuild/install/sign, the user should run a fresh Houdini 20.5 session
from the updated setup and reproduce the Solaris/IPR path. If the warning
returns, enable `HDMR_PRIMTYPE_DIAG=1` before launch. The pass cannot claim the
GUI/IPR warning fixed until the refreshed GUI path is either clean or those logs
prove the source.

## 2026-06-16 Headless Husk Diagnostic Result

A controlled `husk -R HdMoonrayRendererPlugin --settings /Render/rendersettings`
probe with `HDMR_PRIMTYPE_DIAG=1` did not reproduce the Solaris unsupported
prim warnings.

Observed in the headless husk path:

- `GetSupportedBprimTypes()` includes `renderSettings`.
- `/Render/rendersettings` reaches hdMoonray as `input=renderSettings`.
- `canonicalBprimType` leaves it as `renderSettings`.
- `CreateBprim path=/Render/rendersettings input=renderSettings
  canonical=renderSettings supported=1`.
- No raw `RenderSettings`, `RenderProduct`, or `RenderVar` token reached the
  hdMoonray create path in this probe.
- No `Selected hydra renderer doesn't support prim type 'RenderSettings'` or
  `RenderProduct` warning appeared in this probe.

Current interpretation:

- The batch `husk --settings` path is not the failing warning path.
- A blind `RenderSettings -> renderSettings` alias is not evidence-backed by
  this headless result.
- A fake Product/Var Bprim remains explicitly rejected.
- The remaining warning is likely specific to the Solaris viewport/IPR path, a
  different Houdini SceneIndex/legacy traversal bridge, or an upstream adapter
  lookup before hdMoonray receives a create/support query.

Next proof required:

- Run a fresh Houdini/Solaris/IPR session with `HDMR_PRIMTYPE_DIAG=1`.
- If raw `RenderSettings` reaches hdMoonray, patch that exact path.
- If the warning appears without any matching `HDMR_PRIMTYPE_DIAG` raw
  Product/Settings create/support line, treat the source as upstream of
  hdMoonray delegate creation and locate the Houdini/Solaris adapter path.

## 2026-06-20 Headless Shipping Result

The final headless validation after the Step 3 architecture cleanup remains
clean:

- `husk -R HdMoonrayRendererPlugin --settings /Render/rendersettings` writes a
  nonconstant EXR and exits cleanly for the clean probe.
- `hd_usd2rdl -settings /Render/rendersettings` writes RDLA without unsupported
  RenderSettings/Product/Var warnings.
- `hd_render -settings /Render/rendersettings` writes a nonconstant EXR without
  unsupported RenderSettings/Product/Var warnings.
- `RenderProduct` and `RenderVar` are not implemented as fake/no-op Bprims.

If a refreshed GUI/IPR session is also clean, this note can be reduced to a
historical warning-path record. If the warning returns, the next action is a
diagnostic GUI launch, not adding Product/Var placeholder support.
