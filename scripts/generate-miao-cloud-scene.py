#!/usr/bin/env python3
"""Generate MiaoCloud.mdwall/scene.json from its scene.ini.

Why a script and not a hand-written JSON: the layer geometry mapping is arithmetic, and
arithmetic done by hand is arithmetic nobody checks. The renderer's transform is
scale-about-target-centre, rotate-about-target-centre, then translate, while a sprite
fills the whole render target. So a legacy layer at (x, y, w, h) inside a
(design_width x design_height) space maps to:

    scale   = (w / design_width, h / design_height)
    position = (x - design_width/2  * (1 - scale.x),
                y - design_height/2 * (1 - scale.y))

which is verified by the inverse: composing the sprite's target rect (0,0,dw,dh) with
scale about the centre and then that translation yields exactly (x, y, w, h).

Run:  python3 scripts/generate-miao-cloud-scene.py [--write]
"""
import argparse
import configparser
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
PACKAGE = ROOT / "assets" / "wallpapers" / "MiaoCloud.mdwall"


def read_layers(path):
    """Return (scene config, layers in file order).

    ConfigParser preserves section order, and the .ini's section order *is* the layer
    stacking order. Drawn back-to-front like scene.ini lists them, so the order is
    carried through rather than re-sorted.
    """
    cp = configparser.ConfigParser()
    cp.read(path, encoding="utf-8")
    scene = dict(cp["Scene"])
    layers = [dict(cp[name]) for name in cp.sections() if name.startswith("Layer")]
    if len(layers) != int(scene["layer_count"]):
        raise SystemExit("scene.ini 声明 layer_count=%s,实际有 %d 个 Layer 段"
                         % (scene["layer_count"], len(layers)))
    return scene, layers


def num(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def transform_for(layer, design_w, design_h):
    """Return the exact (unrounded) transform for one legacy layer.

    Rounding happens only at serialisation time — `verify` below composes these raw
    values, and rounding before composing produced a 1e-3 error in the layer width that
    this function's own assertion caught.
    """
    w, h = num(layer["width"]), num(layer["height"])
    x, y = num(layer["x"]), num(layer["y"])
    sx, sy = w / design_w, h / design_h
    # Inverse of the renderer's scale-about-centre: after scaling, the sprite's centre
    # stays put, so the required translation is the offset between where each edge lands
    # and where the layer wants it.
    px = x - (design_w / 2.0) * (1.0 - sx)
    py = y - (design_h / 2.0) * (1.0 - sy)
    return {
        "position": [px, py],
        "scale": [sx, sy],
        "rotation": 0.0,
        # The legacy renderer puts the layer's opacity on the layer; the scene runtime
        # carries it on the Transform, which the renderer multiplies down the node chain.
        "opacity": num(layer["opacity"], 1.0),
    }


def verify(layers, transforms, design_w, design_h):
    """Re-compose each transformed sprite and assert it lands on the legacy rect."""
    for layer, tr in zip(layers, transforms):
        sx, sy = tr["scale"]
        px, py = tr["position"]
        want = (num(layer["x"]), num(layer["y"]), num(layer["width"]), num(layer["height"]))
        # sprite rect (0,0,dw,dh) -> scale about centre (dw/2, dh/2) -> translate
        edges_x = ((design_w / 2) * (1 - sx) + px, (design_w / 2) * (1 + sx) + px)
        edges_y = ((design_h / 2) * (1 - sy) + py, (design_h / 2) * (1 + sy) + py)
        got = (edges_x[0], edges_y[0], edges_x[1] - edges_x[0], edges_y[1] - edges_y[0])
        assert all(abs(a - b) < 1e-4 for a, b in zip(want, got)), (
            "%s: 合成结果 %s 与 scene.ini 的 %s 不符" % (layer["name"], got, want))


def build():
    cp_layers = read_layers(PACKAGE / "scene.ini")
    scene_cfg, layers = cp_layers
    design_w, design_h = num(scene_cfg["design_width"]), num(scene_cfg["design_height"])
    transforms = [transform_for(l, design_w, design_h) for l in layers]
    verify(layers, transforms, design_w, design_h)

    nodes = [{
        "id": "node://root",
        "name": "Root",
        "parentId": "",
        "enabled": True,
        "components": [{
            "id": "component://root/transform",
            "kind": "transform",
            "properties": [
                {"name": "position", "type": "vec2", "default": [0.0, 0.0]},
                {"name": "scale", "type": "vec2", "default": [1.0, 1.0]},
                {"name": "rotation", "type": "float", "default": 0.0},
                {"name": "opacity", "type": "float", "default": 1.0},
            ],
        }],
    }]
    assets = []
    for index, (layer, tr) in enumerate(zip(layers, transforms)):
        node_id = "node://layer/%s" % layer["name"]
        asset_id = "asset://layer/%s/image" % layer["name"]
        assets.append({
            "id": asset_id,
            "type": "image",
            "source": layer["file"].replace("\\", "/"),
        })
        nodes.append({
            "id": node_id,
            "name": layer["name"],
            "parentId": "node://root",
            "enabled": True,
            "components": [
                {
                    "id": "component://layer/%d/transform" % index,
                    "kind": "transform",
                    "properties": [
                        {"name": "position", "type": "vec2", "default": tr["position"]},
                        {"name": "scale", "type": "vec2", "default": tr["scale"]},
                        {"name": "rotation", "type": "float", "default": tr["rotation"]},
                        {"name": "opacity", "type": "float", "default": tr["opacity"]},
                    ],
                },
                {
                    "id": "component://layer/%d/sprite" % index,
                    "kind": "spriteRenderer",
                    "properties": [
                        {"name": "opacity", "type": "float", "default": 1.0},
                        {"name": "tint", "type": "color", "default": [1.0, 1.0, 1.0, 1.0]},
                        {"name": "cornerRadius", "type": "float", "default": 0.0},
                        # assetReference 的默认值是裸字符串(asset id),不是对象 ——
                        # SceneTextureFixture 用一条断言钉住了这个形状。
                        {"name": "texture", "type": "assetReference", "default": asset_id},
                    ],
                },
            ],
        })

    return {
        "schema": 1,
        "id": "scene://builtin/miao-cloud",
        "kind": "wallpaper",
        "profile": "wallpaper",
        "rootNodeId": "node://root",
        "nodes": nodes,
        "assets": assets,
        "shaders": [],
        "materials": [],
        "inputs": [{"id": "input://frame/time", "type": "float", "default": 0.0}],
        "bindings": [],
        # 动画刻意为空:scene.ini 的六种动画都是解析式正弦(drift 还让 y 轴用
        # speed*0.77 的另一个周期),而场景动画是线性插值的关键帧轨。要在
        # "完全复现"与"循环处连续"之间做取舍,属于要看真实桌面效果才能定的决定,
        # 不能在这里替用户猜。见 docs/TODO.md P0-4。
        "animations": [],
        "postProcesses": [],
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--write", action="store_true")
    args = ap.parse_args()
    doc = build()
    text = json.dumps(doc, ensure_ascii=False, indent=2) + "\n"
    if not args.write:
        sys.stdout.write(text)
        return 0
    target = PACKAGE / "scene.json"
    target.write_text(text, encoding="utf-8")
    print("已写入 %s" % target)
    print("节点 %d 个(1 root + %d 图层),资产 %d 个" % (len(doc["nodes"]), len(doc["nodes"]) - 1, len(doc["assets"])))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
