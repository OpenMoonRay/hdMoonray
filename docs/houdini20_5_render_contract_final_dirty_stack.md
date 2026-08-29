# Houdini 20.5 Final Render Contract Dirty Stack

> Historical dirty-stack ledger. This file is superseded by the pushed H20.5
> render-contract, scene-scale, and config-driven OCIO integration stack:
> parent `55e5e6b`, scene_rdl2 `b7aa377`, moonray `e403e80`,
> hdMoonray `34d2d7e`, moonray_dcc_plugins `04bd9a8`, and moonshine
> `5bec7a4`.
>
> Do not use this file as the current implementation contract. It is preserved
> as forensic history from the pre-commit audit. Current contract docs are:
> `houdini20_5_render_contract_final.md`,
> `houdini20_5_sceneindex_render_product_contract.md`,
> `houdini20_5_ocio_colormanagement.md`, and
> `hdMoonray_stable_vs_wip_matrix.md`.

Preserve snapshot:

`/tmp/moonray_render_contract_final_20260616_162142`

This ledger classified the then-current mixed working tree before final render
contract diagnostics or behavior patches. It was not a commit plan by itself.

## Workstream Key

| Key | Workstream |
|-----|------------|
| A | RenderSettings/Product/Var contract |
| B | Houdini/Solaris/IPR warning path |
| C | Camera/framing/aspect path |
| D | DCC scene-linking/ROP authority |
| E | command-tool legacy path |
| F | unrelated SDR parser metadata |
| G | unrelated PluginLightFilter aliases |
| H | unrelated render stats / memory metadata |
| I | generated HDA / pycache / accidental dirt |

## Parent Repository

| Path | Status | Workstream | Risk | Evidence / Action |
|------|--------|------------|------|-------------------|
| `moonray/hydra/hdMoonray` | modified submodule | A/B/C/E/G/H | historical: required investigation before commit | At the time of this audit, the parent pointer reflected a dirty hdMoonray submodule and was not ready to pin until split and validated. |
| `moonray/hydra/moonray_sdr_plugins` | modified submodule | F | commit separately or leave | SDR parser metadata is unrelated to this render contract pass unless shader discovery warnings return. |
| `moonray/moonray_dcc_plugins` | modified submodule | D/I | historical: required investigation before commit | At the time of this audit, DCC changes included scene-linking/ROP authority work plus HDA binary state. |

## hdMoonray

| Path | Workstream | Risk | Evidence / Action |
|------|------------|------|-------------------|
| `cmd/hd_cmd/hd_render/CMakeLists.txt` | E | commit separately with command tools | Houdini Python framework rpath fix. Required for installed `hd_render` launch, but not Solaris/IPR warning root. |
| `cmd/hd_cmd/hd_render/hd_render.cc` | E | keep if command-tool validation remains clean | Legacy `UsdImagingDelegate` command tool now excludes raw `RenderProduct`/`RenderVar` prims from population. This does not prove Solaris/IPR clean. |
| `cmd/hd_cmd/hd_usd2rdl/CMakeLists.txt` | E | commit separately with command tools | Houdini Python framework rpath fix. Required for installed `hd_usd2rdl` launch, but not Solaris/IPR warning root. |
| `cmd/hd_cmd/hd_usd2rdl/hd_usd2rdl.cc` | E | keep if command-tool validation remains clean | Same legacy Product/Var traversal cleanup as `hd_render`. |
| `lib/hydramoonray/PrimTypeUtils.cc` | G/B | historical: required investigation before commit | PluginLightFilter alias belonged to light-filter compatibility. It was not treated as the RenderSettings/Product warning fix without prim diagnostics. |
| `lib/hydramoonray/RenderDelegate.cc` | A/B/H | historical: required investigation before commit | Contained real `HdRenderSettings` Bprim sync, render stats/progress mapping, teardown/ownership changes. Also the right location for env-gated prim-type diagnostics. |
| `lib/hydramoonray/RenderDelegate.h` | A/H | historical: required investigation before commit | Delegate ownership/teardown changes were ABI-sensitive and required rebuild/install/sign with matching plugin dylibs. |
| `lib/hydramoonray/RenderPass.cc` | C/H | historical: required investigation before commit | Render pass execution hook and SceneVariables image dimensions path were under audit. |
| `docs/houdini20_5_sceneindex_dirty_stack_audit.md` | A/B/D/E | keep as evidence | Existing audit doc from prior pass. |
| `docs/houdini20_5_sceneindex_render_product_contract.md` | A/B/E | keep as evidence | Documents Product/Var SceneIndex vs legacy delegate split and command-tool result. |
| `docs/houdini20_5_usd_hydra_render_contract.md` | A/B/C/D/E | keep as evidence | Documents intended render contract and current runtime matrix. |
| `docs/houdini20_5_render_contract_final_dirty_stack.md` | A/B/C/D/E/F/G/H/I | keep as evidence | This ledger. |

## DCC Plugins

| Path | Workstream | Risk | Evidence / Action |
|------|------------|------|-------------------|
| `houdini/docs/moonray_render_settings_lop_audit.md` | D/A | keep as evidence | Records RenderSettings/Product resolution/pixel aspect authority and duplicate contract warning policy. |
| `houdini/otls/Lop::DW_MOONRAY::moonrayrendersettings::1.hda` | I/D | generated artifact; commit only with matching source | Binary HDA must match intended Python/HDA generator state. Do not regenerate again until needed. |
| `houdini/python3.11libs/moonray_render_settings.py` | D | keep if validation remains clean | DCC authoring and owned ROP scene-linking. New duplicate-contract warning is non-cook-time userData/comment based. |
| `houdini/tests/dev_validate_moonray_render_settings_lop.py` | D | keep if validation remains clean | Validates DCC RenderSettings/Product contract and duplicate-contract warning behavior. |

## SDR Plugins

| Path | Workstream | Risk | Evidence / Action |
|------|------------|------|-------------------|
| `moonrayShaderParser/parserPlugin.cpp` | F | unrelated; commit separately | Dynamic vector metadata fix, including `iridescence_colors`. Do not touch in this pass unless shader-parser warnings reappear. |

## Generated / Accidental Dirt

No `.DS_Store`, HDA backup folder, or source-tree `__pycache__` files were present after cleanup at the start of this final pass. If validation regenerates cache files, remove them before reporting.

## Immediate Final-Pass Scope

Allowed before behavior patches:

- docs
- env-gated prim-type diagnostics: `HDMR_PRIMTYPE_DIAG=1`
- env-gated camera/framing diagnostics: `HDMR_CAMERA_FRAMING_DIAG=1`

Do not claim the Solaris/IPR warning path is fixed from command-tool validation.
The user must run a fresh Houdini/Solaris/IPR session with diagnostics enabled
after rebuild/install/sign if the warning is only observable in GUI/IPR.
