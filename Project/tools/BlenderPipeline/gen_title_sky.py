"""タイトル画面の空（雲なしの昼の青空）を Blender の物理ベースの空（Nishita）で生成するスクリプト。

使い方（Project/ で実行）:
    blender -b --factory-startup -P tools/BlenderPipeline/gen_title_sky.py -- [オプション]
        --export <パス.hdr>   正距円筒の HDR を書き出す（例: Assets/title_clear_sky.hdr）
        --preview <フォルダ>  確認用 PNG（全天の展開図＋水平方向の見え方）を書き出す
        --ibl                 映り込み（IBL）専用版にする：地平線より下を石っぽい暖色の灰色にする
                              （例: --ibl --export Assets/title_clear_sky_ibl.hdr）

HDR → cubemap DDS は既存の tools/Python/convert_hdr_to_dds.py（Assets/ 直下の *.hdr を変換）で行う。

方針（5_TitleScene.md §10.5）:
- 太陽の円盤は描かない（IBL で金に映したとき太陽の点だけが光るのを防ぐ）。日差しはエンジンの平行光源が担当
- 地平線より下は地平線の色で埋める（水面の端の向こうに黒い地面が見えないように）
- IBL 版（--ibl）は地平線より下を地面の色にする。下向きの反射（カメラより低い所の金など）が
  明るい水色を映して白っぽくなるのを防ぐ。背景・水面に見せる空は通常版のまま使う
- エンジンの Skybox はトーンマップ無しで値をそのまま出すので、地平線の明るさが 1 弱になるよう倍率を合わせる
"""

import math
import sys
from pathlib import Path

import bpy

# ============================================================
# パラメータ
# ============================================================
SUN_ELEVATION_DEG = 55.0   # 太陽の高さ。高いほど空全体が濃い青
SUN_ROTATION_DEG = 200.0   # 太陽の方位（Blender の Z 軸まわり）
AIR_DENSITY = 1.0
DUST_DENSITY = 0.0         # 少ないほど地平線が霞まず水色が残る
OZONE_DENSITY = 4.0        # 多いほど青が深い（アニメ寄り）
SATURATION = 1.5           # 仕上げの彩度
TARGET_HORIZON_MAX = 0.85  # 地平線付近の最大チャンネルをこの値に合わせる（エンジンで白飛びさせない）
WIDTH = 4096               # 正距円筒（高さは半分）
# 視線の z（sin 仰角）の下限。真横ちょうどは大気の通り道が最長で黄ばむので、
# 仰角 6° 付近の水色で地平線とその下を埋める
HORIZON_CLAMP_Z = 0.1
# IBL 版の地平線より下の色（エンジンでの値＝線形）。水越しの石畳を想定した暖色の灰色
IBL_GROUND_COLOR = (0.25, 0.21, 0.16)
IBL_GROUND_BLEND_Z = 0.08  # 地平線から下へこの z（sin 仰角）までかけて地面の色へ移る


def build_world(strength, ibl):
    world = bpy.data.worlds.new("TitleSky")
    bpy.context.scene.world = world
    world.use_nodes = True
    nt = world.node_tree
    nodes, links = nt.nodes, nt.links
    bg = nodes["Background"]

    # 視線の仰角を下限で止める＝地平線より下は地平線の色になる。
    # 方位は保ったまま dir = normalize(x, y, 0) * sqrt(1 - zc^2) + (0, 0, zc)、zc = max(z, 下限)
    # （z だけ止めて正規化すると、真下付近で x, y≒0 のため天頂の色に化ける）
    coord = nodes.new("ShaderNodeTexCoord")
    sep = nodes.new("ShaderNodeSeparateXYZ")
    links.new(coord.outputs["Generated"], sep.inputs["Vector"])

    horiz = nodes.new("ShaderNodeCombineXYZ")
    links.new(sep.outputs["X"], horiz.inputs["X"])
    links.new(sep.outputs["Y"], horiz.inputs["Y"])
    horiz_n = nodes.new("ShaderNodeVectorMath")
    horiz_n.operation = "NORMALIZE"
    links.new(horiz.outputs["Vector"], horiz_n.inputs[0])

    zc = nodes.new("ShaderNodeMath")
    zc.operation = "MAXIMUM"
    zc.inputs[1].default_value = HORIZON_CLAMP_Z
    links.new(sep.outputs["Z"], zc.inputs[0])
    zc2 = nodes.new("ShaderNodeMath")
    zc2.operation = "MULTIPLY"
    links.new(zc.outputs["Value"], zc2.inputs[0])
    links.new(zc.outputs["Value"], zc2.inputs[1])
    one_minus = nodes.new("ShaderNodeMath")
    one_minus.operation = "SUBTRACT"
    one_minus.inputs[0].default_value = 1.0
    links.new(zc2.outputs["Value"], one_minus.inputs[1])
    cos_el = nodes.new("ShaderNodeMath")
    cos_el.operation = "SQRT"
    links.new(one_minus.outputs["Value"], cos_el.inputs[0])

    horiz_s = nodes.new("ShaderNodeVectorMath")
    horiz_s.operation = "SCALE"
    links.new(horiz_n.outputs["Vector"], horiz_s.inputs[0])
    links.new(cos_el.outputs["Value"], horiz_s.inputs["Scale"])
    up = nodes.new("ShaderNodeCombineXYZ")
    links.new(zc.outputs["Value"], up.inputs["Z"])
    norm = nodes.new("ShaderNodeVectorMath")
    norm.operation = "ADD"
    links.new(horiz_s.outputs["Vector"], norm.inputs[0])
    links.new(up.outputs["Vector"], norm.inputs[1])

    sky = nodes.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_disc = False
    sky.sun_elevation = math.radians(SUN_ELEVATION_DEG)
    sky.sun_rotation = math.radians(SUN_ROTATION_DEG)
    sky.air_density = AIR_DENSITY
    sky.dust_density = DUST_DENSITY
    sky.ozone_density = OZONE_DENSITY
    links.new(norm.outputs["Vector"], sky.inputs["Vector"])

    hsv = nodes.new("ShaderNodeHueSaturation")
    hsv.inputs["Saturation"].default_value = SATURATION
    links.new(sky.outputs["Color"], hsv.inputs["Color"])
    bg.inputs["Strength"].default_value = strength
    if not ibl:
        links.new(hsv.outputs["Color"], bg.inputs["Color"])
        return bg, None

    # 視線の z が 0 → -IBL_GROUND_BLEND_Z で空 → 地面の色へ
    ground_fac = nodes.new("ShaderNodeMapRange")
    ground_fac.inputs["From Min"].default_value = 0.0
    ground_fac.inputs["From Max"].default_value = -IBL_GROUND_BLEND_Z
    ground_fac.inputs["To Min"].default_value = 0.0
    ground_fac.inputs["To Max"].default_value = 1.0
    ground_fac.clamp = True
    links.new(sep.outputs["Z"], ground_fac.inputs["Value"])
    ground = nodes.new("ShaderNodeRGB")
    mix = nodes.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    links.new(ground_fac.outputs["Result"], mix.inputs["Factor"])
    links.new(hsv.outputs["Color"], mix.inputs[6])     # A（RGBA の入力）
    links.new(ground.outputs["Color"], mix.inputs[7])  # B
    links.new(mix.outputs[2], bg.inputs["Color"])      # Result（RGBA）
    return bg, ground


def setup_panorama():
    scene = bpy.context.scene
    scene.render.engine = "CYCLES"
    scene.cycles.device = "CPU"
    scene.cycles.samples = 4  # 背景だけなので少なくてよい
    scene.cycles.use_denoising = False
    scene.render.resolution_x = WIDTH
    scene.render.resolution_y = WIDTH // 2
    scene.view_settings.view_transform = "Standard"  # エンジンと同じくトーンマップ無し

    cam_data = bpy.data.cameras.new("Pano")
    cam_data.type = "PANO"
    cam_data.panorama_type = "EQUIRECTANGULAR"
    cam = bpy.data.objects.new("Pano", cam_data)
    cam.rotation_euler = (math.radians(90.0), 0.0, 0.0)  # 水平を向ける（画像の上下中央＝地平線）
    scene.collection.objects.link(cam)
    scene.camera = cam


def render_to(path, file_format, color_depth):
    scene = bpy.context.scene
    scene.render.image_settings.file_format = file_format
    scene.render.image_settings.color_depth = color_depth
    scene.render.filepath = str(Path(path).resolve())
    bpy.ops.render.render(write_still=True)
    print(f"[gen_title_sky] wrote {path}")


def measure_rows():
    """縮小レンダで天頂付近と地平線付近の平均色を測る（倍率合わせ用）"""
    scene = bpy.context.scene
    scene.render.resolution_x, scene.render.resolution_y = 256, 128
    # Render Result を直接保存すると表示変換でクランプされるので、浮動小数の EXR に書いてから読む
    tmp = Path(bpy.app.tempdir) / "sky_measure.exr"
    render_to(tmp, "OPEN_EXR", "32")
    m = bpy.data.images.load(str(tmp))
    w, h = m.size
    px = m.pixels[:]

    def row_avg(y):
        acc = [0.0, 0.0, 0.0]
        for x in range(w):
            i = (y * w + x) * 4
            for c in range(3):
                acc[c] += px[i + c]
        return [a / w for a in acc]

    # 画像の行 0 が下（Blender の pixels は下から）。h-1 付近＝天頂、h/2 の少し上＝地平線
    zenith = row_avg(h - 2)
    horizon = row_avg(h // 2 + 2)
    scene.render.resolution_x, scene.render.resolution_y = WIDTH, WIDTH // 2
    return zenith, horizon


def main():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    export_path = Path(argv[argv.index("--export") + 1]) if "--export" in argv else None
    preview_dir = Path(argv[argv.index("--preview") + 1]) if "--preview" in argv else None
    ibl = "--ibl" in argv

    bpy.ops.wm.read_factory_settings(use_empty=True)
    setup_panorama()
    bg, ground = build_world(1.0, ibl)

    # 地平線の明るさが TARGET_HORIZON_MAX になるよう倍率を決める
    zenith, horizon = measure_rows()
    strength = TARGET_HORIZON_MAX / max(max(horizon), 1e-6)
    bg.inputs["Strength"].default_value = strength
    if ground is not None:
        # Background の強さが全体に掛かるので、地面の色は割り戻して置く
        ground.outputs["Color"].default_value = (*(c / strength for c in IBL_GROUND_COLOR), 1.0)
    fmt = lambda c: "(" + ", ".join(f"{v * strength:.3f}" for v in c) + ")"
    print(f"[gen_title_sky] strength={strength:.4f} zenith={fmt(zenith)} horizon={fmt(horizon)}")

    if export_path is not None:
        export_path.parent.mkdir(parents=True, exist_ok=True)
        render_to(export_path, "HDR", "32")

    if preview_dir is not None:
        preview_dir.mkdir(parents=True, exist_ok=True)
        render_to(preview_dir / "sky_equirect.png", "PNG", "8")
        # 水平方向（タイトルのカメラに近い画角）の見え方
        scene = bpy.context.scene
        cam = scene.camera
        cam.data.type = "PERSP"
        cam.data.lens = 24.0
        cam.rotation_euler = (math.radians(95.0), 0.0, 0.0)
        scene.render.resolution_x, scene.render.resolution_y = 1280, 720
        render_to(preview_dir / "sky_view.png", "PNG", "8")


main()
