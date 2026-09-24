#!/usr/bin/env python3
"""Rebuild a builtin wallpaper's scene.json from its frozen legacy migration fixture.

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

This handles **all three** builtin wallpapers, not just MiaoCloud. Writing a second
generator for NeonCity / MysticMoon would have duplicated the transform arithmetic and
the animation frequency table (drift 0.77 / sway 0.81 / breathe 1.0) — the same
"two backends, two copies of one rule" shape the repo has already been bitten by. The
two remaining packages use only forms MiaoCloud already covered: note that scene.ini's
`float` and `drift` are the *same branch* in LayeredSceneRenderer.h:231-233, so MysticMoon's
`float` is not a new animation type.

Run:  python3 scripts/generate-miao-cloud-scene.py [--package NAME|all] [--write|--check]
"""
import math
import argparse
import configparser
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
LEGACY_FIXTURES = ROOT / "tests" / "fixtures" / "legacy-wallpaper-scenes"

# scene id / animation id prefix per package. These are part of the on-disk format
# (the empty-shell scene.json files already used neon-city / mystic-moon), so they are
# fixed here rather than derived — a rename would change every animation id in the file.
SLUGS = {"MiaoCloud": "miao-cloud", "NeonCity": "neon-city", "MysticMoon": "mystic-moon"}


def package_dir(name):
    return ROOT / "assets" / "wallpapers" / (name + ".mdwall")


def legacy_scene_path(name):
    # scene.ini no longer ships in the .mdwall package. It is frozen under tests/
    # solely as migration evidence so the canonical scene.json can be compared with
    # the exact legacy composition that preceded it.
    return LEGACY_FIXTURES / name / "scene.ini"


def read_layers(path):
    """Return (scene config, layers in file order).

    ConfigParser preserves section order, and the .ini's section order *is* the layer
    stacking order. Drawn back-to-front like scene.ini lists them, so the order is
    carried through rather than re-sorted.

    read_string on text we read ourselves, **not** cp.read(path): read() silently
    ignores a file it cannot open and returns the list it did manage to read, so a
    wrong path here yields an empty layer list — which then reports as
    "layer_count says N but there are 0 Layer sections", pointing the reader at the
    .ini's contents rather than at the path that was wrong. Same defect, same fix, as
    in scripts/generate-builtin-wallpaper-art.py.
    """
    text = path.read_text(encoding="utf-8")
    cp = configparser.ConfigParser()
    cp.read_string(text, source=str(path))
    scene = dict(cp["Scene"])
    layers = [dict(cp[name]) for name in cp.sections() if name.startswith("Layer")]
    if len(layers) != int(scene["layer_count"]):
        raise SystemExit("scene.ini 声明 layer_count=%s,实际有 %d 个 Layer 段"
                         % (scene["layer_count"], len(layers)))
    # The whole mapping, not just [Scene]: [Particles] is a separate section and the
    # particle emitters are built from it. Returning only [Scene] here silently gave
    # particle_emitters() nothing to read, and an empty emitter list looks exactly
    # like "this package declares no particles".
    return scene, layers, cp


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


# --- 动画迁移 ---------------------------------------------------------------
# scene.ini 的动画是解析式的(x 走 sin、y 走 cos,而且 y 的频率是 speed*0.77 或
# speed*0.81 —— 见 LayeredSceneRenderer.h:229-244),场景动画是线性插值的关键帧轨。
# 一条轨只能动一个完整属性,而 position 是 vec2、没有 .x/.y 寻址,所以一个
# Lissajous 的两根轴必须拆到父子两个节点上:节点变换沿 parentId 链连乘,
# 父节点动 y、子节点动 x,合成结果就是原来的两频李萨如图。
#
# 采样数 16(即 15 个区间)不是拍的:均匀采样 + 线性插值的最大误差是
# A*(1-cos(pi/15)) = A*0.0219,对最大的 drift 幅值 14px 是 0.306px —— 亚像素。
# 这个数由 scripts/verify-builtin-wallpaper-animation-parity.py 逐点复核,不是注释里的口号。
SAMPLES_PER_PERIOD = 16


def period_of(speed):
    return 2.0 * math.pi / speed if speed > 0 else 0.0


def rounded(value, places=6):
    """把采样值按固定小数位写进 scene.json。

    为什么必须 round:关键帧的值来自 math.sin / math.cos,而**超越函数的结果
    依赖平台的 libm**。glibc 与 macOS 的 sin 在末位可能不同,json.dumps 会
    把这个末位差原样写进产物 —— 于是"重新生成与磁盘逐字节一致"这道门在
    另一个平台上必红,而差异只是 1e-16 的相对量。
    2026-09-22 就这么红了两轮:在 macOS 上生成、在 Linux CI 上校验,报
    "不一致",而我本机复现不出来,因为本机就是生成它的那一边。

    小数位取 6:本项目动画幅值最大 14px,6 位是 1e-6 px,远小于声明的
    A*(1-cos(pi/15)) = 0.306px 误差上界。乘加除是 IEEE 精确舍入、与平台
    无关的,所以 time 本来不需要 round;round 它是为了让同一份产物在任何
    机器上都逐字节相同。
    """
    if isinstance(value, list):
        return [round(v, places) for v in value]
    return round(value, places)


def track(component_id, prop, fn, period, name, slug):
    """一个周期内均匀采样,Linear,loop。"""
    keys = []
    for i in range(SAMPLES_PER_PERIOD):
        t = period * i / (SAMPLES_PER_PERIOD - 1)
        keys.append({"time": t, "value": rounded(fn(t)), "easing": "linear"})
    return {
        "id": "animation://%s/%s" % (slug, name),
        "target": {"componentId": component_id, "propertyName": prop},
        "enabled": True,
        "loop": "loop",
        "duration": period,
        "keyframes": keys,
    }


def animation_plan(layer):
    """按 scene.ini 的 animation 名给出每根轴/每个属性的运动函数。

    返回 (x_fn, y_fn, angle_fn, scale_fn, extra)——extra 是不随关键帧走的静态量。
    频率乘数直接抄 LayeredSceneRenderer,不猜:drift 0.77、sway 0.81、breathe 1.0。
    """
    kind = layer["animation"]
    speed = num(layer.get("speed"), 0.0)
    phase = num(layer.get("phase"), 0.0)
    ax = num(layer.get("amplitude_x"), 0.0)
    ay = num(layer.get("amplitude_y"), 0.0)
    if kind in ("none", "") or speed <= 0 and kind != "blink":
        return None

    if kind in ("drift", "float"):
        return {
            "x": lambda t: math.sin(t * speed + phase) * ax,
            "y": lambda t: math.cos(t * speed * 0.77 + phase) * ay,
            "angle": None, "scale": None,
        }
    if kind == "breathe":
        sa = num(layer.get("scale_amplitude"), 0.0)
        return {
            "x": None,
            "y": lambda t: math.cos(t * speed + phase) * ay,
            "angle": None,
            "scale": lambda t: 1.0 + math.sin(t * speed + phase) * sa,
        }
    if kind == "sway":
        ra = num(layer.get("rotation_amplitude"), 0.0)
        return {
            "x": lambda t: math.sin(t * speed + phase) * ax,
            "y": lambda t: math.cos(t * speed * 0.81 + phase) * ay,
            "angle": lambda t: math.sin(t * speed + phase) * ra,
            "scale": None,
        }
    return None


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


def particle_emitters(cfg, slug):
    """ Declarative emitters from scene.ini's [Particles].

    The legacy renderer draws three *stateless analytic fields* — sparkles, a comet
    trail, and falling petals — evaluated from the index every frame. Those map onto
    the analytic emitter modes, one emitter per field, carrying only what [Particles]
    actually declares. clamps match LayeredSceneRenderer's own reads so a scene.ini
    value outside the range produces what legacy produced, not what the text said.
    """
    try:
        particles = dict(cfg["Particles"])
    except KeyError:
        return []

    def clamp(value, low, high, default):
        if value is None:
            return default
        try:
            parsed = float(value)
        except (TypeError, ValueError):
            return default
        return max(low, min(high, parsed))

    sparkles = int(clamp(particles.get("sparkle_count"), 0, 96, 22))
    petals = int(clamp(particles.get("petal_count"), 0, 64, 12))
    opacity = clamp(particles.get("opacity"), 0.0, 1.0, 0.46)
    comets = int(clamp(particles.get("flow_count"), 0, 12, 0))
    comet_opacity = clamp(particles.get("flow_opacity"), 0.0, 1.0, 0.0)
    comet_speed = clamp(particles.get("flow_speed"), 0.005, 0.5, 0.065)

    emitters = []
    if sparkles > 0:
        emitters.append({
            "id": "particle://%s/sparkle" % slug,
            "enabled": True,
            "mode": "sparkle",
            # The legacy sparkle's fixed tint. Declared so an author can recolour the
            # field later without the formula knowing about it.
            "analyticColor": [1.0, 0.92, 0.78, 1.0],
            "analyticCount": sparkles,
            "analyticOpacity": round(opacity, 6),
        })
    if comets > 0:
        emitters.append({
            "id": "particle://%s/comet" % slug,
            "enabled": True,
            "mode": "cometTrail",
            "analyticColor": [1.0, 0.84, 0.98, 1.0],
            "analyticCount": comets,
            "analyticOpacity": round(comet_opacity, 6),
            "analyticSpeed": round(comet_speed, 6),
        })
    if petals > 0:
        emitters.append({
            "id": "particle://%s/petal" % slug,
            "enabled": True,
            "mode": "petalFall",
            "analyticColor": [1.0, 0.66, 0.83, 1.0],
            "analyticCount": petals,
            "analyticOpacity": round(opacity, 6),
        })
    return emitters


def build(slug, name):
    scene_cfg, layers, ini_cfg = read_layers(legacy_scene_path(name))
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
    animations = []

    for index, (layer, tr) in enumerate(zip(layers, transforms)):
        node_id = "node://layer/%s" % layer["name"]
        asset_id = "asset://layer/%s/image" % layer["name"]
        transform_id = "component://layer/%d/transform" % index
        sprite_id = "component://layer/%d/sprite" % index
        assets.append({
            "id": asset_id,
            "type": "image",
            "source": layer["file"].replace("\\", "/"),
        })

        # scene.ini 的 static 值。动画轨是"绝对赋值"(MiaoSceneRuntime::ApplyAnimation
        # 走 properties_.Set),不是叠加,所以关键帧里必须带上静态偏移。
        static_position = list(tr["position"])
        static_scale = list(tr["scale"])
        static_rotation = tr["rotation"]
        static_sprite_opacity = 1.0
        parent_id = "node://root"

        plan = animation_plan(layer)
        if layer["animation"] == "blink":
            # blink 的 rotation 是**固定**的:speed=0 使 wave=sin(phase)=sin(pi/2)=1,
            # 于是 angle = rotation_amplitude 恒定(LayeredSceneRenderer.h:243-245)。
            # 它不是一个动画,是一个静态角度;写成动画反而多一条永不变的轨。
            static_rotation = num(layer.get("rotation_amplitude"), 0.0)
            interval = max(0.6, num(layer.get("blink_interval"), 4.8))
            duration = min(max(num(layer.get("blink_duration"), 0.16), 0.04), 0.5)
            phase = num(layer.get("phase"), 0.0)
            # 不透明度是方波。legacy 的相位在 cycle 里:cycle=fmod(t+phase,interval),
            # 于是可见窗口相对 t=0 平移了 phase。动画轨没有 phase 字段,只能用
            # 关键帧的**时刻**把窗口摆回去:
            #   fmod(t+phase,interval) <= duration
            #     ⟺ 局部时间 lt ∈ [interval-phase, interval+phase... ]
            #   解得 lt ∈ [interval-phase, interval-phase+duration]
            # 两侧的台阶各占一帧(1/240s,即文档允许的最高帧率的倒数):
            # 关键帧时间必须严格递增(ApplyAnimation 对 span<=0 直接报错),
            # 所以零宽台阶只能用一帧的斜坡表达,时间误差 ≤ 4.2ms。
            edge = 1.0 / 240.0
            start = max(0.0, interval - phase)
            stop = min(interval, interval - phase + duration)
            keys = [{"time": 0.0, "value": 0.0, "easing": "linear"}]
            keys.append({"time": start, "value": 0.0, "easing": "linear"})
            keys.append({"time": start + edge, "value": 1.0, "easing": "linear"})
            keys.append({"time": stop, "value": 1.0, "easing": "linear"})
            keys.append({"time": min(interval, stop + edge), "value": 0.0, "easing": "linear"})
            # 去掉时间相同或倒退的键(极端参数下会出现)
            filtered = [keys[0]]
            for k in keys[1:]:
                if k["time"] > filtered[-1]["time"]:
                    filtered.append(k)
            animations.append({
                "id": "animation://%s/%s-blink" % (slug, layer["name"]),
                "target": {"componentId": sprite_id, "propertyName": "opacity"},
                "enabled": True,
                "loop": "loop",
                "duration": interval,
                "keyframes": filtered,
            })

        if plan:
            x_fn, y_fn, angle_fn, scale_fn = plan["x"], plan["y"], plan["angle"], plan["scale"]
            speed = num(layer.get("speed"), 0.0)

            # y 轴与 x 轴频率不同(drift 0.77、sway 0.81),必须拆到父节点:
            # 一条轨动一个完整属性,而 position 是 vec2、没有 .x/.y 寻址。
            if y_fn is not None:
                axis_id = "node://layer/%s/axis-y" % layer["name"]
                axis_transform_id = "component://layer/%d/axis-y/transform" % index
                # y 轴频率乘数因动画而异:LayeredSceneRenderer 里 drift 用 0.77、
                # sway 用 0.81、breathe 用 1.0(就是 speed 本身)。第一版把非 drift
                # 一律按 0.81 算,于是 breathe 的周期从 7.66s 变成 9.46s ——
                # 是 parity 检查把它抓出来的(误差 8.9px,是上界的 90 倍)。
                y_multiplier = {"drift": 0.77, "float": 0.77, "sway": 0.81}.get(
                    layer["animation"], 1.0)
                period_y = period_of(speed * y_multiplier)
                nodes.append({
                    "id": axis_id,
                    "name": "%s drift axis" % layer["name"],
                    "parentId": "node://root",
                    "enabled": True,
                    "components": [{
                        "id": axis_transform_id,
                        "kind": "transform",
                        "properties": [
                            {"name": "position", "type": "vec2", "default": [0.0, 0.0]},
                            {"name": "scale", "type": "vec2", "default": [1.0, 1.0]},
                            {"name": "rotation", "type": "float", "default": 0.0},
                            {"name": "opacity", "type": "float", "default": 1.0},
                        ],
                    }],
                })
                # 父节点的 position.x 恒为 0(该轴不横向移动),只有 y 跟着走。
                animations.append(track(
                    axis_transform_id, "position",
                    lambda t, f=y_fn: [0.0, f(t)],
                    period_y, "%s-axis-y" % layer["name"], slug))
                parent_id = axis_id

            if x_fn is not None:
                period_x = period_of(speed)
                bx, by = static_position
                animations.append(track(
                    transform_id, "position",
                    lambda t, f=x_fn, bx=bx, by=by: [bx + f(t), by],
                    period_x, "%s-axis-x" % layer["name"], slug))

            if angle_fn is not None:
                period_a = period_of(speed)
                animations.append(track(
                    transform_id, "rotation",
                    lambda t, f=angle_fn: f(t),
                    period_a, "%s-angle" % layer["name"], slug))

            if scale_fn is not None:
                period_s = period_of(speed)
                sx, sy = static_scale
                # legacy 是 layerScale(=1+wave*scaleAmplitude) 乘在原始尺寸上,
                # 所以这里要乘 static_scale,不是替换它。
                animations.append(track(
                    transform_id, "scale",
                    lambda t, f=scale_fn, sx=sx, sy=sy: [sx * f(t), sy * f(t)],
                    period_s, "%s-scale" % layer["name"], slug))

        nodes.append({
            "id": node_id,
            "name": layer["name"],
            "parentId": parent_id,
            "enabled": True,
            "components": [
                {
                    "id": transform_id,
                    "kind": "transform",
                    "properties": [
                        {"name": "position", "type": "vec2", "default": static_position},
                        {"name": "scale", "type": "vec2", "default": static_scale},
                        {"name": "rotation", "type": "float", "default": static_rotation},
                        {"name": "opacity", "type": "float", "default": tr["opacity"]},
                    ],
                },
                {
                    "id": sprite_id,
                    "kind": "spriteRenderer",
                    "properties": [
                        {"name": "opacity", "type": "float", "default": static_sprite_opacity},
                        {"name": "tint", "type": "color", "default": [1.0, 1.0, 1.0, 1.0]},
                        {"name": "cornerRadius", "type": "float", "default": 0.0},
                        {"name": "texture", "type": "assetReference", "default": asset_id},
                    ],
                },
            ],
        })

    return {
        "schema": 1,
        "id": "scene://builtin/%s" % slug,
        "kind": "wallpaper",
        "profile": "wallpaper",
        "rootNodeId": "node://root",
        "nodes": nodes,
        "assets": assets,
        "shaders": [],
        "materials": [],
        "inputs": [{"id": "input://frame/time", "type": "float", "default": 0.0}],
        "bindings": [],
        # 动画已迁移(2026-09-22)。六种 scene.ini 动画里五种落到关键帧轨,
        # 关键帧在正弦极值/等分点上按 16 采样/周期生成;Lissajous 的两根轴
        # 因为频率不同(drift 0.77、sway 0.81)而拆到父子两个节点,靠变换连乘合成。
        # 最大误差 A*(1-cos(pi/15)),即最大幅值 14px 上 0.306px —— 由
        # scripts/verify-builtin-wallpaper-animation-parity.py 逐点复核。
        "animations": animations,
        # 粒子已迁移(2026-09-23)。此前这里刻意留空,理由是"legacy 的粒子是过程式
        # 生成的,没有每粒子的声明式来源" —— 那个理由只对**有状态发射器**成立。
        # 事实上 legacy 的三种粒子全是**无状态解析场**:每帧按索引现算,不存任何
        # 跨帧状态。所以它不需要"每粒子初速度/寿命",它需要的是另一种 emitter 模型。
        #
        # 于是 ParticleEmitterDefinition 多了 mode(Simulated / Sparkle / CometTrail /
        # PetalFall),解析场的公式在 MiaoAnalyticParticleField.cpp 里**只存在一份**,
        # 两个渲染后端都调它。这里声明的只有 scene.ini 真正暴露过的那些量:
        # 计数、不透明度、以及彗星的流速。配方与常数一律不暴露 —— 0.78 的场高、
        # i%5 的脉动分频、11 段尾迹都是设计常量,不是创作旋钮。
        #
        # clamp 与 LayeredSceneRenderer 读 [Particles] 时的那几处一致(96/64/12、
        # opacity [0,1]、flow_speed [0.005,0.5]):scene.ini 写 0.001 时 legacy 实际
        # 用 0.005,scene.json 若照抄 0.001 就已分叉。
        "particleEmitters": particle_emitters(ini_cfg, slug),
        "postProcesses": [],
    }


def main():
    ap = argparse.ArgumentParser()
    choices = sorted(SLUGS) + ["all"]
    ap.add_argument("--package", default="all", choices=choices,
                    help="生成/校验哪一个包(默认 all = 三个内置壁纸)")
    ap.add_argument("--write", action="store_true", help="写回 scene.json")
    ap.add_argument("--check", action="store_true",
                    help="只比对:重新生成并与磁盘上的 scene.json 逐字节比较,不一致则非零退出")
    args = ap.parse_args()
    if args.write and args.check:
        ap.error("--write 与 --check 互斥")

    names = sorted(SLUGS) if args.package == "all" else [args.package]
    failures = []
    for name in names:
        slug = SLUGS[name]
        # build() 里包含对每层的逆合成 assert,所以三种模式都会先验几何。
        doc = build(slug, name)
        text = json.dumps(doc, ensure_ascii=False, indent=2) + "\n"
        target = package_dir(name) / "scene.json"

        if args.check:
            try:
                on_disk = target.read_text(encoding="utf-8")
            except OSError as exc:
                failures.append("❌ 读不到 %s:%s" % (target, exc))
                continue
            if on_disk != text:
                # 把差异本身打出来。上一版只说"不一致",而定位它要的正是差异在哪一行 ——
                # 2026-09-22 这道门在 Linux CI 上红了一轮,我手上只有"不一致"三个字,
                # 本机却复现不出来,等于没有信息。
                import difflib
                diff = list(difflib.unified_diff(
                    on_disk.splitlines(), text.splitlines(),
                    "scene.json(磁盘)", "冻结的 legacy fixture 重新生成", lineterm="", n=1))
                failures.append(
                    "❌ %s 与重新生成的结果不一致(%d 行不同,下面是最多 24 行差异)。\n"
                    "  tests/fixtures 下的 scene.ini 是迁移基准;运行时唯一入口是 scene.json。\n"
                    "  手改 scene.json 会让二者悄悄分叉 —— 而分叉的后果是图层位置\n"
                    "  与源不符,却没有任何测试会报错。\n"
                    "  若这是有意的视觉改动,请同时更新迁移基准或移除对应旧保真断言;否则用 python3 %s --package %s --write 恢复。\n"
                    "  或者确认这次偏离是有意的,并把理由写进提交信息。\n%s"
                    % (target, max(0, len(diff) - 2), __file__, name,
                       "\n".join("    " + line for line in diff[:24])))
                continue
            print("✅ %s 与冻结 legacy scene fixture 一致(%d 节点 / %d 资产,几何逐字节复现)"
                  % (target, len(doc["nodes"]), len(doc["assets"])))
            continue

        if args.write:
            target.write_text(text, encoding="utf-8")
            print("已写入 %s" % target)
            # 节点分三类,分开数:1 root + N 图层 + M 个 axis-y 父节点。
            # 早先这里把 len(nodes)-1 一律印成"图层",于是 MiaoCloud 印的是
            # "1 root + 8 图层",而它只有 5 层 —— 剩下 3 个是给李萨如 y 轴加的父节点。
            # 一个读起来像概况、其实是错分类的数字,比没有更坏事。
            layer_nodes = sum(1 for n in doc["nodes"]
                              if n["id"].startswith("node://layer/") and "/axis-" not in n["id"])
            axis_nodes = len(doc["nodes"]) - 1 - layer_nodes
            print("节点 %d 个(1 root + %d 图层 + %d 个 axis-y 父节点),资产 %d 个,动画 %d 条"
                  % (len(doc["nodes"]), layer_nodes, axis_nodes,
                     len(doc["assets"]), len(doc["animations"])))
            continue

        sys.stdout.write(text)

    if failures:
        for f in failures:
            print(f, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
