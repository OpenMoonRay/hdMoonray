"""Generate Houdini-authored Dome Light LOP fixtures.

Run with Houdini's hython after sourcing the Houdini/OpenMoonRay environment.
The script intentionally creates the real Houdini `domelight::3.0` LOP and
does not edit its hidden `primtype` parameter.
"""

from __future__ import print_function

import json
import os
import sys

import hou
from pxr import Usd


OUT_DIR = "/Applications/MoonRay/runtime-fixtures/houdini_ui_dome"
TEXTURE_PATH = (
    "/Applications/MoonRay/source/openmoonray/moonray/hydra/hdMoonray/"
    "testSuite/runtime/h20_isolation/assets/dome_texture.ppm"
)


def _set_if_exists(node, parm_name, value):
    parm = node.parm(parm_name)
    if parm is not None:
        parm.set(value)
        return True
    return False


def _build_network(name, texture=False):
    stage = hou.node("/stage")
    if stage is None:
        stage = hou.node("/").createNode("lopnet", "stage")

    for child in stage.children():
        child.destroy()

    camera = stage.createNode("camera", "camera1")
    camera.parm("primpath").set("/cameras/camera1")
    camera.parmTuple("t").set((0, 1.2, 6))
    camera.parmTuple("r").set((-15, 0, 0))
    camera.parm("focalLength").set(45)
    camera.parm("horizontalAperture").set(36)
    camera.parm("verticalAperture").set(20.25)

    sphere = stage.createNode("sphere", "sphere1")
    sphere.setInput(0, camera)
    sphere.parm("primpath").set("/World/sphere")
    sphere.parm("radius").set(1)

    dome = stage.createNode("domelight::3.0", "domelight1")
    dome.setInput(0, sphere)
    dome.parm("primpath").set("/lights/domelight1")
    before_primtype = dome.parm("primtype").eval()
    dome.parm("xn__inputsintensity_i0a").set(3 if not texture else 2)
    dome.parmTuple("xn__inputscolor_zta").set((0.35, 0.5, 1.0) if not texture else (1, 1, 1))
    if texture:
        dome.parm("xn__inputstexturefile_r3ah").set(TEXTURE_PATH)
        dome.parm("xn__inputstextureformat_06ah").set("latlong")
    after_primtype = dome.parm("primtype").eval()

    var = stage.createNode("rendervar", "color")
    var.setInput(0, dome)
    var.parm("primpath").set("/Render/Products/Vars/color")
    var.parm("dataType").set("color4f")
    var.parm("sourceName").set("color")
    var.parm("sourceType").set("raw")
    var.parm("xn__driverparametersaovname_jebkd").set("color")

    product = stage.createNode("renderproduct", "product")
    product.setInput(0, var)
    product.parm("primpath").set("/Render/Products/product")
    product.parm("orderedVars").set("/Render/Products/Vars/color")
    product.parm("productName").set(os.path.join(OUT_DIR, name + ".exr"))
    product.parm("productType").set("raster")

    settings = stage.createNode("rendersettings", "settings")
    settings.setInput(0, product)
    settings.parm("primpath").set("/Render/RenderSettings")
    settings.parm("products").set("/Render/Products/product")
    settings.parm("camera").set("/cameras/camera1")
    settings.setDisplayFlag(True)

    return settings, {
        "node_type": dome.type().nameWithCategory(),
        "node_path": dome.path(),
        "primtype_before": before_primtype,
        "primtype_after": after_primtype,
        "texture": texture,
        "texture_path": TEXTURE_PATH if texture else "",
    }


def _export_and_inspect(settings, name, info):
    os.makedirs(OUT_DIR, exist_ok=True)
    usd_path = os.path.join(OUT_DIR, name + ".usda")
    stage = settings.stage()
    stage.Flatten().Export(usd_path)

    exported = Usd.Stage.Open(usd_path)
    light = exported.GetPrimAtPath("/lights/domelight1")
    info.update({
        "usd_path": usd_path,
        "prim_path": str(light.GetPath()) if light else "",
        "prim_type": light.GetTypeName() if light else "",
        "attributes": {},
    })
    if light:
        for attr_name in [
            "inputs:intensity",
            "inputs:exposure",
            "inputs:color",
            "inputs:texture:file",
            "inputs:texture:format",
            "texture:file",
        ]:
            attr = light.GetAttribute(attr_name)
            if attr:
                info["attributes"][attr_name] = str(attr.Get())

    info_path = os.path.join(OUT_DIR, name + ".json")
    with open(info_path, "w") as out:
        json.dump(info, out, indent=2, sort_keys=True)
    print(json.dumps(info, indent=2, sort_keys=True))
    return usd_path


def main():
    for name, texture in [
        ("houdini_domelight_constant", False),
        ("houdini_domelight_texture", True),
    ]:
        settings, info = _build_network(name, texture)
        _export_and_inspect(settings, name, info)


if __name__ == "__main__":
    sys.exit(main())
