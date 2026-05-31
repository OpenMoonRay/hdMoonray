#!/usr/bin/env hython

"""Generate a minimal Houdini-authored MoonRay material builder fixture.

This intentionally creates the installed MoonRay Material Builder operator
inside a Solaris Material Library LOP, exports the authored MoonRay USD material
network, and then adds a tiny sphere/light/camera scene around it for husk.
"""

from __future__ import annotations

import argparse
import os

import hou
import moonray_material_builder
from pxr import Gf, Sdf, Usd, UsdGeom, UsdLux, UsdRender, UsdShade


BUILDER_TOOL = "moonraymaterialbuilder"


def _reset_stage():
    stage = hou.node("/stage") or hou.node("/").createNode("lopnet", "stage")
    for child in stage.children():
        child.destroy()
    return stage


def _set_if_present(node, parm_name, value):
    parm = node.parm(parm_name)
    if parm is None:
        raise RuntimeError(f"Missing expected parm {node.path()}.{parm_name}")
    parm.set(value)


def _enable_iridescence_fixture(builder):
    dwa = builder.node("dwa_base")
    _set_if_present(dwa, "iridescence", 1.0)
    _set_if_present(dwa, "iridescence_color_control", "use ramp")
    _set_if_present(dwa, "iridescence_ramp_interpolation_mode", "RGB")

    # Houdini's ramp menu exports tokens such as hermite/catmull-rom; hdMoonray
    # must convert them to MoonRay's native IntVector ramp interpolation enums.
    ramp = hou.Ramp(
        (
            hou.rampBasis.Hermite,
            hou.rampBasis.CatmullRom,
            hou.rampBasis.MonotoneCubic,
            hou.rampBasis.Bezier,
            hou.rampBasis.BSpline,
            hou.rampBasis.Linear,
            hou.rampBasis.Constant,
        ),
        (0.0, 0.167, 0.333, 0.5, 0.667, 0.833, 1.0),
        (
            (1.0, 0.0, 0.0),
            (1.0, 1.0, 0.0),
            (0.0, 1.0, 0.0),
            (0.0, 1.0, 1.0),
            (0.0, 0.0, 1.0),
            (1.0, 0.0, 1.0),
            (1.0, 0.0, 0.0),
        ),
    )
    _set_if_present(dwa, "iridescence_ramp", ramp)


def _make_material_library(enable_iridescence=False):
    stage = _reset_stage()
    matlib = stage.createNode("materiallibrary", "materiallibrary1")
    builder = moonray_material_builder.create_moonray_material_builder(
        {"pane": None, "node": matlib}, "moonray_material"
    )
    if not builder.isMaterialFlagSet():
        raise RuntimeError("MoonRay Material Builder was created without its material flag")

    if builder.node("dwa_base") is None:
        raise RuntimeError("MoonRay Material Builder did not create default DwaBaseMaterial")
    if builder.node("normal_displacement") is None:
        raise RuntimeError("MoonRay Material Builder did not create default NormalDisplacement path")

    if enable_iridescence:
        _enable_iridescence_fixture(builder)

    matlib.parm("genpreviewshaders").set(0)
    matlib.parm("referencerendervars").set(0)
    matlib.parm("matnode1").set(builder.name())
    matlib.parm("matpath1").set("/materials/moonray_material")
    matlib.cook(force=True)
    return matlib, builder


def _add_render_scene(stage):
    UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)

    sphere = UsdGeom.Sphere.Define(stage, "/world/sphere")
    sphere.CreateRadiusAttr(1.5)
    sphere.AddTranslateOp().Set(Gf.Vec3d(0.0, 0.0, 0.0))

    material = UsdShade.Material.Get(stage, "/materials/moonray_material")
    if not material:
        raise RuntimeError("Exported stage does not contain /materials/moonray_material")
    UsdShade.MaterialBindingAPI(sphere).Bind(material)

    light = UsdLux.RectLight.Define(stage, "/lights/key")
    light.AddTransformOp().Set(Gf.Matrix4d(
        0.707107, 0, 0.707107, 0,
        0.353553, 0.866025, -0.353553, 0,
        -0.612372, 0.5, 0.612372, 0,
        -2.5, 4, 3, 1))
    light.CreateIntensityAttr(350.0)
    light.CreateNormalizeAttr(True)
    light.CreateWidthAttr(2.0)
    light.CreateHeightAttr(2.0)

    camera = UsdGeom.Camera.Define(stage, "/camera")
    camera.AddTransformOp().Set(Gf.Matrix4d(
        1, 0, 0, 0,
        0, 0.965926, -0.258819, 0,
        0, 0.258819, 0.965926, 0,
        0, 1.2, 6, 1))
    camera.CreateFocalLengthAttr(35.0)
    stage.SetDefaultPrim(stage.GetPrimAtPath("/world"))

    settings = UsdRender.Settings.Define(stage, "/Render/RenderSettings")
    settings.CreateResolutionAttr(Gf.Vec2i(64, 64))
    settings.CreateCameraRel().SetTargets([Sdf.Path("/camera")])
    product = UsdRender.Product.Define(stage, "/Render/Product")
    product.CreateProductTypeAttr("raster")
    product.CreateProductNameAttr("moonray_material_builder_basic.exr")
    var = UsdRender.Var.Define(stage, "/Render/Vars/color")
    var.CreateDataTypeAttr("color4f")
    var.CreateSourceNameAttr("color")
    var.CreateSourceTypeAttr("raw")
    var.GetPrim().CreateAttribute(
        "driver:parameters:aov:name", Sdf.ValueTypeNames.String).Set("color")
    product.CreateOrderedVarsRel().SetTargets([var.GetPath()])
    settings.CreateProductsRel().SetTargets([product.GetPath()])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--out",
        default="/tmp/moonray_material_builder_basic.usda",
        help="Output USDA path.",
    )
    parser.add_argument(
        "--hip",
        default="",
        help="Optional Houdini HIP file path to save for UI inspection.",
    )
    parser.add_argument(
        "--iridescence",
        action="store_true",
        help="Enable a DwaBaseMaterial iridescence ramp with non-default interpolation tokens.",
    )
    args = parser.parse_args()

    matlib, builder = _make_material_library(args.iridescence)
    usd_stage = Usd.Stage.Open(matlib.stage().Flatten())
    _add_render_scene(usd_stage)
    usd_stage.Export(args.out)

    if args.hip:
        os.makedirs(os.path.dirname(args.hip), exist_ok=True)
        hou.hipFile.save(args.hip)

    print(f"MoonRay Material Builder tool: {BUILDER_TOOL}")
    print(f"MoonRay Material Builder node type: {builder.type().name()}")
    print(f"Builder path: {builder.path()}")
    print(f"Builder children: {[child.type().name() for child in builder.children()]}")
    print(f"Iridescence fixture: {args.iridescence}")
    print(f"USD output: {args.out}")
    if args.hip:
        print(f"HIP output: {args.hip}")


if __name__ == "__main__":
    main()
