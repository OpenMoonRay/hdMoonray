# hdMoonray AOV Audit

## Purpose

This document tracks the current hdMoonray AOV support model, the H20.5 production cameraDepth investigation, the 2026 native AOV baseline, and the Apple Silicon half-packing repair that made the first native production AOV set render filled.

MoonRay `RenderOutput` scene objects are the native MoonRay AOV mechanism. Solaris/USD `RenderVar` prims are the USD/Hydra request mechanism that asks hdMoonray for image buffers. hdMoonray bridges these two worlds by translating Hydra AOV bindings into MoonRay `RenderOutput` objects and then resolving the renderer payload back into Hydra render buffers.

The proven production zero-fill blocker for the first native non-beauty AOV set was Apple Silicon half conversion in `scene_rdl2::grid_util::PackTiles`. The ARM/NEON helper converted scalar floats through vector load/store from scalar addresses, corrupting H16 render-output packets to near-zero/zero values before the receiver decoded them. After replacing that path with scalar `__fp16` conversion plus `std::memcpy`, H20.5.584 production `HdMoonrayRendererPlugin` writes filled EXRs for explicit `alpha`, `depth`, `cameraDepth`, `Z`, `N`, `Ng`, `P`, `Wp`, and `St` RenderVars; `weight` writes the expected constant value for the simple test scene.

`cameraDepth` was used as a diagnostic target because it is one channel, renderer-generated, simple to validate, and the debug renderer proved MoonRay could produce it. It is not a preferred product AOV target. The native 2026 baseline generalized the same failure class to `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight`; the repair below shows that the shared zero-fill was packet half-conversion, not USD authoring, RenderVar metadata, or RenderBuffer lookup.

## References

- Official MoonRay Render Outputs guide: https://docs.openmoonray.org/user-reference/how-to-guides/render-outputs/
- Official MoonRay RenderOutput scene object reference: https://docs.openmoonray.org/user-reference/scene-objects/render-output/RenderOutput/
- Official MoonRay Material AOV guide: https://docs.openmoonray.org/user-reference/how-to-guides/render-outputs/material-aovs/
- Official MoonRay Visibility AOV guide: https://docs.openmoonray.org/user-reference/how-to-guides/render-outputs/visibility-aovs/
- Official MoonRay SceneVariables reference: https://docs.openmoonray.org/user-reference/scene-objects/scene-variables/SceneVariables/
- Official MoonRay Arras render docs: https://docs.openmoonray.org/user-reference/tools/arras_render/
- OpenMoonRay download/superproject context: https://openmoonray.org/download
- Arnold USD repository, architecture reference only: https://github.com/Autodesk/arnold-usd
- Local metadata: `/Applications/MoonRay/installs/openmoonray/coredata/RenderOutput.json`
- Local hdMoonray source:
  - `lib/hydramoonray/RenderBuffer.cc`
  - `lib/hydramoonray/RenderBuffer.h`
  - `lib/hydramoonray/RenderPass.cc`
  - `lib/hydramoonray/RenderDelegate.cc`
  - `plugin/hd_moonray_debug/RndrRenderer.cc`
  - `plugin/hd_moonray/ArrasRenderer.cc`
- Local MoonRay transport/source:
  - `moonray/lib/grid/engine_tool/McrtFbSender.cc`
  - `scene_rdl2/lib/common/grid_util/PackTiles.h`
  - `scene_rdl2/lib/common/grid_util/PackTiles.cc`
  - `moonray_arras/mcrt_dataio/lib/client/receiver/ClientReceiverFb.cc`
  - `moonray_arras/mcrt_dataio/lib/engine/merger/MergeFbSender.cc`
- Generated/temporary artifacts:
  - `/tmp/moonray_aov_audit/bare_sphere_beauty_cameraDepth.usda`
  - `/tmp/moonray_aov_audit/bare_sphere_beauty_N_depth.rdla`
  - `/tmp/moonray_aov_audit/cameraDepth_docpass_prod.exr`
  - `/tmp/moonray_aov_audit/cameraDepth_docpass_debug.exr`
  - `/tmp/moonray_aov_audit/packtiles_roundtrip/packtiles_camera_depth_roundtrip.cc`
  - `/tmp/moonray_aov_audit/packtiles_roundtrip/packtiles_camera_depth_roundtrip.results.txt`
  - `/tmp/pre_aov_parent.diff`
  - `/tmp/pre_aov_hdmoonray.diff`
  - `/tmp/pre_aov_dcc.diff`
  - `/tmp/moonray_native_aov/*.usda`
  - `/tmp/native_aov_*.exr`

## 2026 Native AOV Baseline Before Half-Packing Repair

This baseline was run after the proven Render Settings / Beauty repair stack and before the Apple Silicon half-packing repair. No repository source changed during the native AOV audit; `/tmp/pre_aov_parent.diff`, `/tmp/pre_aov_hdmoonray.diff`, and `/tmp/pre_aov_dcc.diff` still matched the dirty repair stack after the audit.

Protected baseline smoke tests still passed in fresh H20.5.584 production `husk`:

| Path | Result |
|---|---|
| Direct camera/default color | production `HdMoonrayRendererPlugin` wrote `/tmp/pre_aov_direct.exr`; RGB nonzero and `Constant: No` |
| Custom MoonRay Render Settings LOP default | production `HdMoonrayRendererPlugin` wrote `/tmp/pre_aov_custom.exr`; RGB nonzero and `Constant: No` |
| Generic Render Settings + built-in RenderProduct/RenderVar | production `HdMoonrayRendererPlugin` wrote `/tmp/pre_aov_generic.exr`; RGB nonzero and `Constant: No` |

Native AOV production test matrix, one explicit RenderProduct/RenderVar per file:

| AOV | RenderBuffer/MoonRay mapping | Production `HdMoonrayRendererPlugin` | Debug `HdMoonrayRendererDebugPlugin` | Classification |
|---|---|---|---|---|
| color | beauty framebuffer path | nonzero, nonconstant | control not needed | production working |
| alpha | `RESULT_ALPHA` | channel exists, constant zero | nonzero, nonconstant | production zero-filled |
| depth | `RESULT_DEPTH` | channel exists, constant zero | nonzero, nonconstant | production zero-filled |
| Z | `RESULT_STATE_VARIABLE`, `depth` | channel exists, constant zero | nonzero, nonconstant | production zero-filled |
| N | `STATE_VARIABLE_N` | channels exist, constant zero | nonzero, nonconstant | production zero-filled |
| Ng | `STATE_VARIABLE_NG` | channels exist, constant zero | nonzero, nonconstant | production zero-filled |
| P | `STATE_VARIABLE_P` | channels exist, constant zero | nonzero, nonconstant | production zero-filled |
| Wp | `STATE_VARIABLE_WP` | channels exist, constant zero | nonzero, nonconstant | production zero-filled |
| St | `STATE_VARIABLE_ST` | channels exist, constant zero | nonzero, nonconstant | production zero-filled |
| weight | `RESULT_WEIGHT` | channel exists, constant zero | constant nonzero `4.0` | production zero-filled |

RDLA declaration proof for the production `N` test:

```text
RenderOutput("/_outputs/N") {
    ["result"] = "state variable",
    ["file_name"] = "/tmp/scene.exr",
}
```

Conclusion at that point: the previous `cameraDepth` result was not contradicted; it was generalized. Native non-beauty AOVs were correctly mapped far enough to create files, channels, and RenderOutput declarations, and the debug renderer could produce meaningful values. The production Arras renderer delivered or applied zero-filled non-beauty RenderOutput payloads.

This pre-fix conclusion is now superseded by the repair below. Keep it as historical evidence because it explains why the investigation moved from USD authoring and RenderBuffer mapping to the production PackTiles encode/decode path.

## 2026-06-14 Native AOV Production Repair

The first proven production loss point was packet encode/decode, not final EXR writing. Temporary diagnostics showed:

- `McrtFbSender` had nonzero native AOV values before encode, for example `/_outputs/N` and `/_outputs/depth`.
- A sender-side self-decode of the just-encoded packet produced zero values before the packet left mcrt.
- `ClientReceiverFb` decoded the same packet to zero values before hdMoonray copied the payload into Hydra render buffers.
- A standalone half-conversion probe reproduced the Apple Silicon scalar conversion bug: the old ARM path converted values such as `5.12391` to half bits `0x0005`, then back to about `2.98e-07`; scalar `__fp16` plus `std::memcpy` preserved the expected half values.

Retained source changes:

- `scene_rdl2/lib/common/grid_util/PackTiles.cc`: replace ARM/NEON scalar float/half conversion with scalar `__fp16` plus `std::memcpy`.
- `moonray/lib/rendering/rndr/RenderOutputWriter.cc`: apply the same ARM scalar half-conversion fix to native MoonRay RenderOutput writing.
- `hdMoonray/plugin/hd_moonray/ArrasRenderer.cc`: allocate render buffers with the requested channel count even before `RenderBuffer::bind()` has attached the MoonRay `RenderOutput`, and reallocate if size/channel count changes.

Temporary diagnostics were removed from `McrtFbSender.cc` and `ClientReceiverFb.cc`; installed runtime dylibs were rebuilt, installed, ad-hoc signed, and scanned for `HDMR_NATIVE_AOV_DIAG`, `senderPacket`, `ResolvedData`, and diagnostic `maxAbs` markers.

Clean H20.5.584 production `husk` validation after the repair:

| AOV | Output | Production `HdMoonrayRendererPlugin` stats | Classification |
|---|---|---|---|
| color/beauty | `/tmp/moonray_clean_beauty.exr` | RGB float, max about `12128.548828 11771.103516 12066.086914`, `Constant: No` | `PASS_FILLED` |
| alpha | `/tmp/moonray_clean_alpha.exr` | min `0`, max `1`, `Constant: No` | `PASS_FILLED` |
| depth | `/tmp/moonray_clean_depth.exr` | min `0.998059`, max `1.000010`, `Constant: No` | `PASS_FILLED`, semantic range needs product review |
| cameraDepth | `/tmp/moonray_clean_cameraDepth.exr` | min `5.125000`, max `5.937500`, `InfCount: 2459`, `Constant: No` | `PASS_FILLED` |
| Z | `/tmp/moonray_clean_Z.exr` | min `0`, max `5.710938`, `Constant: No` | `PASS_FILLED` |
| N | `/tmp/moonray_clean_N.exr` | normal channels min/max roughly `(-0.900879,-0.930664,0)` to `(0.898926,0.855469,0.999023)`, `Constant: No` | `PASS_FILLED` |
| Ng | `/tmp/moonray_clean_Ng.exr` | normal channels min/max roughly `(-0.900879,-0.930664,0)` to `(0.898438,0.855469,0.999023)`, `Constant: No` | `PASS_FILLED` |
| P | `/tmp/moonray_clean_P.exr` | position channels min/max roughly `(-0.819336,-0.462158,-5.710938)` to `(0.816895,1.169922,0)`, `Constant: No` | `PASS_FILLED` |
| Wp | `/tmp/moonray_clean_Wp.exr` | position channels min/max roughly `(-0.819336,-0.724609,0)` to `(0.816895,0.873535,0.983398)`, `Constant: No` | `PASS_FILLED` |
| St | `/tmp/moonray_clean_St.exr` | min `(0,0)`, max `(0.916016,0.856934)`, `Constant: No` | `PASS_FILLED` |
| weight | `/tmp/moonray_clean_weight.exr` | min=max=avg `64`, `Constant: Yes` | `PASS_CONSTANT_EXPECTED` for the simple fixture |

This proves production payload delivery for the first explicit native RenderVar set. By itself, this transport repair did not prove material AOVs, LPE/light AOVs, visibility AOVs, Cryptomatte, primitive attributes, motion vectors, multi-AOV EXR products, or a finished artist UI. Subsequent Render Settings LOP validation documents the first exposed material/denoise AOV set in `moonray_render_settings_lop_audit.md`; the broader AOV families still remain separate validation work.

## cameraDepth Diagnostic Target Status

`cameraDepth` was an invented/diagnostic target for this investigation, not the preferred product-facing first AOV. It was still useful because current hdMoonray maps `HdAovTokens->cameraDepth` to MoonRay `RenderOutput::RESULT_DEPTH`, and RDLA output confirmed it reached MoonRay as a native `RenderOutput` with `["result"] = "depth"`.

Do not start future product work with another cameraDepth-specific path. It served its purpose as a transport probe and produced too many custom-path hypotheses. The repaired native baseline now proves that cameraDepth shared the same Apple Silicon half-packing failure as the native AOV set, but product-facing work should still start from the native `RenderBuffer.cc` mappings and explicit production EXR proof.

## Source, Build, Install, And Load Mapping

| Item | Path/value | Evidence |
|---|---|---|
| active parent checkout | `/Applications/MoonRay/openmoonray` symlink to `/Applications/MoonRay/source/openmoonray` | `pwd -P` resolves to `/Applications/MoonRay/source/openmoonray` |
| parent branch | `Moonray-Houdini21-macOS` | `git rev-parse --abbrev-ref HEAD` in parent |
| active hdMoonray checkout | `/Applications/MoonRay/openmoonray/moonray/hydra/hdMoonray` | symlink-resolved repo under `/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray` |
| hdMoonray branch | `h20.5-solaris-building-testing` | `git rev-parse --abbrev-ref HEAD` |
| active MoonRay core checkout | `/Applications/MoonRay/openmoonray/moonray/moonray` | symlink-resolved repo under `/Applications/MoonRay/source/openmoonray/moonray/moonray` |
| MoonRay core branch | `h20.5-primitive-user-data-python311` | `git rev-parse --abbrev-ref HEAD` |
| active mcrt_dataio checkout | `/Applications/MoonRay/openmoonray/moonray/moonray_arras/mcrt_dataio` | nested submodule, detached `HEAD` |
| active scene_rdl2 checkout | `/Applications/MoonRay/openmoonray/moonray/scene_rdl2` | branch `h20.5-python311-build` |
| CMake source dir | `/Applications/MoonRay/openmoonray` | build output references this source root |
| CMake build dir | `/Applications/MoonRay/build` | used by `cmake --build /Applications/MoonRay/build --target grid_engine_tool --config Release` |
| edited live-debug file from previous pass | `moonray/lib/grid/engine_tool/McrtFbSender.cc` | restored to clean git state after failed diagnostic |
| target expected to compile it | `grid_engine_tool` | built dylib path `build/moonray/moonray/lib/grid/engine_tool/Release/libgrid_engine_tool.dylib` |
| installed dylib path | `/Applications/MoonRay/installs/openmoonray/lib/libgrid_engine_tool.dylib` | shasum matches rebuilt clean build product: `ed15d845d2b22cf5da841ade62ae0d8dd43a86a6` |
| final diagnostic strings removed | yes | diagnostic marker string scan returned no matches |
| isolated PackTiles test binary | `/tmp/moonray_aov_audit/packtiles_roundtrip/packtiles_camera_depth_roundtrip` | shasum `2cc68f0be91ae622e59cc9224837dd57b9a1c7a5` |
| isolated test linked dylibs | `/Applications/MoonRay/installs/openmoonray/lib` and `/Applications/MoonRay/installs/lib` | `otool -L` and `LC_RPATH` show installed MoonRay libraries |

Source/build/install lessons:

- There was a real confusion risk between `/Applications/MoonRay/openmoonray`, `/Applications/MoonRay/source/openmoonray`, `/Applications/MoonRay/build`, and `/Applications/MoonRay/installs/openmoonray`.
- `/Applications/MoonRay/openmoonray` resolves to `/Applications/MoonRay/source/openmoonray`, but future work must verify this before assuming source edits are built.
- The build directory must be checked before compiling. Earlier cache state referenced a missing H20.5.684 install before it was reconfigured for H20.5.584.
- Rebuilt artifacts must be copied into the actual installed runtime paths. Build-tree plugin changes alone are not enough because H20.5 loads installed dylibs.
- `hd_moonray.dylib`, `libgrid_engine_tool.dylib`, and `libclient_receiver.dylib` can each retain stale diagnostics if source is reverted but binaries are not rebuilt and reinstalled.
- Use shasum checks for build-vs-installed dylibs and `strings` checks for diagnostic markers after every diagnostic pass.

## cameraDepth Evidence Chain

| Stage | File/function/artifact | Evidence | cameraDepth status | Conclusion |
|---|---|---|---|---|
| USD RenderVar | `/tmp/moonray_aov_audit/bare_sphere_beauty_cameraDepth.usda` | `RenderVar "cameraDepth"` has `sourceName = "cameraDepth"`, `sourceType = "raw"`, `dataType = "float"`, and `driver:parameters:aov:name = "cameraDepth"` | authored | USD request is present. |
| Hydra AOV binding | `RenderBuffer::bind()` | `aovNameFromSettings()` returns sourceName for raw sourceType | requested as `cameraDepth` | Hydra binding name is expected. |
| hdMoonray RenderBuffer mapping | `RenderBuffer.cc` lookup table | `HdAovTokens->cameraDepth -> HdFormatFloat32, RO::RESULT_DEPTH` | mapped | `cameraDepth` is an hdMoonray/Hydra name mapped to MoonRay depth result. |
| RDLA RenderOutput | `/tmp/moonray_aov_audit/bare_sphere_beauty_N_depth.rdla` | `RenderOutput("/_outputs/cameraDepth") { ["result"] = "depth", ["math_filter"] = "min" }` | declared active | MoonRay RenderOutput is created. |
| Debug RenderContext snapshot | `RndrRenderer::resolve()` | uses `RenderContext::snapshotRenderOutput()` by RenderOutputDriver index | works | Debug renderer produces valid depth. |
| Production ArrasRenderer lookup | `ArrasRenderer::resolve()` | looks up full path, leaf name, then matched id via `ClientReceiverFb::getRenderOutputMTSafe()` | mostly ruled out | Earlier diagnostics showed `/_outputs/cameraDepth` is listed and lookup returns payload dimensions. |
| ClientReceiverFb raw extraction before fix | `ClientReceiverFb::getRenderOutputMTSafe()` diagnostic from prior pass | returned 64x64 one-channel payload but all zero | present but zero | Lookup is not the primary loss point. |
| ClientReceiverFb closest extraction before fix | same diagnostic | closest extraction also zero, closestStatus was 0 | zero | Closest-filter read did not recover data. |
| RenderOutputDriver mapping | local source/diagnostic | `RESULT_DEPTH` aliases to a depth state AOV internally | valid | `RESULT_DEPTH` is a legitimate MoonRay depth path. |
| McrtFbSender sender-side buffer | prior `McrtFbSender.cc` diagnostic | finite hit depths around 5.1 and background/no-hit `inf` before encode | valid before encode | MoonRay/mcrt has real depth data before transport. |
| PackTiles / delta encode | `McrtFbSender::addRenderOutputToProgressiveFrame()` | calls `PackTiles::encodeRenderOutput()` with active pixels, AOV buffer, default value, weight buffer, direct-to-client/noNumSampleMode=true | suspected | Transport encode/decode is the failure area. |
| Client receiver decode | prior `ClientReceiverFb` diagnostic | decode completed with active action but produced zero data | zero after decode | Receiver side sees zeroed payload. |
| Unit-weight encode attempt | temporary sender patch from previous pass | branch matched, emitted nonzero-sized packets, final EXR stayed zero | failed | Sender unit weights alone were adjacent but not sufficient as a live fix. |
| Unsafe self-decode probe | temporary live sender diagnostic | crashed mcrt with signal 11 | removed | Live self-decode used unsafe/incorrect preconditions; do isolated tests instead. |
| Restored production EXR baseline before half-packing repair | `/tmp/moonray_aov_audit/cameraDepth_docpass_prod.exr` | `Pz` min=0 max=0 constant yes | historical blocked state | Superseded by the 2026-06-14 PackTiles half-conversion repair. |

Important conclusions:

- `cameraDepth` naming was not the root cause.
- `RESULT_DEPTH` versus `STATE_VARIABLE_DEPTH` was tested and was not the root cause.
- The sender-side mcrt buffer contained valid finite depth values before encode.
- The failure occurred after sender-side values existed, in production transport/decode.
- The unit-weight encode attempt did not fix final production `Pz`.
- The unsafe live self-decode probe crashed mcrt with signal 11 and was removed.
- Current status after the 2026-06-14 repair is no longer blocked for this explicit cameraDepth production probe; keep the older blocked evidence as the trail that led to PackTiles.
- Beauty works because radiance/color AOVs are weight-related by design. `cameraDepth` is different because it is a state/depth payload whose finite hit values should survive independently of radiance sample weights.
- The next clean investigation is to follow the receiver-side packet application path with the isolated PackTiles result in mind.

## Confirmed Render-Output Path

| Stage | Current evidence | Remaining unknown |
|---|---|---|
| USD/Hydra RenderVar/AOV | Scratch USDA authored `cameraDepth`, and the native baseline authored raw `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight` RenderVars. hdMoonray `aovNameFromSettings()` uses raw `sourceName`. | Product-facing naming remains deferred until production payloads work. |
| hdMoonray RenderBuffer mapping | `RenderBuffer.cc` maps `cameraDepth` to `HdFormatFloat32` and MoonRay `RESULT_DEPTH`; `depth`, `Z`, `N`, `P`, `Ng`, `Wp`, `St`, `alpha`, and `weight` also map to native RenderOutput result/state-variable paths. | Mapping is not the leading blocker for the native baseline; production payload delivery/decode is. |
| MoonRay RenderOutput declaration | RDLA declared `RenderOutput("/_outputs/cameraDepth")` with `["result"] = "depth"` and `["math_filter"] = "min"`. | The exact best `channel_format`, channel naming, and product metadata for final USD/Husk AOV UX remain deferred. |
| `RenderContext::snapshotDeltaRenderOutput` / `snapshotDeltaAov` | Source shows `snapshotDeltaRenderOutput()` dispatches to `snapshotDeltaAov()` for regular AOVs; transcripts recorded sender-side finite depth values after this snapshot path. | The exact live value/active/weight contract at the input to PackTiles remains unresolved. |
| `McrtFbSender::addRenderOutputToProgressiveFrame` | Source sends render-output buffers through `PackTiles::encodeRenderOutput()`. Diagnostics showed the path was reached and emitted nonzero-sized packets. | Which live active/weight/value combinations produce the zero decoded payload is still not proven safely. |
| `PackTiles::encodeRenderOutput` | Isolated roundtrip proved zero weights can zero finite depth values, and unit/active weights can preserve them in isolation. | The isolated result did not translate to a production fix, so the live contract differs from the simplified probe or another downstream step still clears data. |
| `PackTiles::decodeRenderOutput` | Receiver-side diagnostics saw decode complete with non-empty active masks, but values were already zero after decode. | Whether the zeroing is encode-side, decode-side, packet semantic, or receiver-application semantic remains open. |
| `ClientReceiverFb` decode/application | Diagnostics found `ClientReceiverFb` had the output and returned a one-channel 64x64 payload, but values were zero. | The exact point inside receiver framebuffer application where non-radiance AOVs should bypass weight semantics is still unknown. |
| `ClientReceiverFb::getRenderOutputMTSafe` | hdMoonray production lookup by full path, leaf name, then id returned a payload; closest-filter extraction did not recover data for cameraDepth. Native baseline output files/channels exist but are zero-filled. | Exact receiver application semantics remain unresolved. |
| hdMoonray `ArrasRenderer::resolve` | Source currently asks receiver for full path, leaf, then matched id and copies payload into render buffers. This is less suspicious after receiver diagnostics found zero before final resolve. | Final resolve can still be instrumented for a representative native AOV, but it is not the leading blocker. |
| `RenderBuffer::Resolve` -> EXR output | Before the half-packing repair, Beauty wrote filled pixels while production `cameraDepth/Pz` and native non-beauty AOVs wrote metadata/channel/subimage but constant zero pixels. | Superseded for the explicit native baseline by the 2026-06-14 production EXR proof. |

## Sender-Side Findings

- `McrtFbSender` has semantic access to `RenderOutputDriver` and therefore to render-output metadata. `cameraDepth` is not merely a string by the time it reaches sender code.
- `RenderContext::snapshotDeltaRenderOutput()` delegates regular AOVs to `snapshotDeltaAov()`.
- The investigated snapshot path records AOV values plus the film/beauty weight buffer. This is separate from `RenderOutputDriver::requiresScaledByWeight()`, which is tracked by the sender but does not mean the weight buffer is absent.
- Source and transcript evidence identified two weight-related hazards:
  - snapshot utilities can suppress active pixels when the source weight is zero;
  - `PackTiles::enqTileVal()` can write zero when the supplied weight is zero, even when the render-output packet is not in normalized/scaled mode.
- Earlier non-crashing sender diagnostics observed finite cameraDepth hit values around the expected sphere range, with no-hit/background represented as `inf`.
- Sender diagnostics also showed nonzero packets were emitted for `/_outputs/cameraDepth`.
- Production EXR `Pz` still decoded as zero, so sender presence and packet presence alone were not sufficient.

## Receiver-Side Findings

- `ArrasRenderer::resolve()` asks `ClientReceiverFb` for render outputs by full RenderOutput path, by leaf name, then by enumerated id.
- `ClientReceiverFb::getRenderOutputMTSafe()` returned a 64x64 one-channel cameraDepth payload during diagnostics, but the values were all zero.
- Closest-filter extraction was tested as a hypothesis and did not recover usable depth.
- Receiver decode diagnostics saw non-empty active masks.
- Decoded `FbAov` float payload values were already all zero before active-pixel OR and before hdMoonray final resolve copied data to Hydra buffers.
- This reduces suspicion on hdMoonray `RenderBuffer::Resolve` and EXR writing for the cameraDepth failure. The remaining blocker is before hdMoonray receives usable decoded values.

## PackTiles And Layout Findings

- Isolated PackTiles tests proved finite depth values with zero weights can decode as all zero.
- The same isolated tests proved active/unit weights preserve finite depth values.
- Therefore render-output weights are a real hazard for depth/state transport.
- PackTiles walks render-output buffers in tile-linear order (`tileId << 6` plus tile bit offset), not simple row-major order.
- A row-major active/weight construction was identified as a plausible mismatch risk and was superseded.
- A tile-linear derived active/unit-weight attempt was tested and failed in production.
- An existing-active-mask plus unit-weight attempt was also tested and failed in production.
- Therefore the live production issue is not safely fixed by rewriting only the sender-side weight buffer.

## RenderOutput Attribute Model

| Attribute | Type | Default | Enum values | Required for which result type | Source/evidence | Relevant to hdMoonRay? |
|---|---|---|---|---|---|---|
| active | Bool | true | n/a | all | `RenderOutput.json` | yes |
| camera | SceneObject* | false/null | Camera interface | optional per-output camera | `RenderOutput.json` | later |
| channel_format | Int | 1 | float=0, half=1 | output encoding | `RenderOutput.json`, docs | yes |
| channel_name | String | empty | n/a | output channel naming | `RenderOutput.json` | yes |
| channel_suffix_mode | Int | 0 | auto=0, rgb=1, xyz=2, uvw=3 | multi-channel naming | `RenderOutput.json` | later |
| checkpoint_file_name | String | `checkpoint.exr` | n/a | checkpoint outputs | `RenderOutput.json` | deferred |
| checkpoint_multi_version_file_name | String | empty | n/a | checkpoint outputs | `RenderOutput.json` | deferred |
| compression | Int | 1 | none, zip, rle, zips, piz, pxr24, b44, b44a, dwaa, dwab | file output | `RenderOutput.json` | mostly external/husk |
| cryptomatte_depth | Int | 6 | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_beauty | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_normals | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_p0 | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_positions | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_refn | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_refp | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_output_uv | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_record_reflected | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_record_refracted | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| cryptomatte_support_resume_render | Bool | false | n/a | cryptomatte | `RenderOutput.json` | deferred |
| denoise | Bool | false | n/a | file denoise | `RenderOutput.json` | deferred |
| denoiser_input | Int | 0 | not an input=0, as albedo=1, as normal=2 | denoiser helpers | `RenderOutput.json` | deferred |
| display_filter | SceneObject* | false/null | DisplayFilter interface | display filter result | `RenderOutput.json` | deferred |
| exr_dwa_compression_level | Float | 85 | n/a | file output | `RenderOutput.json` | deferred |
| exr_header_attributes | SceneObject* | false/null | metadata object | EXR header | `RenderOutput.json` | deferred |
| file_name | String | `scene.exr` | n/a | file output | `RenderOutput.json` | hdMoonray currently sets placeholder for outputs |
| file_part | String | empty | n/a | multipart EXR | `RenderOutput.json` | deferred |
| lpe | String | empty | n/a | light aov | `RenderOutput.json`, docs | mapped by prefix but unvalidated |
| material_aov | String | empty | n/a | material aov | `RenderOutput.json`, docs | mapped by prefix but unvalidated |
| material_aov_secondary_rays | Bool | false | n/a | material aov | `RenderOutput.json` | deferred |
| math_filter | Int | 0 | average=0, sum=1, min=2, max=3, force_consistent_sampling=4, closest=5 | reductions and depth | `RenderOutput.json` | yes |
| output_type | String | `flat` | e.g. flat/deep by docs convention | file output | `RenderOutput.json` | deferred |
| primitive_attribute | String | empty | n/a | primitive attribute result | `RenderOutput.json` | mapped by prefix but unvalidated |
| primitive_attribute_type | Int | 0 | FLOAT=0, VEC2F=1, VEC3F=2, RGB=3 | primitive attribute | `RenderOutput.json` | mapped by prefix |
| result | Int | 0 | see result table | all | `RenderOutput.json`, `RenderOutput.h` | yes |
| resume_file_name | String | empty | n/a | resume render | `RenderOutput.json` | deferred |
| state_variable | Int | 2 | see state table | state variable result | `RenderOutput.json`, `RenderOutput.h` | yes |
| visibility_aov | String | default LPE expression | n/a | visibility aov | `RenderOutput.json`, docs | deferred |

## RenderOutput Result Enum

| result enum/value | Meaning | Required extra attributes | Simple/direct output? | Requires material/shader/light setup? | Safe hdMoonray target? | Notes |
|---|---|---|---|---|---|---|
| RESULT_BEAUTY / 0 / beauty | full RGB render | none | yes | no | yes | Production works. |
| RESULT_ALPHA / 1 / alpha | alpha | none | yes | no | debug works, production zero | Reference/skip path in sender. |
| RESULT_DEPTH / 2 / depth | z distance from camera | usually `math_filter=min`, float channel | yes | no | debug works, production zero | hdMoonray `cameraDepth` and native `depth` map here. |
| RESULT_STATE_VARIABLE / 3 / state variable | built-in state variable | `state_variable` | yes | no | debug only today | Production payload delivery is broken for tested state AOVs. |
| RESULT_PRIMITIVE_ATTRIBUTE / 4 / primitive attribute | procedural/prim attr | `primitive_attribute`, `primitive_attribute_type` | no | requires attr | later | Needs USD primvar/attribute validation. |
| RESULT_HEAT_MAP / 5 / time per pixel | diagnostic timing | none | yes | no | mapped but untested | Sender special reference path. |
| RESULT_WIREFRAME / 6 / wireframe | wireframe diagnostic | none | yes | no | unsafe | Code comment says crashes. |
| RESULT_MATERIAL_AOV / 7 / material aov | material expression | `material_aov` | no | material labels/closures | later | Mapped by `shader:` prefix. |
| RESULT_LIGHT_AOV / 8 / light aov | LPE | `lpe` | no | scene/light setup | later | Mapped by `lpe:` prefix. |
| RESULT_VISIBILITY_AOV / 9 / visibility aov | light visibility ratio | `visibility_aov` | no | light/path setup | later | Deferred. |
| RESULT_WEIGHT / 11 / weight | sample weight | none | yes | no | debug constant nonzero, production zero | Sender special reference path. |
| RESULT_BEAUTY_AUX / 12 / beauty aux | adaptive auxiliary | none | yes | no | mapped but untested | Sender special reference path. |
| RESULT_CRYPTOMATTE / 13 / cryptomatte | ID mattes | cryptomatte attrs and ID setup | no | cryptomatte setup | deferred | Complex, not first target. |
| RESULT_ALPHA_AUX / 14 / alpha aux | adaptive alpha auxiliary | none | yes | no | mapped but untested | Sender special reference path. |
| RESULT_DISPLAY_FILTER / 15 / display filter | display filter output | `display_filter` | no | display filter object | deferred | Not part of first AOV implementation. |

## State Variable Enum

| state_variable enum/value | Meaning | Channels | Expected MoonRay type/format | Expected Hydra HdFormat | Expected USD RenderVar dataType | Safe first target? | Notes |
|---|---|---:|---|---|---|---|---|
| P / 0 | position | 3 | VEC3F/RGB-like | Float32Vec3 | vector3f/color3f | after transport fix | Debug works; production zero observed. |
| Ng / 1 | geometric normal | 3 | VEC3F | Float32Vec3 | normal3f/vector3f | after transport fix | Debug works; production zero observed. |
| N / 2 | shading normal | 3 | VEC3F | Float32Vec3 | normal3f/vector3f | after transport fix | Debug works; production zero observed. |
| St / 3 | texture coordinates | 2 | VEC2F | Float32Vec2 | float2 | after transport fix | Debug works; production zero observed. |
| dPds / 4 | derivative of P wrt S | 3 | VEC3F | Float32Vec3 | vector3f | later | Mapped. |
| dPdt / 5 | derivative of P wrt T | 3 | VEC3F | Float32Vec3 | vector3f | later | Mapped. |
| dSdx / 6 | s derivative wrt x | 1 | FLOAT | Float32 | float | later | Mapped. |
| dSdy / 7 | s derivative wrt y | 1 | FLOAT | Float32 | float | later | Mapped. |
| dTdx / 8 | t derivative wrt x | 1 | FLOAT | Float32 | float | later | Mapped. |
| dTdy / 9 | t derivative wrt y | 1 | FLOAT | Float32 | float | later | Mapped. |
| Wp / 10 | world position | 3 | VEC3F | Float32Vec3 | vector3f/color3f | later | Mapped. |
| depth / 11 | z distance from camera | 1 | FLOAT | Float32 | float | candidate | `Z` maps here; `cameraDepth` uses result depth instead. |
| motionvec / 12 | 2D motion vector | 2 | VEC2F | Float32Vec2 | float2 | later | Do not implement in this pass. |

## Material AOV Model

Material AOVs are diagnostic material-property outputs. Official docs state they are not influenced by scene lighting or occlusion and use a label/selection/property expression model.

| material_aov/property | Meaning | Requires material labels/selection? | Lighting affected? | Supported by MoonRay docs/source? | Supported by hdMoonray now? | Safe later target? |
|---|---|---|---|---|---|---|
| albedo | unoccluded white-light response | optional labels/selections | no | yes | mapped by `shader:` only | yes, later |
| color | material/lobe color | optional labels/selections | no | yes | mapped by `shader:` only | yes, later |
| emission | emitted radiance property | optional labels/selections | no | yes | mapped by `shader:` only | later |
| factor | fresnel factor | optional labels/selections | no | yes | mapped by `shader:` only | later |
| normal | material normal property | optional labels/selections | no | yes | mapped by `shader:` only | later |
| radius | subsurface radius | optional labels/selections | no | yes | mapped by `shader:` only | later |
| roughness | glossy roughness | optional labels/selections | no | yes | mapped by `shader:` only | later |
| depth, dPds, dPdt, dSdx, dSdy, dTdx, dTdy, matte, motionvec, N, Ng, P, pbr_validity, St, Wp | documented accepted material AOV properties in local metadata | varies | no | yes, in `RenderOutput.json` material_aov comment | not production-validated | later |
| float:<attr>, rgb:<attr>, vec2:<attr>, vec3:<attr> | primitive attribute access through material AOV expression | requires attr | no | yes, in local metadata | not production-validated | later |

## Primitive Attribute AOV Model

| primitive_attribute_type enum/value | Meaning | Hydra format | USD dataType | Notes |
|---|---|---|---|---|
| FLOAT / 0 | one float channel | Float32 | float | Used for int-like Hydra ID fallbacks today, but exact ID behavior needs validation. |
| VEC2F / 1 | two float channels | Float32Vec2 | float2 | Mapped for unknown `primvars:` if format guessed as vec2. |
| VEC3F / 2 | three float channels | Float32Vec3 | vector3f/color3f | Mapped for vec3 primvars. |
| RGB / 3 | three color channels | Float32Vec3 | color3f | Default for unknown non-scalar primvars. |

## LPE, Light, And Visibility AOV Model

| AOV type | Attribute | Example values | Requires scene/light setup? | Supported by MoonRay? | Supported by hdMoonray now? | Notes |
|---|---|---|---|---|---|---|
| LPE/light AOV | `lpe` | `caustic`, `diffuse`, `emission`, `glossy`, `mirror`, `reflection`, `translucent`, `transmission` | yes | yes | prefix `lpe:` maps to `RESULT_LIGHT_AOV` | Deferred. |
| light aov | `result=light aov`, `lpe` | custom OSL-style LPE | yes | yes | mapped by `lpe:` | Needs scene/light validation. |
| visibility aov | `visibility_aov` | default expression from metadata | yes | yes | not currently exposed in RenderBuffer lookup | Deferred. |

## RenderBuffer To MoonRay RenderOutput Contract

| Field | Value | Evidence |
|---|---|---|
| Hydra requested AOV name | `cameraDepth` | USD RenderVar sourceName and RenderBuffer lookup |
| USD RenderVar sourceName | `cameraDepth` | scratch USDA |
| USD RenderVar dataType | `float` | scratch USDA |
| RenderPass AOV binding name | `cameraDepth` | raw sourceType returns sourceName in `aovNameFromSettings()` |
| RenderBuffer constructor/input name | Hydra render buffer id bound to AOV | `RenderBuffer::bind()` |
| RenderBuffer internal AOV token/name | `cameraDepth` | `mAovName = aovNameFromSettings(aovBinding)` |
| RenderBuffer HdFormat | `HdFormatFloat32` | lookup table |
| RenderBuffer channel count | 1 | `Allocate()` format handling |
| RenderBuffer clear value | USD clearValue 0 in fixture | scratch USDA |
| RenderBuffer MoonRay output path | `/_outputs/cameraDepth` | `ROId = "/_outputs/" + mAovName` |
| RenderBuffer MoonRay result enum | `RESULT_DEPTH` | lookup table and RDLA |
| RenderBuffer MoonRay state_variable enum | not set for `cameraDepth` | `RESULT_DEPTH` branch |
| RenderBuffer MoonRay math_filter | `MATH_FILTER_MIN` | RenderBuffer depth branch and RDLA |
| RenderBuffer MoonRay channel_format | default half unless custom settings override | metadata default; no override in RDLA |
| RenderBuffer MoonRay channel_name | empty/default | metadata default; no override in RDLA |
| RenderBuffer MoonRay output_type | `flat` | metadata default |
| RenderBuffer active flag | true | RenderBuffer sets active; RDLA output exists |

Answers:

- `cameraDepth` uses official `RenderOutput.result = depth`.
- `Z` uses `RenderOutput.result = state variable` plus `state_variable = depth`.
- `cameraDepth` is different from `Z` in hdMoonray.
- `MATH_FILTER_MIN` matches MoonRay's depth example and is not currently proven to be the production transport root cause.
- Production closest-filter depth is not currently involved; earlier diagnostics reported closestStatus 0 for cameraDepth.
- `cameraDepth` is a USD/Hydra/hdMoonray name. MoonRay internally receives a RenderOutput named `/_outputs/cameraDepth` with result `depth`.

## Current hdMoonray RenderBuffer AOV Matrix

| Hydra/RenderBuffer AOV name | MoonRay RenderOutput result | Extra MoonRay attr | math_filter | channel_format | channel_name | HdFormat | channel count | USD RenderVar dataType | Debug plugin payload | Production plugin payload | Current status | Missing work |
|---|---|---|---|---|---|---|---:|---|---|---|---|---|
| color | beauty special case | none | default | n/a | n/a | Float32Vec4 | 4 | color4f | works | works | works production | none for beauty |
| beauty | beauty | none | default | default | default | Float32Vec3 | 3 | color3f | equivalent to color path when routed as `color` | production working through current Beauty RenderVar contract | works production | keep scoped to beauty |
| alpha | alpha | none | default | default | default | Float32 | 1 | float | works | filled | works production | product UI/multi-AOV validation |
| depth | depth | none | min | default | default | Float32 | 1 | float | works | filled, normalized-looking range in simple test | works production; semantics need review | product semantics validation |
| cameraDepth | depth | none | min | default | default | Float32 | 1 | float | works | filled finite depth with `inf` background | works production as diagnostic/Hydra mapping | not preferred product AOV name |
| heat_map | time per pixel | none | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | validate |
| wireframe | wireframe | none | default | default | default | Float32Vec3 | 3 | color3f | untested | untested | unsafe | code comment says crashes |
| weight | weight | none | default | default | default | Float32 | 1 | float | constant nonzero | constant `64` in simple test | works production for simple fixture | broader sampling-scene validation |
| beauty_aux | beauty aux | none | default | default | default | Float32Vec3 | 3 | color3f | untested | untested | mapped but untested | adaptive validation |
| alpha_aux | alpha aux | none | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | adaptive validation |
| P | state variable | P | default | default | default | Float32Vec3 | 3 | vector3f | works | filled | works production | product UI/multi-AOV validation |
| Ng | state variable | Ng | default | default | default | Float32Vec3 | 3 | normal3f/vector3f | works | filled | works production | product UI/multi-AOV validation |
| N / normal | state variable | N | default | default | default | Float32Vec3 | 3 | normal3f | works | filled | works production | product UI/multi-AOV validation |
| St / primvars:st | state variable | St | default | default | default | Float32Vec2 | 2 | float2 | works | filled | works production | product UI/multi-AOV validation |
| dPds | state variable | dPds | default | default | default | Float32Vec3 | 3 | vector3f | untested | untested | mapped but untested | validate |
| dPdt | state variable | dPdt | default | default | default | Float32Vec3 | 3 | vector3f | untested | untested | mapped but untested | validate |
| dSdx | state variable | dSdx | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | validate |
| dSdy | state variable | dSdy | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | validate |
| dTdx | state variable | dTdx | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | validate |
| dTdy | state variable | dTdy | default | default | default | Float32 | 1 | float | untested | untested | mapped but untested | validate |
| Wp | state variable | Wp | default | default | default | Float32Vec3 | 3 | vector3f | works | filled | works production | product UI/multi-AOV validation |
| Z | state variable | depth | default | default | default | Float32 | 1 | float | works | filled | works production | product UI/multi-AOV validation |
| motionvec | state variable | motionvec | default | default | default | Float32Vec2 | 2 | float2 | not tested here | not tested here | mapped but untested | later |
| primvars:* | primitive attribute | primitive_attribute/type | varies | default | default | guessed | varies | varies | untested | untested | mapped but requires primitive attr setup | later |
| lpe:* | light aov | lpe | default | default | default | Float32Vec3 default | 3 | color3f | untested | untested | mapped but requires LPE/light setup | later |
| shader:* | material aov | material_aov | default | default | default | Float32Vec3 default | 3 | color3f | untested | untested | mapped but requires material setup | later |
| cryptomatte | cryptomatte | cryptomatte attrs | default | default | default | custom | custom | custom | untested | untested | mapped but complex | later |
| primId/instanceId/elementId/edgeId/pointId | primitive attribute fallback | primitive attribute | closest | default | default | Int32 | 1 | int | untested | untested | mapped but format uncertain | selection/ID validation |

## AOV Support Tiers

| Tier | AOV family | Examples | Current hdMoonray status | First validation target | Blocker |
|---|---|---|---|---|---|
| 0 | beauty/color only | color/beauty | production-working | done | none |
| 1 | simple renderer/state outputs | cameraDepth/depth, Z, N, Ng, P, Wp, St, alpha, weight | mapped; debug works; production H20.5 explicit RenderVar tests are filled after Apple Silicon half-packing repair | multi-AOV/product UI validation | product/UI exposure and broader scene coverage |
| 2 | material/diagnostic outputs | albedo, roughness, normal, emission, pbr_validity | prefix mapped but unvalidated | albedo after Tier 1 product contract | material AOV model and payload validation |
| 3 | primitive attribute/primvar outputs | primvars:st, IDs, custom attrs | prefix mapped but unvalidated | simple float primvar | USD primvar source and type mapping |
| 4 | LPE/light/visibility outputs | diffuse, glossy, emission, visibility | LPE prefix mapped; visibility not exposed | simple LPE after transport | scene/light setup and output semantics |
| 5 | Cryptomatte/ID/deep/denoiser helpers | Cryptomatte, OIDN helpers, deep | complex/deferred | none yet | dedicated implementation |

## H20.5 Validation

Houdini validation used `/Applications/Houdini/Houdini20.5.584/Frameworks/Houdini.framework/Versions/20.5/Resources/bin/husk`.

Renderer discovery in the normal interactive user package environment can crash while Houdini loads unrelated third-party render packages. The controlled H20.5.584 validation environment used:

```zsh
export HOUDINI_INSTALL_DIR=/Applications/Houdini/Houdini20.5.584
source /Applications/MoonRay/installs/openmoonray/scripts/macOS/setupHoudini.sh
export HOUDINI_PACKAGE_SKIP=1
```

In that controlled environment, `husk --list-renderers` lists `HdMoonrayRendererPlugin (Moonray)` and `HdMoonrayRendererDebugPlugin`.

| Renderer | AOV | Exists? | Min | Max | Constant? | Finite hit values preserved? | Background representation | Pass? |
|---|---|---|---:|---:|---|---|---|---|
| HdMoonrayRendererPlugin | beauty/C | yes | 0 | 12744.106445 | no | yes | black background/alpha | pass |
| HdMoonrayRendererPlugin before half-packing repair | cameraDepth/Pz | yes | 0 | 0 | yes | no | zero-filled | fail |
| HdMoonrayRendererPlugin after half-packing repair | cameraDepth/Pz | yes | 5.125000 | 5.937500 | no | yes | `inf` no-hit background, InfCount 2459 | pass |
| HdMoonrayRendererDebugPlugin | cameraDepth/Pz | yes | 5.123846 | 5.973000 | no | yes | `inf` no-hit background, InfCount 2458 | pass |

The 2026 native baseline extended this validation with explicit RenderVars for `alpha`, `depth`, `Z`, `N`, `Ng`, `P`, `Wp`, `St`, and `weight`. Before the half-packing repair, production output was zero-filled for all non-beauty native AOVs. After the repair, production output is filled/nonconstant for `alpha`, `depth`, `cameraDepth`, `Z`, `N`, `Ng`, `P`, `Wp`, and `St`; `weight` is constant nonzero as expected in the simple fixture.

## Arras Shutdown Socket Error

Production H20.5 `husk` renders that successfully write filled Beauty EXRs can still log:

```text
{dispatcherExit} Message Dispatcher [libcomputation_progmcrt.dylib] : exiting : reason is 'socket was disconnected'
{clientSocketError} SocketPeer::receive: Bad file descriptor
```

Observed ordering in `/tmp/pre_aov_direct_stdout.log` and `/tmp/pre_aov_generic_stdout.log`:

1. mcrt launches and reaches `stage shading complete`.
2. Render timing is printed.
3. `{trace:comp} stop` is printed.
4. stderr reports dispatcher exit / socket disconnected / bad file descriptor.
5. Output EXRs exist and are nonconstant.

Source path: `arras/arras4_core/arras4_client/lib/client/api/Client.cc::threadProc()` suppresses a closed socket only when the client is already in `STATE_DISCONNECTING`; otherwise it reports `{clientSocketError}` and marks a connection error.

Classification: this is not proven harmless teardown noise, and it may still relate to IPR disconnect behavior. It was not the cause of the native AOV zero-fill bug fixed by the half-packing repair. Do not suppress or mask this log without source proof that the disconnect is expected in this lifecycle.

## 2026-06-06 Production Transport Attempts

The focused follow-up pass tested whether the live production failure was caused by the render-output weight/active-mask path around `McrtFbSender::addRenderOutputToProgressiveFrame()` and `PackTiles::encodeRenderOutput()`.

| Attempt | Source change tested | Output | Beauty result | cameraDepth result | Conclusion |
|---|---|---|---|---|---|
| derived finite-depth active/unit weights | For `/_outputs/cameraDepth`, derived tile-linear active pixels and unit weights from finite positive depth values before encode. | `/tmp/moonray_aov_audit/cameraDepth_depth_transport_tile_fix_prod.exr` | `C` nonconstant, max RGB about `12744.106445 5335.891602 3334.152344`, alpha max `1` | `Pz.Z` min=max=avg `0`, constant | Failed. A sender-side unit-weight mask derived from finite depth values did not fix the production EXR. |
| existing active mask + unit weights | Preserved the live snapshot active mask and set unit weights for active `cameraDepth` pixels before encode. | `/tmp/moonray_aov_audit/cameraDepth_depth_transport_unit_weight_prod.exr` | `C` nonconstant, max RGB about `12744.107422 5335.892090 3334.152588`, alpha max `1` | `Pz.Z` min=max=avg `0`, constant | Failed. The live production zero is not fixed by sender-side unit weights alone. |
| guarded sender diagnostic | Added temporary `HDMR_CAMDEPTH_TRANSPORT_DIAG` sender-side diagnostics to inspect live active/value/weight state. | `/tmp/moonray_aov_audit/cameraDepth_sender_active_values_diag.log` | render did not complete | mcrt exited with `std::bad_alloc`, then signal 11 | Failed unsafe probe. The diagnostic was removed and the installed runtime was rebuilt without diagnostic strings. |

The source changes from these attempts were not retained. The installed H20.5 runtime was restored to a non-diagnostic build; `strings` checks on the relevant installed dylibs no longer find `HDMR_CAMDEPTH` markers.

The earlier non-crashing diagnostics remain the best evidence for the live value path:

- sender-side mcrt had finite hit depths around the expected sphere range, with no-hit/background represented as `inf`;
- receiver-side decode saw a non-empty active mask and nonzero packet sizes for `/_outputs/cameraDepth`;
- the decoded render-output values were already all zero before hdMoonray final buffer resolve.

The failed 2026-06-06 attempts narrow the remaining blocker: the live production issue is not safely fixed by rewriting only the sender-side weight buffer. The next safe investigation should instrument the exact `RenderContext::snapshotDeltaRenderOutput()` -> `McrtFbSender::addRenderOutputToProgressiveFrame()` -> `PackTiles::encodeRenderOutput()` input contract, or reproduce that complete contract in an isolated test, before changing PackTiles or receiver application semantics.

## Failed Hypotheses And Attempts

| Attempt/debug path | Intent | Files touched | Evidence produced | Result | Source retained? | Runtime cleaned? | Final status |
|---|---|---|---|---|---|---|---|
| original `RESULT_DEPTH` active/unit sender fix | Prevent depth values from being suppressed by radiance weights. | `moonray/lib/grid/engine_tool/McrtFbSender.cc`, `.h` | Beauty stayed valid; `Pz` stayed constant zero. | failed | no | yes | superseded by later narrower/layout-aware attempts |
| env-gated sender diagnostic | Print live sender stats only when `HDMR_CAMDEPTH_TRANSPORT_DIAG=1`. | `McrtFbSender.cc` | Initial env propagation/diagnostic behavior was unreliable across mcrt process boundaries. | inconclusive | no | yes | useful evidence only, superseded |
| hard-limited sender diagnostic | Force limited sender logging without relying on verbose logger routing. | `McrtFbSender.cc` | Proved sender branch could be reached and packets were nonzero. | evidence only | no | yes | useful evidence only |
| hdMoonray resolve diagnostic | Check whether `ArrasRenderer::resolve()` could find the cameraDepth output and whether it copied zeros. | `plugin/hd_moonray/ArrasRenderer.cc` | Full path/leaf/id lookup found output dimensions; final values still zero. | evidence only | no | yes | useful evidence only |
| ClientReceiverFb decode diagnostic | Inspect receiver-side decoded render-output payload before hdMoonray resolve. | `moonray_arras/mcrt_dataio/lib/client/receiver/ClientReceiverFb.cc` | Decode/application saw non-empty active masks but all-zero float payload. | evidence only | no | yes | useful evidence only |
| closest-filter extraction hypothesis | Test whether cameraDepth needed closest-filter receiver plane. | `ArrasRenderer.cc` | Closest extraction did not recover depth; closest status did not explain zero payload. | failed | no | yes | superseded |
| `cameraDepth` as `STATE_VARIABLE_DEPTH` hypothesis | Test whether `RESULT_DEPTH` was the wrong MoonRay result type. | hdMoonray RenderBuffer mapping during temporary test | Production did not become nonzero. | failed | no | yes | superseded |
| finite-depth derived active/unit weights | Build active mask and unit weights from finite positive depth values. | `McrtFbSender.cc`, `.h` | `/tmp/moonray_aov_audit/cameraDepth_depth_transport_tile_fix_prod.exr`: Beauty nonconstant, `Pz` zero. | failed | no | yes | failed |
| tile-linear derived active/unit weights | Correct the derived active/weight layout to PackTiles tile-linear order. | `McrtFbSender.cc`, `.h` | Render succeeded; `Pz` still zero. | failed | no | yes | failed |
| existing active mask + unit weights | Keep MoonRay snapshot active mask, replace only transport weights with unit weights. | `McrtFbSender.cc` | `/tmp/moonray_aov_audit/cameraDepth_depth_transport_unit_weight_prod.exr`: Beauty nonconstant, `Pz` zero. | failed | no | yes | failed |
| sender unit-weight encode attempt | Confirm whether nonzero sender packets plus unit weights were enough. | `McrtFbSender.cc` | Branch reached and packet size nonzero in diagnostics; final EXR stayed zero. | failed | no | yes | useful evidence only |
| sender-side encode/self-decode diagnostic | Decode the just-encoded packet inside sender to split encode vs downstream. | `McrtFbSender.cc` | mcrt crashed with signal 11; unsafe probe. | unsafe | no | yes | unsafe, do not repeat as live mcrt probe |
| guarded active-value diagnostic | Count values/weights at active pixels under `HDMR_CAMDEPTH_TRANSPORT_DIAG`. | `McrtFbSender.cc` | mcrt exited with `std::bad_alloc` / signal 11 before useful output. | unsafe | no | yes | unsafe |
| unsafe self-decode diagnostic | Live self-decode using production packet state. | `McrtFbSender.cc` | Crashed mcrt / signal 11. | unsafe | no | yes | unsafe |

Rows marked “useful evidence only” should not be copied as implementation. They are breadcrumbs for where evidence came from. Rows marked “unsafe” should not be repeated inside live mcrt without first building an isolated reproduction.

## Production Failure Classification Before Half-Packing Repair

| Hypothesis | Evidence for | Evidence against | Verdict |
|---|---|---|---|
| source/build/install mismatch invalidated prior diagnostics | possible risk in nested MoonRay core builds | clean rebuild/install shasums match; diagnostic strings removed; H20.5 baseline is stable | ruled out for current state |
| wrong RenderOutput parameter contract | cameraDepth could have been `state_variable depth` | MoonRay docs show direct `result=depth`; RDLA declares result depth; explicit state-variable test did not fix production | unlikely |
| valid RenderOutput contract but mcrt does not populate AOV film buffer | production final is zero | sender-side diagnostic found finite depths before encode | ruled out |
| mcrt populates it but McrtFbSender snapshots zero | final is zero | sender-side snapshot diagnostic had finite depths and `inf` background | ruled out |
| McrtFbSender has real data but PackTiles/delta encode suppresses it | isolated PackTiles test reproduces zero decode with zero weights; later sender self-decode showed H16 packet values already zero | Apple Silicon scalar half conversion was proven broken and fixed | proven for the repaired native baseline |
| McrtFbSender has real data but MergeFbSender drops or clears it | merger could be in path in distributed render | local H20.5 production did not fire merger diagnostic | not local blocker |
| McrtFbSender has real data but ClientReceiverFb decode returns zeros | prior receiver diagnostic showed decode zeros | isolated test suggests zero weights can cause that | likely downstream symptom |
| receiver has real data but ArrasRenderer reads wrong payload plane | ArrasRenderer could lookup wrong name | receiver payload itself was zero; lookup returned dimensions | unlikely |
| output only available at final frame but sampled too early | progressive timing can affect AOVs | restored final EXR still zero; debug final works | unlikely |

## Isolated PackTiles Roundtrip

The isolated probe in `/tmp/moonray_aov_audit/packtiles_roundtrip/packtiles_camera_depth_roundtrip.cc` exercises:

- `scene_rdl2::grid_util::PackTiles::encodeRenderOutput`
- `scene_rdl2::grid_util::PackTiles::decodeRenderOutput`
- one-channel `VariablePixelBuffer::FLOAT`
- `noNumSampleMode=true`
- `doNormalizeMode=false`
- `closestFilterStatus=false`
- synthetic finite depth hits around 5.24 to 5.87
- `inf`, zero, and large finite background/default cases
- zero, active-one, and all-one weight buffers

| Case | Value buffer | Weight buffer | Active mask | defaultValue | noNumSampleMode/directToClient | Expected | Actual |
|---|---|---|---|---|---|---|---|
| finite hits + inf background | production-like | all zero | finite hit pixels active | inf | true | nonzero finite hits survive | decoded all zero |
| finite hits + inf background | production-like | unit on finite hits | finite hit pixels active | inf | true | nonzero finite hits survive | finite hits preserved; background partially zero/inf |
| finite hits + inf background | production-like | all ones | all pixels active | inf | true | nonzero finite hits survive | finite hits and inf background preserved |
| finite hits + inf background | production-like | all zero | all pixels active | inf | true | identify whether mask or weights suppress | decoded all zero |
| finite hits + zero background | production-like | all zero | finite hit pixels active | zero | true | isolate inf default behavior | decoded all zero |
| finite hits + large finite background | production-like | all zero | finite hit pixels active | large finite | true | isolate inf default behavior | decoded all zero |
| finite hits only | no inf | all zero | finite pixels active | zero | true | isolate weight gating | decoded all zero |

Historical key result: isolated PackTiles encode/decode reproduced zeroing when the render-output weight buffer was all zero, even with `doNormalizeMode=false`. Finite depth survived when weights were active/unit. This correctly kept attention on PackTiles, but the later production diagnostic split proved the actual Apple Silicon loss point was scalar float/half conversion in the H16 packet path.

Do not revive the failed unit-weight sender fixes. The retained PackTiles change is narrow: it corrects the ARM scalar half conversion used by the existing packet path.

## Current Evidence-Based Conclusion

- Production Beauty remains working in H20.5.584.
- The first native non-beauty AOV production failure was a real transport/packet problem, not USD authoring or RenderBuffer lookup.
- The exact proven loss point was Apple Silicon H16 packet conversion in `PackTiles`: mcrt had real AOV values before encode, but the encoded/self-decoded packet was zero because the ARM scalar half conversion was wrong.
- `cameraDepth/Pz` now renders filled finite hit values in production after the same fix, but it remains a diagnostic/Hydra mapping rather than the preferred product-facing first AOV name.
- Explicit single-RenderVar production tests now pass for `alpha`, `depth`, `cameraDepth`, `Z`, `N`, `Ng`, `P`, `Wp`, and `St`; `weight` returns constant nonzero `64` in the simple fixture.
- Temporary sender/receiver diagnostics were removed from source and installed runtime binaries.
- Material AOVs, LPE/light AOVs, visibility AOVs, primitive attributes, Cryptomatte, motion vectors, multi-AOV products, and artist-facing AOV UI remain deferred until separately proven.

## Next AOV Pass Rules

Do not repeat the custom cameraDepth-first path. The native baseline and repair have already generalized and fixed the first transport failure.

Rules for the next AOV transport pass:

1. Do not start with `cameraDepth`.
2. Do not invent AOV names.
3. Treat `RenderBuffer.cc` mappings as the source of truth.
4. Use the native baseline evidence before changing transport code.
5. Prefer `N`, `depth`, or another native `RenderBuffer.cc` mapping as representative tests before adding product UI.
6. No PackTiles, mcrt, receiver, or transport change without source-path proof, log proof, and H20.5 production EXR proof.
7. No UI exposure before production-filled payloads for the exact AOVs being exposed.
8. No `moonray_dcc_plugins` or UI changes before production payloads are proven.
9. No material AOVs, LPEs, primvars, Cryptomatte, denoiser outputs, or light AOVs in the baseline pass.
10. No support claim without H20.5 production EXR stats proving nonzero/nonconstant finite payloads where expected.

## Open Questions

- Does `depth` versus `cameraDepth` need a product-level naming/semantics decision? The simple test produced filled values for both, but `depth` had a normalized-looking range while `cameraDepth/Pz` carried finite camera-space depths plus `inf` background.
- Which native AOVs should be exposed first in Houdini UI, and should they be single-product controls or a separate AOV/product builder?
- Do multiple ordered RenderVars in one RenderProduct preserve all native AOVs and Beauty together in production H20.5?
- Do material AOVs, LPE/light AOVs, primitive attributes, visibility AOVs, Cryptomatte, and motion vectors work after the packet fix, or do they need separate implementation?
- Does the `{clientSocketError} SocketPeer::receive: Bad file descriptor` shutdown log have any relationship to IPR disconnects, or is it isolated teardown behavior after successful disk renders?
