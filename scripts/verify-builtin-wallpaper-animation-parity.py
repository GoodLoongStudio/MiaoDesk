#!/usr/bin/env python3
"""逐点复核内置壁纸的 scene.json 动画是否复现 scene.ini 的解析式运动。

为什么需要它:动画从"解析式正弦"迁到"线性插值关键帧轨"是一次**有损**迁移,
损失量必须是个数,不能是注释里的一句"亚像素"。这个脚本把两个源都读进来,
按引擎的真实语义求值,再逐点比。

覆盖**三个**内置壁纸。2026-09-23 之前只复核 MiaoCloud —— 那时另外两个包的
scene.json 还是空壳,没有可校验的产物。两个包迁完之后只复核一个,
等于让"保真"对三分之二的迁移不成立,而报告照样全绿。

引擎语义(摘自 src/content/runtime/MiaoSceneRuntime.cpp,不是推测):
  AnimationLocalTime  PingPong: period = duration*2, 折回 [0, duration]
                        Loop:     fmod(time, duration)
  ApplyAnimation       局部时间 <= 首帧 → 停在首帧;>= 末帧 → 停在末帧;
                        否则在相邻两帧间线性插值,并用 from.easing
  ApplyEasing          linear → t;easeInOut → 2t²(t<0.5) 否则 1-(2-2t)²/2

scene.ini 的公式摘自 src/include/miaodesk/LayeredSceneRenderer.h:229-244:
  wave = sin(t*speed + phase)
  drift/float  x += wave*amplitudeX
               y += cos(t*speed*0.77 + phase)*amplitudeY
  breathe      scale += wave*scaleAmplitude
               y += cos(t*speed + phase)*amplitudeY
  sway         x += wave*amplitudeX
               y += cos(t*speed*0.81 + phase)*amplitudeY
               angle = wave*rotationAmplitude
  blink        angle = wave*rotationAmplitude        (speed=0 → 恒定)
               cycle = fmod(max(0, t+phase), blinkInterval)
               cycle > blinkDuration 时 opacity = 0

`float` 与 `drift` 是**同一个分支、同一条公式**(LayeredSceneRenderer.h:231),
所以 MysticMoon 的 `float` 不需要第五套迁移规则。

李萨如的两根轴频率不同,而场景一条轨只能动一个完整属性(position 是 vec2、
没有 .x/.y 寻址),所以生成器把 y 轴放到父节点、x 轴留在子节点,靠节点变换
连乘合成。这里按同样的连乘来比。
"""
import configparser
import json
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
WALLPAPERS = ROOT / "assets" / "wallpapers"
PACKAGES = ("MiaoCloud", "NeonCity", "MysticMoon")


def paths(name):
    package = WALLPAPERS / (name + ".mdwall")
    return package / "scene.json", package / "scene.ini"

# 均匀采样 + 线性插值的解析上界:A*(1-cos(pi/(N-1)))。
SAMPLES = 16
BOUND = 1.0 - math.cos(math.pi / (SAMPLES - 1))
# 第二项:采样值按 6 位小数写入(见 generate-miao-cloud-scene.py 的 rounded()),
# 每个值最多引入 0.5e-6 的量化误差,而轨值与解析值各一次,所以是 1e-6。
# 少了这一项, breathe 的最大误差会刚好压在解析上界上并被判 FAIL ——
# 那不是迁移变差了,是上界算漏了一项。
ROUNDING = 1e-6


def fail(msg):
    print("❌ " + msg)
    sys.exit(1)


def read_ini(ini_path):
    """Layers in file order.

    read_string on text we read ourselves, not cfg.read(path): read() silently
    ignores a file it cannot open and returns the ones it did read, so a wrong path
    here yields an empty layer list that reads like "this package has no layers".
    """
    cfg = configparser.ConfigParser()
    cfg.read_string(ini_path.read_text(encoding="utf-8"), source=str(ini_path))
    layers = []
    for section in cfg.sections():
        if not section.startswith("Layer"):
            continue
        e = cfg[section]
        layers.append({k: e.get(k, "") for k in e})
    return cfg, layers


def local_time(t, duration, loop):
    if loop == "loop":
        w = math.fmod(t, duration)
        return w if w >= 0 else w + duration
    if loop == "pingpong":
        p = duration * 2.0
        w = math.fmod(t, p)
        if w < 0:
            w += p
        return w if w <= duration else p - w
    return max(0.0, min(t, duration))


def easing(u, kind):
    u = min(1.0, max(0.0, u))
    if kind == "linear":
        return u
    if kind == "easeinout":
        return 2 * u * u if u < 0.5 else 1 - ((-2 * u + 2) ** 2) / 2.0
    if kind == "easein":
        return u * u
    if kind == "easeout":
        return 1 - (1 - u) * (1 - u)
    return u


def eval_track(track, t):
    keys = track["keyframes"]
    duration = float(track["duration"])
    lt = local_time(t, duration, track.get("loop", "loop"))
    first = float(keys[0]["time"])
    if lt <= first:
        return keys[0]["value"]
    last = float(keys[-1]["time"])
    if lt >= last:
        return keys[-1]["value"]
    for i in range(len(keys) - 1):
        a, b = keys[i], keys[i + 1]
        ta, tb = float(a["time"]), float(b["time"])
        if lt <= tb:
            u = 0.0 if tb <= ta else (lt - ta) / (tb - ta)
            e = easing(u, a.get("easing", "linear"))
            va, vb = a["value"], b["value"]
            if isinstance(va, list):
                return [x + (y - x) * e for x, y in zip(va, vb)]
            return va + (vb - va) * e
    return keys[-1]["value"]


def num(value, default=0.0):
    try:
        return float(value)
    except (TypeError, ValueError):
        return default


def check_package(package):
    """Return the failure count for one package.

    The parameter is `package`, not `name`: the per-layer loop below binds
    `name = layer["name"]`, and a parameter with that name would be silently
    reassigned by it. It happens to be harmless here because paths()/read_ini()
    are called before the loop, which is precisely why it would have been hard to
    notice later.
    """
    scene_path, ini_path = paths(package)
    ini_cfg, layers = read_ini(ini_path)
    doc = json.loads(scene_path.read_text(encoding="utf-8"))
    tracks = {a["id"]: a for a in doc.get("animations", [])}
    by_target = {}
    for a in doc.get("animations", []):
        by_target.setdefault(a["target"]["componentId"], []).append(a)

    animated = [l for l in layers if l.get("animation") not in ("", "none")]
    print(f"scene.ini 带动画的层: {len(animated)} 个({', '.join(l['name'] for l in animated)})")
    print(f"scene.json 动画轨:     {len(doc.get('animations', []))} 条")
    print()

    failures = 0
    for layer in animated:
        name = layer["name"]
        speed = num(layer.get("speed"))
        phase = num(layer.get("phase"))
        ax = num(layer.get("amplitude_x"))
        ay = num(layer.get("amplitude_y"))
        kind = layer["animation"]

        # 找到这一层的 x / y / angle / scale / opacity 轨
        transform_id = None
        sprite_id = None
        for node in doc["nodes"]:
            if node["id"] == "node://layer/%s" % name:
                for c in node["components"]:
                    if c["kind"] == "transform":
                        transform_id = c["id"]
                    elif c["kind"] == "spriteRenderer":
                        sprite_id = c["id"]
        assert transform_id, name

        def get(component, prop):
            for a in by_target.get(component, []):
                if a["target"]["propertyName"] == prop:
                    return a
            return None

        axis_y_node = "node://layer/%s/axis-y" % name
        axis_y_transform = None
        for node in doc["nodes"]:
            if node["id"] == axis_y_node:
                for c in node["components"]:
                    if c["kind"] == "transform":
                        axis_y_transform = c["id"]
        track_y = get(axis_y_transform, "position") if axis_y_transform else None
        track_x = get(transform_id, "position")
        track_angle = get(transform_id, "rotation")
        track_scale = get(transform_id, "scale")
        track_opacity = get(sprite_id, "opacity")

        # legacy 的静态偏移:scene.json 里 transform 的 position/scale 默认值
        static_pos = static_scale = None
        for node in doc["nodes"]:
            if node["id"] == "node://layer/%s" % name:
                for c in node["components"]:
                    if c["kind"] == "transform":
                        static_pos = [p["default"] for p in c["properties"] if p["name"] == "position"][0]
                        static_scale = [p["default"] for p in c["properties"] if p["name"] == "scale"][0]

        # 采样窗口:取所有相关周期的最小公倍的近似 —— 直接用较长周期的 3 倍
        periods = []
        for tr in (track_x, track_y, track_angle, track_scale):
            if tr:
                periods.append(float(tr["duration"]))
        window = max(periods) * 2 if periods else 1.0
        n = 24000

        worst = {"x": 0.0, "y": 0.0, "angle": 0.0, "scale": 0.0, "opacity": 0.0}
        for k in range(n + 1):
            t = window * k / n
            if kind in ("drift", "float"):
                lx = math.sin(t * speed + phase) * ax
                ly = math.cos(t * speed * 0.77 + phase) * ay
            elif kind == "breathe":
                lx = 0.0
                ly = math.cos(t * speed + phase) * ay
            elif kind == "sway":
                lx = math.sin(t * speed + phase) * ax
                ly = math.cos(t * speed * 0.81 + phase) * ay
            else:
                lx = ly = 0.0

            # 合成:父节点给 y,子节点给 x。legacy 的 x/y 是"叠加在静态位置上",
            # 场景轨是绝对赋值,所以要比的是 轨值 - 静态值。
            if track_x:
                v = eval_track(track_x, t)
                worst["x"] = max(worst["x"], abs((v[0] - static_pos[0]) - lx))
                worst["x"] = max(worst["x"], abs((v[1] - static_pos[1]) - (ly if not track_y else 0.0)))
            if track_y:
                v = eval_track(track_y, t)
                worst["y"] = max(worst["y"], abs((v[1] - 0.0) - ly))
            if track_angle and kind != "blink":
                worst["angle"] = max(worst["angle"], abs(eval_track(track_angle, t) - math.sin(t * speed + phase) * num(layer.get("rotation_amplitude"))))
            if track_scale:
                sa = num(layer.get("scale_amplitude"))
                want = 1.0 + math.sin(t * speed + phase) * sa
                got = eval_track(track_scale, t)
                worst["scale"] = max(worst["scale"], abs(got[0] / static_scale[0] - want))
            if track_opacity:
                interval = max(0.6, num(layer.get("blink_interval"), 4.8))
                dur = min(max(num(layer.get("blink_duration"), 0.16), 0.04), 0.5)
                cycle = math.fmod(max(0.0, t + phase), interval)
                want = 0.0 if cycle > dur else 1.0
                worst["opacity"] = max(worst["opacity"], abs(eval_track(track_opacity, t) - want))

        amp = max(ax, ay, abs(num(layer.get("rotation_amplitude"))), 1e-9)
        print(f"[{name}] {kind}")
        # 只报这一层**实际存在**的轴。第一版无条件报 opacity,于是没有不透明度轨的
        # drift/sway/breathe 也打印"opacity 0.0000 ok" —— 一个稳定的假绿:
        # 它把"这条没测"显示成"这条测过了而且精确"。
        present = {"x": track_x, "y": track_y, "angle": track_angle and kind != "blink",
                   "scale": track_scale, "opacity": track_opacity}
        for key, label in (("x", "x"), ("y", "y"), ("angle", "angle"), ("scale", "scale(相对)"), ("opacity", "opacity")):
            if not present[key]:
                continue
            if worst[key] > 0.0 or key == "opacity":
                bound = BOUND * (amp if key in ("x", "y", "angle") else 1.0)
                note = ""
                if key == "opacity":
                    note = " (方波,精确)" if worst[key] < 1e-9 else " (方波!)"
                    ok = worst[key] < 1e-9
                else:
                    limit = bound + ROUNDING
                    ok = worst[key] <= limit
                    note = f" (上界 {limit:.4f} = {bound:.4f} + {ROUNDING:g})"
                print(f"    {label:<16} 最大误差 {worst[key]:.4f}{note}  {'ok' if ok else 'FAIL'}")
                if not ok:
                    failures += 1
        print()

    if failures:
        print(f"❌ {failures} 项超出误差上界")
    return failures


def main():
    total = 0
    for name in PACKAGES:
        print(f"\n{'=' * 62}\n{name}\n{'=' * 62}")
        total += check_package(name)
    if total:
        print(f"\n❌ 三个包合计 {total} 项超出误差上界")
        return 1
    print(f"\n✅ 三个包的迁移动画全部在解析上界 A*(1-cos(pi/{SAMPLES-1})) "
          f"= {BOUND*100:.2f}% 幅值之内")
    print("   (最大幅值 14px 上约 0.306px —— 亚像素)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
