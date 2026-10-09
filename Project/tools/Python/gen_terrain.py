#!/usr/bin/env python3
"""低空飛行フェーズの地形（川筋・岸・丘）をハイトマップとして手続き生成する。

設計書: Documents/Tasks/12_TerrainRendering.md（L2）。値の前提は 11_LowAltitudeFlight.md §3.0。

設計:
    - 川の中心線 = レール（gen_rail_path.build_points()）の XZ。エンジンの SplineCurveActor と
      同じ Catmull-Rom（端点はクランプ）で補間し、細かいポリラインにする。
    - 各グリッド点から中心線までの符号付き距離 sd（進行方向 +Z に対して右が +）と、
      最寄り点までの弧長 s を求める。川の半幅は s の 1D ノイズでゆっくり変える。
    - 高さは「川の断面（川底 → 岸の斜面 → 草原）」＋「外側の丘（fBm）」の足し算。
      川底は bed_y で止める（深さの色は川のマップが持つ。12 番 §2.3）。
    - 谷にも流用できるよう、形のパラメータはすべてレシピ JSON に出す（12 番 §6）。
    - 2026-10-09（12 番 §8 Step 7）：蛇行の内側の大きい山（peaks）、縁の向こうの台地（plateau）、
      谷のレールに沿った峡谷（canyon）、海岸（coast）。高さは extent 全体で 1 枚として計算し、
      chunks で複数のメッシュ（streamGroup 別）に切り分ける。

使い方（Project/ で実行）:
    python tools/Python/gen_terrain.py --recipe tools/Python/recipes/river_terrain.json
    python tools/Python/gen_terrain.py --recipe tools/Python/recipes/river_terrain.json --seed 7
    python tools/Python/gen_terrain.py --recipe tools/Python/recipes/river_terrain.json --gltf --scene
      --gltf   : glTF と仮の色テクスチャを Assets/ に書き、cook_assets.py で .mesh/.mat にする
      --scene  : StagePlay.json の <name> エントリだけを書き換える（無ければ足す）
      --river-map: 川のマップ（L3 の水面用）と共通の .json を書き、cook する
      --shore  : 左右の岸の線（L6 の岩配置・L10 の茂み用）を書く
      --visibility: 見通しチェック。実機と同じ視野で、後から読む塊・谷の中が画面に映る割合を秒ごとに出す
      --no-cook: --gltf / --river-map でも cook を走らせない
  chunks があると --gltf / --scene は塊ごと（<塊の名前>.mesh など）、--river-map / --shore は river_map / shore が
  true の塊だけで書く。以下の <name> は塊の名前（Generated/ の確認用の出力だけはレシピの name）。

出力（Generated/ は .gitignore 済み。Assets/ に置くと cook が BC7_SRGB で上書きするので置かない）:
    Generated/Terrain/<name>/<name>_Height.npy      float32 の高さ [m]（後段のメッシュ化用）
    Generated/Terrain/<name>/<name>_Height16.png    16bit の高さ（min..max を 0..65535）
    Generated/Terrain/<name>/<name>_Height.json     範囲・原点・セル幅・画像の向き
    Generated/Terrain/<name>/<name>_Preview.png     真上から見た陰影図＋レール＋川の縁
    Generated/Terrain/<name>/<name>_View_*.png      レールの視点からの簡易レンダ（VIEWS。仮の色＋陰影＋空気遠近）
  画像の向きはすべて「上 = +Z（前方）、右 = +X」。行 0 が z_max、列 0 が x_min。
--gltf の出力:
    Assets/Models/<name>/<name>.gltf / .bin → Resources/Models/<name>/<name>.mesh / .mat
    Assets/Textures/MaskTexture/Terrain/<name>_Splat.png → BC7_UNORM（線形）。地形シェーダの t0
        R = 草、G = 土・砂（水際の帯と土の斑）、B = 岩（急斜面）、A = 濡れ（水際）。RGB は合計 1
    Assets/Textures/MaskTexture/Terrain/<name>_Tint.png → BC7_UNORM（線形）。地形全体の色合い（アルベドへの乗数 ÷ 2。0.5 = そのまま）
        地形シェーダの t2 で引く（地形は法線マップを使わないので、法線マップの枠を流用する。glTF の normalTexture に入れる）
  .mat は v5（shadingModel = 2）。層の配列テクスチャ（gen_terrain_layers.py）とタイル長などは glTF の
  extras.terrainLayers で cook_assets.py へ渡す。
  UV は地形全体で 0..1（u = +X、v = 行の向き＝ v 0 が z_max）。スプラットマップ・川のマップと同じ対応。
--river-map の出力（12_TerrainRendering.md §5 / 11_LowAltitudeFlight.md §3.0）:
    Assets/Textures/MaskTexture/Terrain/<name>_RiverMap.png → Resources/.../<name>_RiverMap.dds（BC7_UNORM・線形）
        R = 水深（0..1 → 0..maxDepth m）、G,B = 流れのワールド X,Z（0.5 中心、±1 = maxFlowSpeed m/s）、A = 予備（1）
    Resources/Json/Terrain/<name>.json   ワールド XZ ↔ UV の対応・maxDepth・maxFlowSpeed（ゲームが読む。スプラットマップと共有）
    Generated/Terrain/<name>/<name>_RiverMapPreview.png   水深の色＋流れの矢印
--shore の出力:
    Resources/Json/Terrain/<name>_Shore.json   左右の岸（水際 = 高さ 0 の線）のポリライン [[x, z], ...]（下流へ向かう順）
--visibility の出力:
    Generated/Terrain/<name>/<name>_Vis_<秒>s.png   実機の視野の簡易レンダ（マゼンタ = 後から読む塊、黄 = 谷の中）
"""

from __future__ import annotations

import argparse
import json
import math
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

sys.path.insert(0, str(Path(__file__).resolve().parent))
import gen_rail_path  # noqa: E402


DEFAULT_RECIPE: dict = {
    "name": "Terrain",
    "seed": 1,
    "extent": {"x_min": -1000.0, "x_max": 1000.0, "z_min": 900.0, "z_max": 2600.0},
    "cell_m": 4.0,
    "river": {
        "rail_index_range": [0, 59],  # 中心線に使うレールの制御点（両端を含む）
        "half_width_min": 100.0,
        "half_width_max": 160.0,
        "width_wavelength": 700.0,    # 半幅の揺れの波長（Z 方向 [m]）。左右の岸は別のノイズ
        "width_bias": 1.0,            # 半幅ノイズの累乗。大きいほど普段は細く、ときどき大きく膨らむ
        "edge_noise_amp": 0.0,        # 岸の線を揺らす 2D ノイズの振れ幅 [m]（入り江・岬）
        "edge_noise_wavelength": 250.0,
        "guard": 100.0,               # 中心線からこの距離 [m] 以内は必ず水（§3.0 の半幅 100m 以上）
        "guard_smooth": 30.0,         # guard との境をなめらかにつなぐ幅 [m]
        "bed_y": -3.0,
        "shore_in": 30.0,             # 川の縁から内側へ、川底に着くまでの距離 [m]
        "shore_out": 25.0,            # 川の縁から外側へ、草原の高さに着くまでの距離 [m]
        "head_extend": 1500.0,        # 中心線の始まりを接線方向に延ばす長さ [m]（範囲の端で縁が丸まらないように）
        "tail_extend": 1500.0,        # 中心線の終わりを延ばす長さ [m]。滝で終わる川は縁に届く分だけ
        # 滝の手前で川を絞る（滝の幅 ≈ 谷の幅。11 番 §1-E の絵2）。half_width = 0 で無効
        "taper": {"z_start": 0.0, "z_end": 0.0, "half_width": 0.0, "noise_cut": 0.8},
    },
    "bank": {"height_min": 0.0, "height_max": 3.0, "wavelength": 180.0},
    "hills": {
        "height": 60.0,
        "start": 120.0,               # 川の縁からこの距離で丘が立ち上がり始める [m]
        "ramp": 450.0,                # 立ち上がりきるまでの距離 [m]
        "wavelength": 700.0,
        "octaves": 5,
        "persistence": 0.5,
        "lacunarity": 2.0,
        "sharpness": 1.6,             # fBm の累乗。大きいほど谷が広く峰が尖る
        "ridge_mix": 0.0,             # 尾根のノイズ（ridged）を混ぜる割合。尾根筋が出る
        "ridge_wavelength": 500.0,
    },
    # 高さを段にする。段の境（蹴上げ）が急斜面になり、傾斜で塗る岩の層が「崖」として出る
    "terrace": {
        "step": 0.0,                  # 段の高さ [m]。0 で無効
        "riser": 0.3,                 # 1 段のうち蹴上げ（急な所）の割合
        "strength": 0.0,              # 0..1。元の高さとどれだけ混ぜるか
        "jitter": 0.5,                # 段の位相をノイズでずらす量（段 1 つ分 = 1）。等高線そのままの段にしない
        "jitter_wavelength": 250.0,
        "mask_coverage": 1.0,         # 段にする場所の割合（0..1）。参考の丘は崖の帯がところどころに出る
        "mask_wavelength": 400.0,
        # 段の高さ・崖の厚みを場所ごとに揺らす（一様だと等間隔・等幅の帯に見える）
        "step_variation": 0.0,        # 段の高さを (1 ± これ) 倍の範囲で揺らす
        "riser_range": [0.3, 0.3],    # 蹴上げの割合の範囲
        "variation_wavelength": 300.0,
    },
    # 陸の上全体の細かい起伏（つるっとした輪郭を崩す）
    "detail": {"amp": 0.0, "wavelength": 70.0, "octaves": 3, "start": 15.0, "ramp": 60.0},
    # 決まった範囲に盛る丘（滝の手前の視線を切る丘など）。side で片岸だけにもできる
    "mounds": [],
    # 縁の向こうの台地（11 番 §1-F）。z_start から blend で川沿いの丘と入れ替え、奥へ向かって rise まで上る。
    # 上っていくので、低空（Y≈3.5）からも谷の切れ目と左右の草原が見える
    "plateau": {
        "enabled": False,
        "z_start": 2450.0,
        "blend": 220.0,
        "z_wobble": 0.0,                # z_start を列ごとに ± これだけ揺らす [m]
        "z_wobble_wavelength": 300.0,
        "rise": 0.0,                    # 奥での高さ [m]
        "rise_z": [2700.0, 3450.0],     # この Z の範囲で 0 → rise へ上る
        "amp": 10.0,                    # なだらかな起伏の高さ [m]
        "wavelength": 260.0,
    },
    # 大きい山（蛇行の内側。11 番 §3.0「低空の蛇行と山」・ユーザーの参考画像 5 枚目）。
    # 裾は緩い草の斜面、上は縦の溝の入った急な岩肌。川の側は川が削った崖になる
    "peaks": [],
    # 台地の縁の崖（滝）。縁より先は谷底まで落とす。谷の中の細部は谷の担当（12 番 §0）
    "cliff": {
        "enabled": False,
        "z": 2520.0,                    # 縁の基準の Z [m]（§3.0 の滝の縁 Z ≈ 2500〜2540）
        "amp": 18.0,                    # 縁のギザギザの振れ幅 [m]（尾根ノイズ）
        "wavelength": 90.0,
        "face_width": 25.0,             # 崖面の水平の幅 [m]（ここで floor_y まで落ちる）
        "face_jitter": 5.0,             # 崖面を高さ方向にも前後させる量 [m]（平らな壁に見せない）
        "floor_y": -150.0,              # 谷底（§3.0）
        "notch": 15.0,                  # 川の所だけ縁を奥へ切り込ませる量 [m]（滝の口）
        "river_ends": False,            # 川を縁で終わらせる（縁の先は台地。峡谷と組み合わせる）
        "rail_clear_x": 60.0,           # レールからこの横の距離 [m] 以内は縁を rail_max_z までに収める
        "rail_max_z": 2525.0,           # §2.3：レールが通る所は縁を Z ≤ 2540（idx40 は Z=2565・Y=−10 で台地より下を通る）
    },
    # 縁の先の峡谷（谷のレールに沿った溝）。無効なら縁の先は全幅で谷底（旧来の形）。
    # 谷の中の細部（岩柱・障害物・当たり判定）は谷の担当。谷のレールを変えたらこのスクリプトを流し直す
    "canyon": {
        "enabled": False,
        "rail_index_range": [39, 59],
        "tail_extend": 900.0,           # 谷のレールの終わりを海まで延ばす [m]
        "half_width": 130.0,            # 上の半幅 [m]（仮。谷の担当が決める）
        "half_width_amp": 15.0,         # 左右別の 1D ノイズで揺らす量
        "half_width_wavelength": 180.0,
        "wall_noise_amp": 10.0,         # 壁の線のギザギザ（尾根ノイズ）
        "wall_noise_wavelength": 60.0,
        "floor_ratio": 0.55,            # 谷底の半幅 = 上の半幅 × これ
        "step": 16.0,                   # 壁の段（石を積んだような段）の高さ [m]
        "riser": 0.45,                  # 1 段のうち切り立つ部分の割合
        "step_jitter": 0.6,
    },
    # 海岸（谷の出口の先）。台地は海食崖で海へ落ちる。海の水面は L3／ボス戦の担当（ここは地形だけ）
    "coast": {
        "enabled": False,
        "sea_y": -150.0,                # 海面（11 番 §3.0：谷底と同じ）
        "z": 3620.0,                    # 海岸線の基準の Z [m]
        "amp": 90.0,
        "wavelength": 420.0,
        "width": 70.0,                  # 崖の上から海底まで落ちる水平の幅 [m]
        "sea_floor_y": -178.0,
        "floor_slope": 0.04,            # 沖へ向かって深くなる勾配
    },
    # 岩の所（スプラットの岩）に盛る塊の凹凸。網より細かい形は無いので、影が落ちる凹凸はここで作る
    "rocks": {
        "amp": 0.0,                     # 塊の高さ [m]（0 で無効）
        "cell": 6.0,                    # 塊の大きさ [m]（ボロノイの 1 セル）
        "tilt": 0.35,                   # 塊ごとの面の傾き
        "crack": 0.18,                  # 塊の境の割れ目の幅（セルの大きさに対する割合）
        "crack_depth": 0.6,             # 割れ目の深さ（amp に対する割合）
    },
    # スプラットマップ（地形シェーダの t0）
    "splat": {
        "size": 2048,
        "rock_slope": [0.22, 0.38],     # 1 - 法線.y がこの範囲で岩へ（約 39°〜52°）
        "rock_jitter": 0.0,             # 岩になる傾斜のしきい値をノイズで ± これだけ揺らす（帯の縁をギザギザに）
        "rock_jitter_wavelength": 30.0,
        "outcrop_coverage": 0.0,        # やや急な草原に散らす岩の露頭の割合
        "outcrop_wavelength": 45.0,
        "outcrop_min_slope": 0.06,      # これより平らな所には露頭を出さない（約 20°）
        "sand_band": 10.0,              # 水際からこの距離 [m] までは砂
        "dirt_coverage": 0.12,          # 草原の中の土の斑の割合
        "dirt_wavelength": 140.0,
        "wet_height": [0.3, 1.5],       # この高さ [m] より下ほど濡れる
    },
    # 地形全体の色合いのマップ（遠くで層の模様が平均されて単色になるのを防ぐ）
    "tint": {
        "size": 2048,
        "lush": [0.72, 0.86, 0.78],     # 谷・窪み・水辺（濃い緑）。草の層の色への乗数
        "dry": [1.38, 1.22, 0.80],      # 尾根・日なた・高い所（乾いた黄色）
        "olive": [1.12, 1.02, 0.72],
        "teal": [0.82, 0.97, 1.08],
        "rock_warm": [1.18, 0.94, 0.82],   # 赤茶の岩
        "rock_cool": [0.92, 0.96, 1.02],   # 灰の岩
        "dry_wavelength": 320.0,        # 乾き具合の大きな斑
        "hue_wavelength": 45.0,         # 色合いの小さな斑
        "curvature_radius": 12.0,       # 尾根・谷を測る範囲 [m]
    },
    # 地形シェーダの層（gen_terrain_layers.py の配列 DDS と順番を合わせる：草 / 土 / 岩）
    "material": {
        "color_array": "Resources/Textures/Terrain/TerrainLayers_Color.dds",
        "normal_array": "Resources/Textures/NormalMapTexture/Terrain/TerrainLayers_Normal.dds",
        "tile": [4.0, 5.0, 8.0],        # 層ごとのタイル長 [m]
        "roughness": [0.95, 0.9, 0.8],
        "height_blend": 0.2,            # 高さブレンドの幅（小さいほど境界がくっきり）
        "triplanar_sharpness": 4.0,     # 岩の三平面投影の面の切り替えの鋭さ
        "macro_variation": 0.25,        # 低い周波数の色ムラの強さ
        "wetness": 0.45,                # 濡れた所を暗くする強さ
        "environment": 0.4,             # 環境光（空の映り込み）の強さ。1 だと空の青に染まって明暗が平たくなる
    },
    # 川のマップ（L3 の水面が読む）
    "river_map": {
        "size": 1024,
        "max_depth": 10.0,            # R = 1 の水深 [m]（§3.0 では仮）
        "depth_ramp": 80.0,           # 水際からこの距離 [m] で最大の深さになる
        "base_speed": 2.0,            # 流速 [m/s]
        "max_speed": 8.0,             # 滝の口での流速。G,B の ±1 もこの値
        "falls_z": 2500.0,            # 滝の口の Z（§3.0 の滝の縁）
        "accel_length": 600.0,        # 滝の口の手前、この距離で加速する [m]
        "bank_slow": 40.0,            # 水際からこの距離 [m] までは遅くする
    },
    # 出力するメッシュの分け方。高さは extent 全体で 1 枚として計算し、ここで切り分ける（境目の行は共有するので隙間は出ない）。
    # 空なら extent 全体を name の 1 枚にする。river_map / shore はその塊だけで書く
    # 例: {"name": "RiverTerrain", "x_min": .., "x_max": .., "z_min": .., "z_max": .., "streamGroup": "LowAltitude",
    #      "river_map": true, "shore": true}
    "chunks": [],
    # 見通しチェック（--visibility）：実機と同じ視野で、hide_z より奥（後から読む塊）が画面に映る割合を秒ごとに出す
    "visibility": {
        "t_from": 72.0,
        "t_to": 117.0,
        "hide_z": 2600.0,
        "interior_z": 2540.0,           # 谷の中の判定：縁より奥で
        "interior_y": -10.0,            #   この高さより下（11 番 §3.0「谷の物の高さの上限」）
        "fov_y": 0.45,                  # カメラの FovY [rad]
        "aspect": 16.0 / 9.0,
        "pitch_bias_deg": -5.0,         # camera.lowPitch（11 番 §5.3）
        "pitch_window": [72.0, 119.0],
        "width": 240,
        "snap_secs": [87, 90, 93, 96, 100, 103, 105, 106, 108, 111, 114],
    },
}

PEAK_DEFAULT: dict = {
    "x": 0.0, "z": 0.0,           # 山の中心 [m]
    "radius": 250.0,              # 裾の半径 [m]
    "height": 200.0,
    "foot": 0.15,                 # 裾（草の斜面）の高さの割合
    "foot_r": [0.70, 1.0],        # 裾が立ち上がる半径の範囲（radius に対する割合）
    "core_r": [0.25, 0.62],       # 岩の本体が立ち上がる範囲
    "warp": 0.18,                 # 輪郭を歪ませる量（円く見せない）
    "warp_wavelength": 220.0,
    "flute": 0.35,                # 縦の溝（柱状の岩肌）の深さの割合
    "flute_wavelength": 22.0,     # 溝の間隔 [m]
    "crag": 0.25,                 # 頂上付近のギザギザ
    "crag_wavelength": 70.0,
    "river_start": 5.0,           # 川の縁からこの距離で立ち上がる [m]（川の側は削られた崖になる）
    "river_ramp": 60.0,
    "rock_r": 0.66,               # この半径より内側は岩の塗り（裾は草。木は L10）
}

MOUND_DEFAULT: dict = {
    "z_min": 0.0, "z_max": 0.0,   # 丘を盛る Z の範囲 [m]
    "z_fade": 150.0,              # 範囲の前後で高さを 0 へ落とす距離 [m]
    "height": 100.0,
    "start": 40.0,                # 川の縁からこの距離で立ち上がり始める [m]
    "ramp": 200.0,
    "wavelength": 320.0,          # 高さの揺れ（峰と鞍部）の波長
    "variation": 0.5,             # 高さの揺れの強さ（0 = 一様な壁）
    "side": "both",               # "both" / "left" / "right"（下流を向いて）
    "z_wobble": 0.0,              # z_min / z_max を列ごとに ± これだけ揺らす [m]
    "z_wobble_wavelength": 300.0,
}

SCENE_PATH = "Resources/Json/Scenes/StagePlay.json"
TERRAIN_JSON_DIR = "Resources/Json/Terrain"
STREAM_GROUP = "LowAltitude"
CATMULL_SUBDIV = 16        # レール 1 区間あたりの分割数
PREVIEW_SCALE = 1
SUN_TOWARD = (-0.191, 0.815, -0.547)   # 太陽へ向かう向き（タイトルの空・StagePlay の sections と同じ）

# 確認用の視点（広角の見渡し図）：(名前, レールの制御点, 高さの上書き[m] or None, yaw[deg, +X 側が正。None = レールの向き], 見下ろし[deg])
VIEWS = [
    ("Dive_idx20", 20, None, 0.0, 35.0),        # 降下の途中（Y=345）。地形の端の見え方
    ("Low_idx26", 26, 30.0, None, 6.0),         # 低空の前半（少し上から）
    ("Meander_idx30", 30, 30.0, None, 2.0),     # 11 番 §1-E 絵1：山が正面
    ("Falls_idx36", 36, 30.0, None, 2.0),       # 絵2：曲がり終えて縁と谷
    ("Canyon_idx44", 44, None, None, 5.0),      # 谷の中
]


def merge(base: dict, over: dict) -> dict:
    out = dict(base)
    for k, v in over.items():
        out[k] = merge(out[k], v) if isinstance(v, dict) and isinstance(out.get(k), dict) else v
    return out


def smoothstep(a: float, b: float, x: np.ndarray) -> np.ndarray:
    t = np.clip((x - a) / (b - a), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def smooth_min(a: np.ndarray, b: np.ndarray, k: float) -> np.ndarray:
    h = np.clip(0.5 + 0.5 * (b - a) / k, 0.0, 1.0)
    return b + (a - b) * h - k * h * (1.0 - h)


# ---------------------------------------------------------------- ノイズ（範囲付きの値ノイズ。タイリングはしない）

def value_noise_2d(x: np.ndarray, z: np.ndarray, wavelength: float, rng: np.random.Generator) -> np.ndarray:
    """0..1 の値ノイズ。格子の原点を乱数でずらし、オクターブ間で格子が揃わないようにする。"""
    ox, oz = rng.random(2) * 1000.0
    gx = (x - x.min()) / wavelength + ox % 1.0
    gz = (z - z.min()) / wavelength + oz % 1.0
    nx = int(math.ceil(gx.max())) + 2
    nz = int(math.ceil(gz.max())) + 2
    lattice = rng.random((nz, nx))
    ix = np.floor(gx).astype(np.int64)
    iz = np.floor(gz).astype(np.int64)
    tx = gx - ix
    tz = gz - iz
    tx = tx * tx * (3.0 - 2.0 * tx)
    tz = tz * tz * (3.0 - 2.0 * tz)
    v00 = lattice[iz, ix]
    v10 = lattice[iz, ix + 1]
    v01 = lattice[iz + 1, ix]
    v11 = lattice[iz + 1, ix + 1]
    return (v00 * (1 - tx) + v10 * tx) * (1 - tz) + (v01 * (1 - tx) + v11 * tx) * tz


def fbm_2d(x, z, wavelength, octaves, persistence, lacunarity, rng) -> np.ndarray:
    total = np.zeros_like(x)
    amp, norm, wl = 1.0, 0.0, wavelength
    for _ in range(octaves):
        total += value_noise_2d(x, z, wl, rng) * amp
        norm += amp
        amp *= persistence
        wl /= lacunarity
    return total / norm


def ridged_2d(x, z, wavelength, octaves, rng) -> np.ndarray:
    """0..1 の尾根ノイズ。値ノイズの 0.5 付近を尖らせる（1 - |2n - 1|）^2 を重ねる。"""
    total = np.zeros_like(x)
    amp, norm, wl = 1.0, 0.0, wavelength
    for _ in range(octaves):
        n = value_noise_2d(x, z, wl, rng)
        total += (1.0 - np.abs(2.0 * n - 1.0)) ** 2 * amp
        norm += amp
        amp *= 0.5
        wl *= 0.5
    return total / norm


def normalize_signed(n: np.ndarray) -> np.ndarray:
    """fBm は 0.5 付近に固まるので、標準偏差で正規化して -1..1 に収める。"""
    return np.clip((n - n.mean()) / (2.0 * n.std()), -1.0, 1.0)


def terrace(h: np.ndarray, step: float, riser: float, phase: np.ndarray) -> np.ndarray:
    """段々にした高さ。1 段のうち (1 - riser) が平ら、riser が急な蹴上げになる。"""
    t = h / step + phase
    f = np.floor(t)
    frac = smoothstep(1.0 - riser, 1.0, t - f)
    return (f + frac - phase) * step


def value_noise_1d(s: np.ndarray, wavelength: float, rng: np.random.Generator) -> np.ndarray:
    g = (s - s.min()) / wavelength + rng.random()
    lattice = rng.random(int(math.ceil(g.max())) + 2)
    i = np.floor(g).astype(np.int64)
    t = g - i
    t = t * t * (3.0 - 2.0 * t)
    return lattice[i] * (1 - t) + lattice[i + 1] * t


# ---------------------------------------------------------------- 川の中心線

def catmull_rom_xz(points: list[list[float]], subdiv: int) -> np.ndarray:
    """SplineCurveActor::Sample と同じ Catmull-Rom（端はクランプ）で XZ を細分化する。"""
    p = np.array([[q[0], q[2]] for q in points], dtype=np.float64)
    n = len(p)
    out = []
    for seg in range(n - 1):
        p0, p1, p2, p3 = p[max(seg - 1, 0)], p[seg], p[seg + 1], p[min(seg + 2, n - 1)]
        for k in range(subdiv):
            t = k / subdiv
            t2, t3 = t * t, t * t * t
            out.append(0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2
                              + (-p0 + 3 * p1 - 3 * p2 + p3) * t3))
    out.append(p[-1])
    return np.array(out)


def build_centerline(rail_points, idx_range, head_extend: float, tail_extend: float) -> np.ndarray:
    a, b = idx_range
    line = catmull_rom_xz(rail_points[a:b + 1], CATMULL_SUBDIV)
    parts = [line]
    if head_extend > 0.0:
        d0 = line[0] - line[1]
        parts.insert(0, (line[0] + d0 / np.linalg.norm(d0) * head_extend)[None, :])
    if tail_extend > 0.0:
        d1 = line[-1] - line[-2]
        parts.append((line[-1] + d1 / np.linalg.norm(d1) * tail_extend)[None, :])
    return np.vstack(parts)


def rail_sample(rail, s: float) -> np.ndarray:
    """制御点の番号 s（小数）でのレールの位置。SplineCurveActor と同じ Catmull-Rom（端はクランプ）"""
    n = len(rail)
    seg = min(max(int(math.floor(s)), 0), n - 2)
    t = min(max(s - seg, 0.0), 1.0)
    p0, p1 = np.array(rail[max(seg - 1, 0)]), np.array(rail[seg])
    p2, p3 = np.array(rail[seg + 1]), np.array(rail[min(seg + 2, n - 1)])
    return 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                  + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)


def rail_forward(rail, s: float) -> np.ndarray:
    a = rail_sample(rail, max(s - 0.02, 0.0))
    b = rail_sample(rail, min(s + 0.02, len(rail) - 1.0))
    d = b - a
    return d / max(np.linalg.norm(d), 1e-9)


def signed_distance(px: np.ndarray, pz: np.ndarray, line: np.ndarray, chunk: int = 16384):
    """各点 → ポリラインの符号付き距離（進行方向に対して右が +）と、最寄りの線分の向き（XZ の単位ベクトル）を返す。"""
    a = line[:-1]
    d = line[1:] - a
    seg_len = np.linalg.norm(d, axis=1)
    seg_len2 = np.maximum(seg_len * seg_len, 1e-12)
    unit = d / np.maximum(seg_len, 1e-12)[:, None]

    flat_x, flat_z = px.ravel(), pz.ravel()
    sd = np.empty_like(flat_x)
    tangent = np.empty((flat_x.size, 2))
    for i in range(0, flat_x.size, chunk):
        qx = flat_x[i:i + chunk, None] - a[None, :, 0]
        qz = flat_z[i:i + chunk, None] - a[None, :, 1]
        t = np.clip((qx * d[None, :, 0] + qz * d[None, :, 1]) / seg_len2[None, :], 0.0, 1.0)
        ex = qx - t * d[None, :, 0]
        ez = qz - t * d[None, :, 1]
        dist2 = ex * ex + ez * ez
        best = np.argmin(dist2, axis=1)
        rows = np.arange(best.size)
        dist = np.sqrt(dist2[rows, best])
        # cross(d, q) が負 = 右側（+Z 進行で +X 側）
        cross = d[best, 0] * qz[rows, best] - d[best, 1] * qx[rows, best]
        sd[i:i + chunk] = np.where(cross <= 0.0, dist, -dist)
        tangent[i:i + chunk] = unit[best]
    return sd.reshape(px.shape), tangent.reshape(px.shape + (2,))


# ---------------------------------------------------------------- 高さ

def cliff_edge_line(xs: np.ndarray, line: np.ndarray, cl: dict, crng: np.random.Generator) -> np.ndarray:
    """台地の縁の線（列ごとの Z）。尾根ノイズで尖ったギザギザ。レールの周りは手前に収める"""
    rid = np.zeros_like(xs)
    amp_sum, wl = 0.0, cl["wavelength"]
    for o in range(4):
        v = value_noise_1d(xs, wl, crng)
        rid += (1.0 - np.abs(2.0 * v - 1.0)) ** 2 * (0.5 ** o)
        amp_sum += 0.5 ** o
        wl *= 0.5
    rid = (rid / amp_sum - 0.5) * 2.0
    z_edge = cl["z"] + cl["amp"] * rid
    # 中心線の X は縁の Z で引く
    rail_x = float(np.interp(cl["z"], line[:, 1], line[:, 0]))
    near = 1.0 - smoothstep(cl["rail_clear_x"], cl["rail_clear_x"] + 40.0, np.abs(xs - rail_x))
    return z_edge + (np.minimum(z_edge, cl["rail_max_z"]) - z_edge) * near


def build_peak(px: np.ndarray, pz: np.ndarray, edge: np.ndarray, pk: dict, rng: np.random.Generator):
    """大きい山の高さ [m] と岩の塗りの重みを返す。裾は緩い草の斜面、本体は縦の溝の入った岩"""
    dx, dz = px - pk["x"], pz - pk["z"]
    dist = np.sqrt(dx * dx + dz * dz)
    warp = normalize_signed(fbm_2d(px, pz, pk["warp_wavelength"], 3, 0.5, 2.0, rng))
    r = dist / (pk["radius"] * (1.0 + pk["warp"] * warp))
    core = 1.0 - smoothstep(pk["core_r"][0], pk["core_r"][1], r)
    foot = 1.0 - smoothstep(pk["foot_r"][0], pk["foot_r"][1], r)
    # 縦の溝：中心からの角度方向に細かく、半径方向に長い尾根ノイズ（参考画像の柱状の岩肌）。
    # 角度の継ぎ目（±π）は山の裏（+Z 側）に向ける
    # 角度はノイズで曲げる（まっすぐな放射線だと真上から星形に見える）。山頂付近は溝を弱める
    bend = normalize_signed(fbm_2d(px, pz, 90.0, 3, 0.5, 2.0, rng)) * pk["flute_wavelength"] * 2.5
    arc = np.arctan2(-dx, -dz) * pk["radius"] + bend
    n = value_noise_2d(arc, dist * 0.12, pk["flute_wavelength"], rng)
    flute = (1.0 - np.abs(2.0 * n - 1.0)) ** 2
    flute = 1.0 + (flute - 1.0) * smoothstep(0.12, 0.4, r)
    crag = ridged_2d(px, pz, pk["crag_wavelength"], 3, rng)
    body = core * (1.0 - pk["flute"] + pk["flute"] * flute) * (1.0 - pk["crag"] + pk["crag"] * crag)
    river = smoothstep(pk["river_start"], pk["river_start"] + pk["river_ramp"], edge)
    height = pk["height"] * (pk["foot"] * foot + (1.0 - pk["foot"]) * body) * river
    rock = (1.0 - smoothstep(pk["rock_r"] - 0.04, pk["rock_r"] + 0.04, r)) * smoothstep(0.3, 0.6, river)
    return height, rock


def build_height(recipe: dict):
    ext = recipe["extent"]
    cell = float(recipe["cell_m"])
    nx = int(round((ext["x_max"] - ext["x_min"]) / cell)) + 1
    nz = int(round((ext["z_max"] - ext["z_min"]) / cell)) + 1
    xs = ext["x_min"] + np.arange(nx) * cell
    zs = ext["z_max"] - np.arange(nz) * cell   # 行 0 = z_max（真上図で上が前方）
    px, pz = np.meshgrid(xs, zs)

    rng = np.random.default_rng(recipe["seed"])
    rail = gen_rail_path.build_points()
    rv = recipe["river"]
    line = build_centerline(rail, rv["rail_index_range"], rv["head_extend"], rv["tail_extend"])
    sd, tangent = signed_distance(px, pz, line)

    cl = recipe["cliff"]
    crng = np.random.default_rng(int(recipe["seed"]) + 41)
    z_edge = cliff_edge_line(xs, line, cl, crng) if cl["enabled"] else None

    # 半幅は「その点の Z」で引く。最寄り点の弧長で引くと、川から遠い所で最寄り点が別の曲がりへ
    # 飛んで段差（横筋）が出る。レールは Z に単調なので Z で十分。左右の岸は別のノイズ。
    tp = rv["taper"]
    taper = smoothstep(tp["z_start"], tp["z_end"], pz) if tp["half_width"] > 0.0 else np.zeros_like(pz)

    def side_width():
        n = value_noise_1d(pz, rv["width_wavelength"], rng) ** rv["width_bias"]
        hw = rv["half_width_min"] + (rv["half_width_max"] - rv["half_width_min"]) * n
        return hw + (tp["half_width"] - hw) * taper
    hw_right, hw_left = side_width(), side_width()
    half_width = np.where(sd >= 0.0, hw_right, hw_left)
    edge = np.abs(sd) - half_width   # 川の縁からの距離（川の中で負）
    if rv["edge_noise_amp"] > 0.0:
        wobble = fbm_2d(px, pz, rv["edge_noise_wavelength"], 4, 0.5, 2.0, rng)
        # fBm は 0.5 付近に固まるので、標準偏差で正規化して振れ幅を amp に合わせる
        wobble = np.clip((wobble - wobble.mean()) / (2.0 * wobble.std()), -1.0, 1.0)
        edge += rv["edge_noise_amp"] * wobble * (1.0 - tp["noise_cut"] * taper)
    # どれだけ揺らしても、レールの周りは水にする
    edge = smooth_min(edge, np.abs(sd) - rv["guard"], rv["guard_smooth"])
    if z_edge is not None and cl.get("river_ends", False):
        # 川は滝の縁で終わる。縁を越えた所は陸（峡谷の中は後で掘る）
        edge = np.maximum(edge, (pz - (z_edge[None, :] + 10.0)) * 3.0)

    bk = recipe["bank"]
    bank = bk["height_min"] + (bk["height_max"] - bk["height_min"]) \
        * value_noise_2d(px, pz, bk["wavelength"], rng)
    shore = smoothstep(-rv["shore_in"], rv["shore_out"], edge)
    h = rv["bed_y"] + (bank - rv["bed_y"]) * shore

    hl = recipe["hills"]
    hills = fbm_2d(px, pz, hl["wavelength"], hl["octaves"], hl["persistence"], hl["lacunarity"], rng)
    hills = np.clip((hills - 0.25) / 0.6, 0.0, 1.0) ** hl["sharpness"]  # fBm は 0.5 付近に偏るので広げる
    # ここから後のノイズは rng を後ろで消費するので、上の川・岸・丘の形は変わらない
    if hl["ridge_mix"] > 0.0:
        ridge = ridged_2d(px, pz, hl["ridge_wavelength"], 4, rng)
        hills = hills * (1.0 - hl["ridge_mix"]) + hills * ridge * 1.6 * hl["ridge_mix"]
    land = hl["height"] * hills * smoothstep(hl["start"], hl["start"] + hl["ramp"], edge)

    for m in recipe["mounds"]:
        m = merge(MOUND_DEFAULT, m)
        zw = 0.0
        if m["z_wobble"] > 0.0:   # 範囲の端を列ごとに揺らす（一直線だと真上から目立つ）
            zw = (m["z_wobble"] * (value_noise_1d(xs, m["z_wobble_wavelength"], rng) * 2.0 - 1.0))[None, :]
        zmask = smoothstep(m["z_min"] - m["z_fade"] + zw, m["z_min"] + zw, pz) \
            * (1.0 - smoothstep(m["z_max"] + zw, m["z_max"] + m["z_fade"] + zw, pz))
        if m["side"] == "right":
            zmask = zmask * (sd >= 0.0)
        elif m["side"] == "left":
            zmask = zmask * (sd < 0.0)
        vary = ridged_2d(px, pz, m["wavelength"], 3, rng)
        shape = 1.0 - m["variation"] + m["variation"] * vary
        land = np.maximum(land, m["height"] * shape * zmask * smoothstep(m["start"], m["start"] + m["ramp"], edge))

    tr = recipe["terrace"]
    if tr["step"] > 0.0 and tr["strength"] > 0.0:
        phase = value_noise_2d(px, pz, tr["jitter_wavelength"], rng) * tr["jitter"]
        stepped = terrace(land, tr["step"], tr["riser"], phase)
        where = value_noise_2d(px, pz, tr["mask_wavelength"], rng)
        cov = tr["mask_coverage"]
        where = smoothstep(1.0 - cov - 0.08, 1.0 - cov + 0.08, where) if cov < 1.0 else 1.0
        if tr["step_variation"] > 0.0 or tr["riser_range"][0] != tr["riser_range"][1]:
            step_scale = 1.0 + tr["step_variation"] * (value_noise_2d(px, pz, tr["variation_wavelength"], rng) * 2.0 - 1.0)
            riser = tr["riser_range"][0] + (tr["riser_range"][1] - tr["riser_range"][0]) \
                * value_noise_2d(px, pz, tr["variation_wavelength"] * 0.6, rng)
            stepped = terrace(land, tr["step"] * step_scale, riser, phase)
        # 低い所（岸に近い草原）まで段にすると畑の畝になるので、1 段目より上だけ効かせる
        land += (np.maximum(stepped, 0.0) - land) * tr["strength"] * where * smoothstep(0.3 * tr["step"], tr["step"], land)

    # 縁の向こうの台地：川沿いの丘と入れ替え、奥へ上っていく草原にする
    pl = recipe["plateau"]
    if pl["enabled"]:
        prng = np.random.default_rng(int(recipe["seed"]) + 51)
        roll = fbm_2d(px, pz, pl["wavelength"], 4, 0.5, 2.0, prng)
        roll = (roll - roll.min()) / max(roll.max() - roll.min(), 1e-9)
        plateau = pl["rise"] * smoothstep(pl["rise_z"][0], pl["rise_z"][1], pz) + pl["amp"] * roll
        z0 = pl["z_start"] + pl["z_wobble"] * (value_noise_1d(xs, pl["z_wobble_wavelength"], prng) * 2.0 - 1.0)
        w = smoothstep(z0[None, :], z0[None, :] + pl["blend"], pz)   # 境目を列ごとに揺らす（一直線だと真上から目立つ）
        land = land + (plateau - land) * w
    h += land

    dt = recipe["detail"]
    if dt["amp"] > 0.0:
        n = normalize_signed(fbm_2d(px, pz, dt["wavelength"], dt["octaves"], 0.5, 2.0, rng))
        h += dt["amp"] * n * smoothstep(dt["start"], dt["start"] + dt["ramp"], edge)
        # 細かい起伏で草原が水面より下に凹むと、陸の中に水たまりができるので止める
        on_land = edge > rv["shore_out"]
        h = np.where(on_land, np.maximum(h, bk["height_min"] * 0.5), h)

    # 大きい山（細かい起伏より後に盛る。岩の本体を段々にしない）
    peak_rock = np.zeros_like(h)
    for i, pk in enumerate(recipe["peaks"]):
        pk = merge(PEAK_DEFAULT, pk)
        ph, pr = build_peak(px, pz, edge, pk, np.random.default_rng(int(recipe["seed"]) + 61 + i))
        h = np.where(ph > 0.0, np.maximum(h, ph), h)   # 山の外（ph = 0）は川底を埋めない
        peak_rock = np.maximum(peak_rock, pr)

    # 台地の縁の崖。岩の判定より前に入れて、崖面にも岩の塗りと塊の凹凸を乗せる
    beyond = np.zeros_like(h)
    canyon_sd = None
    if cl["enabled"]:
        # 川の所は縁を奥へ切り込ませる（滝の口）。崖面は高さ方向にも前後させる
        in_river = 1.0 - smoothstep(-20.0, 10.0, edge)
        jitter = cl["face_jitter"] * normalize_signed(fbm_2d(px, pz, 30.0, 3, 0.5, 2.0, crng))
        z0 = z_edge[None, :] - cl["notch"] * in_river + jitter
        head = smoothstep(z0, z0 + cl["face_width"], pz)
        floor = cl["floor_y"] + 4.0 * normalize_signed(fbm_2d(px, pz, 60.0, 3, 0.5, 2.0, crng))
        cn = recipe["canyon"]
        if cn["enabled"]:
            # 縁の先は谷のレールに沿った溝だけを掘る。壁は段（石を積んだような崖）
            krng = np.random.default_rng(int(recipe["seed"]) + 71)
            cline = build_centerline(rail, cn["rail_index_range"], 0.0, cn["tail_extend"])
            canyon_sd, _ = signed_distance(px, pz, cline)

            def canyon_side():
                n = value_noise_1d(pz, cn["half_width_wavelength"], krng) * 2.0 - 1.0
                return cn["half_width"] + cn["half_width_amp"] * n
            w_right, w_left = canyon_side(), canyon_side()
            top = np.where(canyon_sd >= 0.0, w_right, w_left)
            top = top + cn["wall_noise_amp"] * (ridged_2d(px, pz, cn["wall_noise_wavelength"], 3, krng) * 2.0 - 1.0)
            bottom = top * cn["floor_ratio"]
            u = np.clip((np.abs(canyon_sd) - bottom) / np.maximum(top - bottom, 1e-3), 0.0, 1.0)
            depth = np.maximum(h - floor, 1.0)
            phase = value_noise_2d(px, pz, 120.0, krng) * cn["step_jitter"]
            prof = np.clip(terrace(u * depth, cn["step"], cn["riser"], phase) / depth, 0.0, 1.0)
            # 段の量子化で谷の外の台地まで削らないよう、壁の上端では必ず 1・谷底では 0 に戻す
            prof = prof + (1.0 - prof) * smoothstep(0.95, 1.0, u)
            prof = prof * smoothstep(0.0, 0.05, u)
            beyond = head * (1.0 - prof)
        else:
            beyond = head
        h = h + (floor - h) * beyond

    # 海岸：谷の出口の先で台地を海食崖にして海底へ落とす
    sea = np.zeros_like(h)
    co = recipe["coast"]
    if co["enabled"]:
        srng = np.random.default_rng(int(recipe["seed"]) + 81)
        wob = value_noise_1d(xs, co["wavelength"], srng) * 0.7 + value_noise_1d(xs, co["wavelength"] * 0.35, srng) * 0.3
        zc = co["z"] + co["amp"] * (wob * 2.0 - 1.0)
        sea = smoothstep(zc[None, :], zc[None, :] + co["width"], pz)
        seabed = co["sea_floor_y"] - co["floor_slope"] * np.maximum(pz - zc[None, :], 0.0)
        h = h + (seabed - h) * sea
        beyond = np.maximum(beyond, sea)

    # 岩の所に塊の凹凸を盛る（岩の重みは盛る前の形で決め、スプラットもそれを使う）
    rock = np.maximum(rock_weight(h, edge, px, pz, recipe), peak_rock)
    rk = recipe["rocks"]
    if rk["amp"] > 0.0:
        h = h + rock_blocks(px, pz, rk, int(recipe["seed"]) + 21) * smoothstep(0.15, 0.6, rock)

    # 見えている面の高さ（川の水面 Y=0・海面 sea_y を含む）。簡易レンダと見通しチェック用
    surface = np.where((h < 0.0) & (beyond < 0.02), 0.0, h)
    if co["enabled"]:
        surface = np.where(sea > 0.5, np.maximum(h, co["sea_y"]), surface)

    return {
        "rock": rock,
        "beyond": beyond,
        "sea": sea,
        "surface": surface.astype(np.float32),
        "canyon_sd": canyon_sd,
        "height": h.astype(np.float32), "xs": xs, "zs": zs, "sd": sd, "edge": edge,
        "half_width": half_width, "line": line, "rail": rail, "tangent": tangent, "pz": pz,
    }


def sample_bilinear(grid: np.ndarray, xs: np.ndarray, zs: np.ndarray, x: float, z: float) -> float:
    cell = xs[1] - xs[0]
    fx = (x - xs[0]) / cell
    fz = (zs[0] - z) / cell
    if not (0 <= fx < len(xs) - 1 and 0 <= fz < len(zs) - 1):
        return float("nan")
    ix, iz = int(fx), int(fz)
    tx, tz = fx - ix, fz - iz
    return float((grid[iz, ix] * (1 - tx) + grid[iz, ix + 1] * tx) * (1 - tz)
                 + (grid[iz + 1, ix] * (1 - tx) + grid[iz + 1, ix + 1] * tx) * tz)


# ---------------------------------------------------------------- 出力

def hillshade(h: np.ndarray, cell: float) -> np.ndarray:
    gz, gx = np.gradient(h, cell)
    gz = -gz   # 行は -Z 方向に進むので符号を戻す
    n = np.stack([-gx, np.ones_like(h), -gz], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    light = np.array([-0.5, 0.7, 0.5])
    light /= np.linalg.norm(light)
    return np.clip(n @ light, 0.0, 1.0)


def make_preview(res: dict, cell: float) -> Image.Image:
    h = res["height"]
    shade = hillshade(h, cell)[..., None]
    # 高さで色分け：水面下 = 青、草原 = 緑、丘 = 黄土
    water = np.array([0.15, 0.30, 0.55])
    grass = np.array([0.35, 0.55, 0.25])
    hill = np.array([0.60, 0.52, 0.38])
    gorge = np.array([0.45, 0.38, 0.33])
    t = np.clip(h / 60.0, 0.0, 1.0)[..., None]
    land = grass * (1 - t) + hill * t
    beyond = (res["beyond"] > 0.02)[..., None]
    land = np.where(beyond & (h < 0.0)[..., None], gorge, land)
    surf = res["surface"]
    is_water = (surf > h + 0.01)[..., None]   # 水面が地面より上にある所
    color = np.where(is_water, water, land) * (0.35 + 0.65 * shade)
    img = Image.fromarray((np.clip(color, 0, 1) * 255).astype(np.uint8), "RGB")
    img = img.resize((img.width * PREVIEW_SCALE, img.height * PREVIEW_SCALE), Image.NEAREST)

    xs, zs = res["xs"], res["zs"]
    def to_px(x, z):
        return ((x - xs[0]) / cell * PREVIEW_SCALE, (zs[0] - z) / cell * PREVIEW_SCALE)

    draw = ImageDraw.Draw(img)
    # 川の縁（edge = 0 の境）を黄色の点で
    border = np.abs(res["edge"]) < cell * 0.5
    for iz, ix in zip(*np.nonzero(border)):
        draw.point((ix * PREVIEW_SCALE, iz * PREVIEW_SCALE), fill=(240, 220, 60))
    # 中心線（細） と レールの制御点（番号付き）
    draw.line([to_px(x, z) for x, z in res["line"]], fill=(255, 255, 255), width=1)
    for i, p in enumerate(res["rail"]):
        x, y, z = p
        if not (zs[-1] <= z <= zs[0]):
            continue
        cx, cy = to_px(x, z)
        col = (230, 40, 40) if y > 100 else (255, 140, 0)
        draw.ellipse((cx - 3, cy - 3, cx + 3, cy + 3), fill=col)
        if i % 2 == 1:
            draw.text((cx + 6, cy - 6), f"{i}", fill=(255, 255, 255))
    return img


def rail_clearance(res: dict, idx_from: int, idx_to: int, subdiv: int = 40):
    """レール（Catmull-Rom）を細かくたどって、レールの高さ − 地面（水面より下なら水面 0）の最小を返す"""
    rail = res["rail"]
    worst = (float("inf"), None)
    for seg in range(idx_from, idx_to):
        p0, p1 = np.array(rail[max(seg - 1, 0)]), np.array(rail[seg])
        p2, p3 = np.array(rail[seg + 1]), np.array(rail[min(seg + 2, len(rail) - 1)])
        for k in range(subdiv):
            t = k / subdiv
            p = 0.5 * (2 * p1 + (-p0 + p2) * t + (2 * p0 - 5 * p1 + 4 * p2 - p3) * t * t
                       + (-p0 + 3 * p1 - 3 * p2 + p3) * t ** 3)
            g = sample_bilinear(res["height"], res["xs"], res["zs"], p[0], p[2])
            if math.isnan(g):
                continue
            # 縁より手前は水面（Y=0）が上にある。縁の先（谷）は地面そのもの
            b = sample_bilinear(res["beyond"].astype(np.float32), res["xs"], res["zs"], p[0], p[2]) \
                if "beyond" in res else 0.0
            ground = g if b > 0.02 else max(g, 0.0)
            c = p[1] - ground
            if c < worst[0]:
                worst = (c, (round(seg + t, 2), round(float(p[0]), 1), round(float(p[1]), 1), round(float(p[2]), 1)))
    return worst


def report(res: dict, cell: float) -> None:
    h = res["height"]
    print(f"grid     : {h.shape[1]} x {h.shape[0]}  ({h.size:,} 頂点, {cell}m)")
    print(f"height   : {h.min():.2f} .. {h.max():.2f} m")
    print(f"水面下の割合: {(h < 0).mean() * 100:.1f} %")
    print("\n idx   rail(X, Y, Z)              地面Y   半幅   レール-地面")
    for i, (x, y, z) in enumerate(res["rail"]):
        g = sample_bilinear(h, res["xs"], res["zs"], x, z)
        if math.isnan(g):
            continue
        hw = sample_bilinear(res["half_width"].astype(np.float32), res["xs"], res["zs"], x, z)
        print(f" {i:3d}  ({x:7.1f}, {y:6.1f}, {z:7.1f})  {g:6.2f}  {hw:5.0f}  {y - g:8.1f}")
    c, where = rail_clearance(res, 36, 41)
    print(f"\nレールと地面（水面）の最小の離れ（idx36〜41）: {c:.1f} m  at (idx, X, Y, Z) = {where}")
    if res.get("canyon_sd") is not None:
        c, where = rail_clearance(res, 41, len(res["rail"]) - 1)
        print(f"谷のレールと谷底の最小の離れ（idx41〜）: {c:.1f} m  at {where}")
        side, at = canyon_side_room(res)
        print(f"谷のレールの高さでの壁までの横の余裕（左右の小さい方）: 最小 {side:.0f} m  at idx {at}")


def canyon_side_room(res: dict, s_from: float = 41.0, step: float = 0.25):
    """谷のレールの高さで、レールから左右の壁（地面がレールより高くなる所）までの横の距離の最小"""
    rail = res["rail"]
    worst = (float("inf"), None)
    s = s_from
    while s <= len(rail) - 1:
        p = rail_sample(rail, s)
        f = rail_forward(rail, s)
        nrm = np.array([-f[2], f[0]])
        nrm /= max(np.linalg.norm(nrm), 1e-9)
        room = []
        for sign in (1.0, -1.0):
            d = 0.0
            while d < 400.0:
                g = sample_bilinear(res["height"], res["xs"], res["zs"], p[0] + sign * nrm[0] * d, p[2] + sign * nrm[1] * d)
                if math.isnan(g) or g > p[1]:
                    break
                d += 1.0
            room.append(d)
        if min(room) < worst[0]:
            worst = (min(room), round(s, 2))
        s += step
    return worst

# ---------------------------------------------------------------- glTF（cook_assets.py の RH→LH 変換を見越して逆算する）

def surface_normals(h: np.ndarray, cell: float):
    """エンジン空間の法線と、高さの X 勾配を返す。"""
    d_row, d_col = np.gradient(h.astype(np.float64), cell)
    dhdx = d_col
    dhdz = -d_row   # 行は -Z 方向に進む
    n = np.stack([-dhdx, np.ones_like(dhdx), -dhdz], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    return n, dhdx


def build_gltf(res: dict, cell: float, name: str, base_color_uri: str, material: dict,
               tint_uri: str, normals=None) -> tuple[dict, bytes]:
    """normals: 切り分ける前の全体で求めた (法線, X 勾配)。塊の端で片側差分になって境目の陰影がずれるのを防ぐ"""
    h = res["height"].astype(np.float64)
    rows, cols = h.shape
    X, Z = np.meshgrid(res["xs"], res["zs"])
    n, dhdx = normals if normals is not None else surface_normals(h, cell)

    # 接線 = dP/du（+X 方向）を法線に直交化。従法線 dP/dv は -Z 向きなので handedness は +1
    t = np.stack([np.ones_like(h), dhdx, np.zeros_like(h)], axis=-1)
    t -= n * np.sum(n * t, axis=-1, keepdims=True)
    t /= np.linalg.norm(t, axis=-1, keepdims=True)

    u = np.broadcast_to(np.arange(cols) / (cols - 1), h.shape)
    v = np.broadcast_to((np.arange(rows) / (rows - 1))[:, None], h.shape)

    # cook は「x を反転・法線 x を反転・v = 1 - v・tangent の x と w を反転・三角形の向きを反転」する。
    # その逆を書いておけば、エンジン上で上の値になる。
    pos = np.stack([-X, h, Z], axis=-1).reshape(-1, 3).astype(np.float32)
    nrm = np.stack([-n[..., 0], n[..., 1], n[..., 2]], axis=-1).reshape(-1, 3).astype(np.float32)
    tan = np.stack([-t[..., 0], t[..., 1], t[..., 2], -np.ones_like(h)], axis=-1).reshape(-1, 4).astype(np.float32)
    uv = np.stack([u, 1.0 - v], axis=-1).reshape(-1, 2).astype(np.float32)

    r, c = np.meshgrid(np.arange(rows - 1), np.arange(cols - 1), indexing="ij")
    i00 = (r * cols + c).ravel()
    i01, i10, i11 = i00 + 1, i00 + cols, i00 + cols + 1
    # glTF（RH）で上から見て反時計回り＝表が +Y。cook の向きの反転と X 反転で LH でも表のまま
    idx = np.stack([i00, i10, i11, i00, i11, i01], axis=-1).ravel().astype(np.uint32)
    a, b, cc = pos[idx[0]], pos[idx[1]], pos[idx[2]]
    assert np.cross(b - a, cc - a)[1] > 0.0, "三角形の向きが逆"

    blob = bytearray()
    views = []

    def add_view(data: bytes, target: int) -> int:
        while len(blob) % 4:
            blob.append(0)
        views.append({"buffer": 0, "byteOffset": len(blob), "byteLength": len(data), "target": target})
        blob.extend(data)
        return len(views) - 1

    vc = pos.shape[0]
    accessors = [
        {"bufferView": add_view(pos.tobytes(), 34962), "componentType": 5126, "count": vc, "type": "VEC3",
         "min": pos.min(axis=0).tolist(), "max": pos.max(axis=0).tolist()},
        {"bufferView": add_view(nrm.tobytes(), 34962), "componentType": 5126, "count": vc, "type": "VEC3"},
        {"bufferView": add_view(tan.tobytes(), 34962), "componentType": 5126, "count": vc, "type": "VEC4"},
        {"bufferView": add_view(uv.tobytes(), 34962), "componentType": 5126, "count": vc, "type": "VEC2"},
        {"bufferView": add_view(idx.tobytes(), 34963), "componentType": 5125, "count": int(idx.size), "type": "SCALAR"},
    ]
    gltf = {
        "asset": {"version": "2.0", "generator": "gen_terrain.py"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": name}],
        "meshes": [{"name": name, "primitives": [{
            "attributes": {"POSITION": 0, "NORMAL": 1, "TANGENT": 2, "TEXCOORD_0": 3},
            "indices": 4, "material": 0}]}],
        "materials": [{
            "name": name,
            "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}, "metallicFactor": 0.0, "roughnessFactor": 0.95},
            # 地形シェーダは法線マップを使わないので、t2 の枠に色合いのマップを入れる
            "normalTexture": {"index": 1},
            # cook_assets.py がこれを見て .mat v5（shadingModel = 2）を書く
            "extras": {"terrainLayers": {
                "colorArray": material["color_array"],
                "normalArray": material["normal_array"],
                "tile": material["tile"],
                "roughness": material["roughness"],
                "heightBlend": material["height_blend"],
                "triplanarSharpness": material["triplanar_sharpness"],
                "macroVariation": material["macro_variation"],
                "wetness": material["wetness"],
                "environment": material["environment"],
            }},
        }],
        "textures": [{"source": 0}, {"source": 1}],
        "images": [{"uri": base_color_uri}, {"uri": tint_uri}],
        "accessors": accessors,
        "bufferViews": views,
        "buffers": [{"uri": f"{name}.bin", "byteLength": len(blob)}],
    }
    return gltf, bytes(blob)


def make_temp_color(res: dict, cell: float, size: int | tuple[int, int]) -> Image.Image:
    """簡易レンダ（make_views）用の色。高さと傾斜で塗り分ける（陰影は焼かない）。向きは UV と同じ（行 0 = z_max）。"""
    h = res["height"]
    n, _ = surface_normals(h, cell)
    slope = 1.0 - n[..., 1]   # 0 = 水平

    sw, sh = (size, size) if isinstance(size, int) else size

    def up(a: np.ndarray) -> np.ndarray:
        return np.asarray(Image.fromarray(a.astype(np.float32), "F").resize((sw, sh), Image.BILINEAR))
    hh, sl, ed = up(h), up(slope), up(res["edge"])

    rng = np.random.default_rng(0)
    yy, xx = np.mgrid[0:sh, 0:sw].astype(np.float64)
    mottle = value_noise_2d(xx, yy, sw / 40.0, rng)[..., None]

    bed = np.array([0.16, 0.20, 0.17])
    sand = np.array([0.58, 0.52, 0.40])
    grass = np.array([0.30, 0.46, 0.17])
    dry = np.array([0.50, 0.50, 0.27])
    rock = np.array([0.42, 0.40, 0.37])
    col = grass * (0.85 + 0.3 * mottle)
    col = col + (dry - col) * smoothstep(15.0, 45.0, hh)[..., None]
    # 砂は水際の帯だけ（高さで塗ると内陸の低い所まで砂になる）
    col = col + (sand - col) * (1.0 - smoothstep(0.0, 12.0, ed))[..., None]
    col = col + (bed - col) * (1.0 - smoothstep(-1.0, 0.0, hh))[..., None]
    col = col + (rock - col) * smoothstep(0.25, 0.45, sl)[..., None]
    return Image.fromarray((np.clip(col, 0.0, 1.0) * 255).astype(np.uint8), "RGB")


def rock_weight(h: np.ndarray, edge: np.ndarray, X: np.ndarray, Z: np.ndarray, recipe: dict) -> np.ndarray:
    """岩の重み（0..1）。急斜面（しきい値をノイズで揺らす）＋草原の露頭。水際には出さない"""
    sp = recipe["splat"]
    n, _ = surface_normals(h, float(recipe["cell_m"]))
    slope = 1.0 - n[..., 1]
    # 形とは別の乱数（地形の形の乱数の消費順を変えない）
    rng = np.random.default_rng(int(recipe["seed"]) + 7)
    shift = 0.0
    if sp["rock_jitter"] > 0.0:
        shift = sp["rock_jitter"] * normalize_signed(fbm_2d(X, Z, sp["rock_jitter_wavelength"], 3, 0.5, 2.0, rng))
    rock = smoothstep(sp["rock_slope"][0] + shift, sp["rock_slope"][1] + shift, slope)
    if sp["outcrop_coverage"] > 0.0:
        oc = sp["outcrop_coverage"]
        n_out = fbm_2d(X, Z, sp["outcrop_wavelength"], 3, 0.5, 2.0, rng)
        n_out = (n_out - n_out.min()) / max(n_out.max() - n_out.min(), 1e-9)
        outcrop = smoothstep(1.0 - oc - 0.03, 1.0 - oc + 0.03, n_out) \
            * smoothstep(sp["outcrop_min_slope"], sp["outcrop_min_slope"] + 0.05, slope)
        rock = np.maximum(rock, outcrop * smoothstep(5.0, 25.0, edge))
    return rock


def worley_blocks(X: np.ndarray, Z: np.ndarray, cell: float, seed: int):
    """ワールド空間のボロノイ（ジッタ付き格子）。塊の番号（乱数表の添字）、塊の点からの相対位置、1・2 番目の距離"""
    table = np.random.default_rng(seed).random((8192, 4))
    gx, gz = X / cell, Z / cell
    ix, iz = np.floor(gx).astype(np.int64), np.floor(gz).astype(np.int64)
    d1 = np.full(X.shape, np.inf)
    d2 = np.full(X.shape, np.inf)
    key = np.zeros(X.shape, dtype=np.int64)
    rel = np.zeros(X.shape + (2,))
    for oz in (-1, 0, 1):
        for ox in (-1, 0, 1):
            cx, cz = ix + ox, iz + oz
            k = (cx * 1619 + cz * 31337 + seed * 6971) % 8192
            px, pz = cx + table[k, 0], cz + table[k, 1]
            dx, dz = gx - px, gz - pz
            d = np.sqrt(dx * dx + dz * dz)
            closer = d < d1
            d2 = np.where(closer, d1, np.minimum(d2, d))
            d1 = np.where(closer, d, d1)
            key = np.where(closer, k, key)
            rel[..., 0] = np.where(closer, dx, rel[..., 0])
            rel[..., 1] = np.where(closer, dz, rel[..., 1])
    return key, rel, d1, d2, table


def rock_blocks(X: np.ndarray, Z: np.ndarray, rk: dict, seed: int) -> np.ndarray:
    """角ばった岩の塊の高さ [m]（0 以上）。塊ごとに高さと面の傾き、境は割れ目"""
    key, rel, d1, d2, table = worley_blocks(X, Z, rk["cell"], seed)
    base = 0.35 + 0.65 * table[key, 2]
    tilt_x = (table[key, 3] - 0.5) * 2.0 * rk["tilt"]
    tilt_z = (table[(key * 7 + 3) % 8192, 3] - 0.5) * 2.0 * rk["tilt"]
    facet = base + tilt_x * rel[..., 0] + tilt_z * rel[..., 1]
    crack = 1.0 - smoothstep(0.0, rk["crack"], d2 - d1)
    return np.maximum(rk["amp"] * (facet - rk["crack_depth"] * crack), 0.0)


def build_splat(res: dict, recipe: dict) -> np.ndarray:
    """R = 草、G = 土・砂、B = 岩、A = 濡れ（行 0 = z_max）。RGB は合計 1。"""
    sp = recipe["splat"]
    h = res["height"].astype(np.float64)
    X, Z = np.meshgrid(res["xs"], res["zs"])
    # 岩は凹凸を盛る前の形で決めたもの（盛った所と岩の塗りを一致させる）
    rock = res["rock"] if "rock" in res else rock_weight(h, res["edge"], X, Z, recipe)
    rng = np.random.default_rng(int(recipe["seed"]) + 8)
    sand = 1.0 - smoothstep(0.0, sp["sand_band"], res["edge"])
    patch = value_noise_2d(X, Z, sp["dirt_wavelength"], rng)
    cov = sp["dirt_coverage"]
    dirt_patch = smoothstep(1.0 - cov - 0.05, 1.0 - cov + 0.05, patch)
    dirt = np.maximum(sand, dirt_patch) * (1.0 - rock)
    grass = 1.0 - rock - dirt
    wet = 1.0 - smoothstep(sp["wet_height"][0], sp["wet_height"][1], h)
    wet = wet * (1.0 - res.get("beyond", 0.0))   # 崖面と谷底は水際ではない
    return np.stack([grass, dirt, rock, wet], axis=-1)


def build_tint(res: dict, recipe: dict, splat: np.ndarray) -> np.ndarray:
    """地形全体の色合い（アルベドへの乗数、RGB）。地形の形と結びつける：
    尾根・日なた・高い所は乾いた黄色、谷・窪み・水辺は濃い緑、岩は赤茶と灰の大きな範囲"""
    tn = recipe["tint"]
    cell = float(recipe["cell_m"])
    h = res["height"].astype(np.float64)
    X, Z = np.meshgrid(res["xs"], res["zs"])
    rng = np.random.default_rng(int(recipe["seed"]) + 31)
    n, _ = surface_normals(h, cell)

    # 曲率：周りの平均より高い＝尾根（+）、低い＝谷（−）。ぼかした高さとの差で測る
    k = max(1, int(round(tn["curvature_radius"] / cell)))
    blur = h.copy()
    for axis in (0, 1):
        acc = np.zeros_like(blur)
        for o in range(-k, k + 1):
            acc += np.roll(blur, o, axis=axis)
        blur = acc / (2 * k + 1)
    ridge = np.clip((h - blur) / 3.0, -1.0, 1.0)

    sun = np.array(SUN_TOWARD) / np.linalg.norm(SUN_TOWARD)
    sunny = np.clip(n @ sun, 0.0, 1.0)
    high = smoothstep(10.0, 90.0, h)
    near_water = 1.0 - smoothstep(0.0, 60.0, res["edge"])
    big = normalize_signed(fbm_2d(X, Z, tn["dry_wavelength"], 4, 0.5, 2.0, rng))
    dryness = np.clip(0.4 + 0.35 * ridge + 0.25 * (sunny - 0.7) + 0.25 * high + 0.35 * big - 0.45 * near_water, 0.0, 1.0)

    lush, dry = np.array(tn["lush"]), np.array(tn["dry"])
    grass = lush + (dry - lush) * smoothstep(0.15, 0.85, dryness)[..., None]
    # 小さな斑で色合いをずらす（オリーブ ↔ 青緑）
    hue = normalize_signed(fbm_2d(X, Z, tn["hue_wavelength"], 4, 0.55, 2.0, rng))
    olive, teal = np.array(tn["olive"]), np.array(tn["teal"])
    grass = grass * np.where((hue > 0)[..., None], 1.0 + (olive - 1.0) * hue[..., None],
                             1.0 + (teal - 1.0) * (-hue)[..., None])

    rock_side = smoothstep(-0.3, 0.3, normalize_signed(fbm_2d(X, Z, 260.0, 3, 0.5, 2.0, rng)))
    rock = np.array(tn["rock_cool"]) + (np.array(tn["rock_warm"]) - np.array(tn["rock_cool"])) * rock_side[..., None]
    # 土・砂はそのまま（草の色合いを乗せると砂が緑がかる）。スプラットの重みで混ぜる
    wg, wd, wr = splat[..., 0:1], splat[..., 1:2], splat[..., 2:3]
    return grass * wg + 1.0 * wd + rock * wr


def export_gltf(res: dict, recipe: dict, project_root: Path, name: str,
                splat_grid: np.ndarray, tint_grid: np.ndarray, normals) -> None:
    """1 つの塊を glTF にする。スプラット・色合い・法線は切り分ける前の全体で求めたものを切り出して渡す"""
    cell = float(recipe["cell_m"])
    assets = project_root / "Assets"
    tex_rel = f"Textures/MaskTexture/Terrain/{name}_Splat.png"
    (assets / tex_rel).parent.mkdir(parents=True, exist_ok=True)
    splat = resize_channels(splat_grid, int(recipe["splat"]["size"]))
    Image.fromarray((np.clip(splat, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA").save(assets / tex_rel)

    out_dir = assets / "Models" / name
    out_dir.mkdir(parents=True, exist_ok=True)
    tint_rel = f"Textures/MaskTexture/Terrain/{name}_Tint.png"
    tint = resize_channels(tint_grid, int(recipe["tint"]["size"]))
    Image.fromarray((np.clip(tint * 0.5, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB").save(assets / tint_rel)

    gltf, blob = build_gltf(res, cell, name, base_color_uri=f"../../{tex_rel}", material=recipe["material"],
                            tint_uri=f"../../{tint_rel}", normals=normals)
    (out_dir / f"{name}.bin").write_bytes(blob)
    (out_dir / f"{name}.gltf").write_text(json.dumps(gltf, indent=2), encoding="utf-8")
    print(f"glTF     : {out_dir / (name + '.gltf')}  ({len(blob) / 1e6:.1f} MB)")


def run_cook(project_root: Path, names: list[str]) -> None:
    result = subprocess.run([sys.executable, str(project_root / "tools" / "Python" / "cook_assets.py")],
                            cwd=str(project_root), capture_output=True, text=True)
    for line in result.stdout.splitlines():
        if any(n in line for n in names):
            print(line)
    if result.returncode != 0:
        print(result.stderr.rstrip(), file=sys.stderr)
        raise SystemExit(f"cook_assets.py failed (exit {result.returncode})")


def write_scene_entry(project_root: Path, name: str, stream_group: str) -> None:
    """シーン JSON の name エントリだけを書き換える（11 番 §3.1 の 4.：キー単位の所有）。"""
    scene = project_root / SCENE_PATH
    doc = json.loads(scene.read_text(encoding="utf-8"))
    entry = {
        "type": "Object3D",
        "name": name,
        "tag": "Terrain",
        "dir": f"Resources/Models/{name}",
        "file": f"{name}.mesh",
        "streamGroup": stream_group,
        "transform": {"scale": [1, 1, 1], "rotate": [0, 0, 0], "translate": [0, 0, 0]},
    }
    objs = doc.setdefault("objects", [])
    for i, o in enumerate(objs):
        if o.get("name") == name:
            objs[i] = entry
            action = "更新"
            break
    else:
        objs.append(entry)
        action = "追加"
    bak = scene.with_name(scene.name + ".bak")
    if not bak.exists():
        shutil.copyfile(scene, bak)
    scene.write_text(json.dumps(doc, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"scene    : {SCENE_PATH} の {name} を{action}")



def render_view(res: dict, cell: float, color: np.ndarray, eye, yaw_deg: float, pitch_deg: float,
                width: int = 960, height: int = 540, fov_deg: float = 70.0, far: float = 3000.0) -> Image.Image:
    """高さマップを列ごとに手前から積む簡易レンダ（voxel space 方式）。見下ろしは画面のずらしで近似する。
    地形の外は描かないので、範囲の端がそのまま見える（端の確認用）。"""
    h = res["height"]
    xs, zs = res["xs"], res["zs"]
    surface = res["surface"]   # 川の水面・海面を含む
    rows, cols = h.shape
    focal = (width * 0.5) / math.tan(math.radians(fov_deg) * 0.5)
    horizon = height * 0.5 - focal * math.tan(math.radians(pitch_deg))
    ex, ey, ez = eye
    yaw = math.radians(yaw_deg)
    ang = yaw + np.arctan((np.arange(width) - width * 0.5) / focal)
    sin_a, cos_a, cos_rel = np.sin(ang), np.cos(ang), np.cos(ang - yaw)

    sky_top, sky_hor = np.array([0.30, 0.55, 0.90]), np.array([0.75, 0.85, 0.95])
    t = np.clip(np.arange(height) / max(horizon, 1.0), 0.0, 1.0)[:, None, None]
    img = np.broadcast_to(sky_top * (1 - t) + sky_hor * t, (height, width, 3)).copy()
    water = np.array([0.20, 0.36, 0.48])

    ybuf = np.full(width, float(height))
    row_idx = np.arange(height)[:, None]
    d = 2.0
    while d < far:
        wx, wz = ex + sin_a * d, ez + cos_a * d
        fx = (wx - xs[0]) / cell
        fz = (zs[0] - wz) / cell
        ix, iz = np.round(fx).astype(int), np.round(fz).astype(int)
        valid = (ix >= 0) & (ix < cols) & (iz >= 0) & (iz < rows)
        ixc, izc = np.clip(ix, 0, cols - 1), np.clip(iz, 0, rows - 1)
        hv = h[izc, ixc]
        surf = surface[izc, ixc]
        is_water = surf > hv + 0.01
        col = np.where(is_water[:, None], water, color[izc, ixc])
        haze = 1.0 - np.exp(-d / 2500.0)
        col = col + (sky_hor - col) * haze
        y = horizon + (ey - surf) / (d * cos_rel) * focal
        y = np.clip(y, 0.0, height)
        draw = valid & (y < ybuf)
        if draw.any():
            mask = (row_idx >= y[None, :]) & (row_idx < ybuf[None, :]) & draw[None, :]
            img[mask] = np.broadcast_to(col[None, :, :], img.shape)[mask]
            ybuf = np.where(draw, y, ybuf)
        d *= 1.01
    return Image.fromarray((np.clip(img, 0.0, 1.0) * 255).astype(np.uint8), "RGB")


def view_colors(res: dict, cell: float) -> np.ndarray:
    """簡易レンダ用の色（仮の色 × 太陽の陰影）。行 0 = z_max"""
    rows, cols = res["height"].shape
    base = np.asarray(make_temp_color(res, cell, (cols, rows)), dtype=np.float64) / 255.0
    n = surface_normals(res["height"], cell)[0]
    sun = np.array(SUN_TOWARD) / np.linalg.norm(SUN_TOWARD)
    shade = 0.35 + 0.65 * np.clip(n @ sun, 0.0, 1.0)
    return base * shade[..., None]


def make_views(res: dict, cell: float, out_dir: Path, name: str, color: np.ndarray) -> None:
    for view_name, idx, y_override, yaw, pitch in VIEWS:
        x, y, z = res["rail"][idx]
        eye = (x, y if y_override is None else y_override, z)
        if yaw is None:
            f = rail_forward(res["rail"], float(idx))
            yaw = math.degrees(math.atan2(f[0], f[2]))
        render_view(res, cell, color, eye, yaw, pitch).save(out_dir / f"{name}_View_{view_name}.png")


# ---------------------------------------------------------------- 見通しチェック（L9 の谷のロード。11 番 §3.0）

def raycast_frame(res: dict, cell: float, eye: np.ndarray, forward: np.ndarray, pitch_bias: float,
                  fov_y: float, aspect: float, width: int, far: float = 4000.0):
    """実機と同じ視野で高さマップへ光線を飛ばし、各画素が当たったかと、最初に当たった点（X, Z, 面の Y）を返す。
    見える面は res["surface"]（川の水面・海面を含む）。最寄りの格子点で引く簡易版"""
    height = int(round(width / aspect))
    surface = res["surface"]
    xs, zs = res["xs"], res["zs"]
    rows, cols = surface.shape
    yaw = math.atan2(forward[0], forward[2])
    pitch = math.asin(max(-1.0, min(1.0, forward[1]))) + pitch_bias
    ty = math.tan(fov_y * 0.5)
    tx = ty * aspect
    u = ((np.arange(width) + 0.5) / width * 2.0 - 1.0) * tx
    v = (1.0 - (np.arange(height) + 0.5) / height * 2.0) * ty
    U, V = np.meshgrid(u, v)
    # カメラ空間（+Z 前、+Y 上）→ ピッチ（上向きが正）→ ヨー
    d = np.stack([U, V, np.ones_like(U)], axis=-1)
    d /= np.linalg.norm(d, axis=-1, keepdims=True)
    cp, sp = math.cos(pitch), math.sin(pitch)
    dy = d[..., 1] * cp + d[..., 2] * sp
    dz = -d[..., 1] * sp + d[..., 2] * cp
    cy, sy = math.cos(yaw), math.sin(yaw)
    dx = d[..., 0] * cy + dz * sy
    dz = -d[..., 0] * sy + dz * cy
    dx, dy, dz = dx.ravel(), dy.ravel(), dz.ravel()
    hit = np.zeros(dx.size, dtype=bool)
    hx = np.full(dx.size, np.nan)
    hz = np.full(dx.size, np.nan)
    hy = np.full(dx.size, np.nan)
    alive = np.ones(dx.size, dtype=bool)
    t = 0.5
    while t < far and alive.any():
        idx = np.nonzero(alive)[0]
        x = eye[0] + dx[idx] * t
        y = eye[1] + dy[idx] * t
        z = eye[2] + dz[idx] * t
        ix = np.round((x - xs[0]) / cell).astype(np.int64)
        iz = np.round((zs[0] - z) / cell).astype(np.int64)
        inside = (ix >= 0) & (ix < cols) & (iz >= 0) & (iz < rows)
        g = np.full(idx.size, -np.inf)
        g[inside] = surface[iz[inside], ix[inside]]
        h_now = inside & (y <= g)
        k = idx[h_now]
        hit[k] = True
        hx[k] = x[h_now]
        hz[k] = z[h_now]
        hy[k] = g[h_now]
        alive[k] = False
        t = t * 1.012 + 0.2
    shape = (height, width)
    return hit.reshape(shape), hx.reshape(shape), hz.reshape(shape), hy.reshape(shape)


def visibility_check(res: dict, recipe: dict, out_dir: Path, name: str, color: np.ndarray) -> None:
    vs = recipe["visibility"]
    cell = float(recipe["cell_m"])
    rail = res["rail"]
    xs, zs = res["xs"], res["zs"]
    snaps = set(int(s) for s in vs["snap_secs"])
    w0, w1 = vs["pitch_window"]
    print(f"\n見通しチェック: 画面に占める割合  左 = Z > {vs['hide_z']:.0f}（谷側の地形の塊）"
          f" / 右 = Z > {vs['interior_z']:.0f} かつ Y < {vs['interior_y']:.0f}（谷の中＝谷の物を置く所）")
    t = float(vs["t_from"])
    rows_out = []
    while t <= vs["t_to"] + 1e-6:
        s = t / gen_rail_path.SEC_PER_POINT
        eye = rail_sample(rail, s)
        fwd = rail_forward(rail, s)
        bias = math.radians(vs["pitch_bias_deg"]) * smoothstep(w0, w0 + 3.0, np.array(t)) \
            * (1.0 - smoothstep(w1 - 3.0, w1, np.array(t)))
        hit, hx, hz, hy = raycast_frame(res, cell, eye, fwd, float(bias), vs["fov_y"], vs["aspect"], int(vs["width"]))
        hidden = hit & (hz > vs["hide_z"])
        interior = hit & (hz > vs["interior_z"]) & (hy < vs["interior_y"])
        frac = float(hidden.mean())
        frac_in = float(interior.mean())
        rows_out.append((t, frac, frac_in))
        mark = "  ← 谷の中が見える" if frac_in > 0.0 else ""
        print(f"  {t:5.0f}s  ({eye[0]:6.0f}, {eye[1]:5.1f}, {eye[2]:6.0f})  {frac * 100:6.2f} %  {frac_in * 100:6.2f} %{mark}")
        if int(round(t)) in snaps:
            img = np.empty(hit.shape + (3,))
            img[:] = np.array([0.62, 0.78, 0.95])
            ix = np.clip(np.round((np.nan_to_num(hx) - xs[0]) / cell).astype(int), 0, len(xs) - 1)
            iz = np.clip(np.round((zs[0] - np.nan_to_num(hz)) / cell).astype(int), 0, len(zs) - 1)
            col = color[iz, ix]
            water = res["surface"][iz, ix] > res["height"][iz, ix] + 0.01
            col = np.where(water[..., None], np.array([0.20, 0.36, 0.48]), col)
            dist = np.sqrt((np.nan_to_num(hx) - eye[0]) ** 2 + (np.nan_to_num(hz) - eye[2]) ** 2)
            haze = (1.0 - np.exp(-dist / 2500.0))[..., None]
            col = col + (np.array([0.75, 0.85, 0.95]) - col) * haze
            img = np.where(hit[..., None], col, img)
            img = np.where(hidden[..., None], img * 0.55 + np.array([1.0, 0.0, 1.0]) * 0.45, img)
            img = np.where(interior[..., None], img * 0.3 + np.array([1.0, 0.85, 0.0]) * 0.7, img)
            out = Image.fromarray((np.clip(img, 0, 1) * 255).astype(np.uint8), "RGB")
            out = out.resize((out.width * 4, out.height * 4), Image.NEAREST)
            out.save(out_dir / f"{name}_Vis_{int(round(t)):03d}s.png")
        t += 1.0
    for col, label in ((1, "谷側の地形の塊"), (2, "谷の中")):
        seen = [r for r in rows_out if r[col] > 0.0]
        if seen:
            print(f"  → {label}が最初に見えるのは {seen[0][0]:.0f}s（{seen[0][col] * 100:.2f} %）")
        else:
            print(f"  → {label}は期間中は見えない")
    print("  画像（_Vis_*.png）：マゼンタ = 谷側の地形の塊、黄 = 谷の中")


# ---------------------------------------------------------------- 川のマップ（L2 → L3）

def distance_from_land(land: np.ndarray, cell: float, max_steps: int) -> np.ndarray:
    """水のセルから一番近い陸までの距離 [m]（4 近傍と 8 近傍を交互に広げる＝八角形の近似）。"""
    dist = np.where(land, 0.0, np.inf)
    reached = land.copy()
    for k in range(1, max_steps + 1):
        g = reached.copy()
        g[1:, :] |= reached[:-1, :]
        g[:-1, :] |= reached[1:, :]
        g[:, 1:] |= reached[:, :-1]
        g[:, :-1] |= reached[:, 1:]
        if k % 2 == 0:
            g[1:, 1:] |= reached[:-1, :-1]
            g[1:, :-1] |= reached[:-1, 1:]
            g[:-1, 1:] |= reached[1:, :-1]
            g[:-1, :-1] |= reached[1:, 1:]
        dist[g & ~reached] = k * cell
        reached = g
    dist[~reached] = (max_steps + 1) * cell
    return dist


def build_river_map(res: dict, cell: float, rm: dict):
    """R = 水深、GB = 流れ、A = 1 の float 配列（行 0 = z_max）と、水深 [m]・流れ [m/s] を返す。"""
    h = res["height"]
    beyond = res.get("beyond", np.zeros_like(h)) > 0.02
    land = h >= 0.0
    # 崖の先（谷）は陸扱いにしない：縁の際まで深いまま流れ落ちる
    from_land = distance_from_land(land & ~beyond, cell, int(math.ceil(max(rm["depth_ramp"], rm["bank_slow"]) / cell)) + 1)

    # 地形の川底は bed_y で止めてあるので、深さは形ではなく水際からの距離で決める（形と色を分ける。§2.3）
    depth = rm["max_depth"] * smoothstep(0.0, rm["depth_ramp"], from_land)

    z = res["pz"]
    speed = rm["base_speed"] + (rm["max_speed"] - rm["base_speed"]) \
        * smoothstep(rm["falls_z"] - rm["accel_length"], rm["falls_z"], z)
    speed = speed * (0.3 + 0.7 * smoothstep(0.0, rm["bank_slow"], from_land))
    speed = np.where(land | beyond, 0.0, speed)
    depth = np.where(beyond, 0.0, depth)
    flow = res["tangent"] * speed[..., None]   # ワールド XZ [m/s]

    rgba = np.empty(h.shape + (4,))
    rgba[..., 0] = np.clip(depth / rm["max_depth"], 0.0, 1.0)
    rgba[..., 1:3] = np.clip(0.5 + 0.5 * flow / rm["max_speed"], 0.0, 1.0)
    rgba[..., 3] = 1.0
    return rgba, depth, flow


def resize_channels(a: np.ndarray, size: int) -> np.ndarray:
    return np.stack([np.asarray(Image.fromarray(a[..., c].astype(np.float32), "F").resize((size, size), Image.BILINEAR))
                     for c in range(a.shape[-1])], axis=-1)


def river_map_preview(depth: np.ndarray, flow: np.ndarray, land: np.ndarray, max_depth: float, max_speed: float) -> Image.Image:
    t = np.clip(depth / max_depth, 0.0, 1.0)[..., None]
    shallow, deep = np.array([0.55, 0.85, 0.85]), np.array([0.05, 0.15, 0.40])
    col = shallow * (1 - t) + deep * t
    col = np.where(land[..., None], np.array([0.35, 0.35, 0.33]), col)
    img = Image.fromarray((col * 255).astype(np.uint8), "RGB")
    img = img.resize((img.width * PREVIEW_SCALE, img.height * PREVIEW_SCALE), Image.NEAREST)
    draw = ImageDraw.Draw(img)
    step = 12
    for iz in range(step // 2, depth.shape[0], step):
        for ix in range(step // 2, depth.shape[1], step):
            if land[iz, ix]:
                continue
            fx, fz = flow[iz, ix]
            k = 20.0 / max_speed   # 最大流速で 20px
            x0, y0 = ix * PREVIEW_SCALE, iz * PREVIEW_SCALE
            x1, y1 = x0 + fx * k, y0 - fz * k   # 画像の上 = +Z
            draw.line([(x0, y0), (x1, y1)], fill=(255, 255, 255), width=1)
            draw.ellipse((x1 - 1.5, y1 - 1.5, x1 + 1.5, y1 + 1.5), fill=(255, 80, 60))
    return img


def export_river_map(res: dict, recipe: dict, project_root: Path, out_dir: Path, name: str, ext: dict) -> None:
    cell = float(recipe["cell_m"])
    rm = recipe["river_map"]
    rgba, depth, flow = build_river_map(res, cell, rm)

    tex_rel = f"Textures/MaskTexture/Terrain/{name}_RiverMap.png"
    tex_path = project_root / "Assets" / tex_rel
    tex_path.parent.mkdir(parents=True, exist_ok=True)
    big = resize_channels(rgba, int(rm["size"]))
    Image.fromarray((np.clip(big, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGBA").save(tex_path)

    land = res["height"] >= 0.0
    river_map_preview(depth, flow, land, rm["max_depth"], rm["max_speed"]).save(out_dir / f"{name}_RiverMapPreview.png")

    info = {
        "name": name,
        "x_min": ext["x_min"], "x_max": ext["x_max"], "z_min": ext["z_min"], "z_max": ext["z_max"],
        "uv": "u = (x - x_min) / (x_max - x_min)、v = (z_max - z) / (z_max - z_min)。地形メッシュの UV・スプラットマップと同じ",
        "waterHeight": 0.0,
        "riverMap": f"Resources/{Path(tex_rel).with_suffix('.dds').as_posix()}",
        "riverMapSize": int(rm["size"]),
        "maxDepth": rm["max_depth"],
        "maxFlowSpeed": rm["max_speed"],
        "channels": "R = 水深 / maxDepth、G = 0.5 + 0.5 * 流れのワールド X / maxFlowSpeed、B = 同 Z、A = 予備（1）",
        "shoreLines": f"{TERRAIN_JSON_DIR}/{name}_Shore.json",
    }
    json_path = project_root / TERRAIN_JSON_DIR / f"{name}.json"
    json_path.parent.mkdir(parents=True, exist_ok=True)
    json_path.write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")

    water = ~land
    print(f"river map: {tex_path.name} {rm['size']}²  水深 最大 {depth[water].max():.1f}m"
          f" / 流速 {np.linalg.norm(flow, axis=-1)[water].min():.1f}..{np.linalg.norm(flow, axis=-1)[water].max():.1f} m/s")
    print(f"json     : {TERRAIN_JSON_DIR}/{name}.json")


# ---------------------------------------------------------------- 岸の線（L2 → L6 / L10）

def sample_bilinear_vec(grid: np.ndarray, xs: np.ndarray, zs: np.ndarray, x: np.ndarray, z: np.ndarray) -> np.ndarray:
    """sample_bilinear の配列版。範囲の外は nan"""
    cell = xs[1] - xs[0]
    fx = (x - xs[0]) / cell
    fz = (zs[0] - z) / cell
    ok = (fx >= 0) & (fx < len(xs) - 1) & (fz >= 0) & (fz < len(zs) - 1)
    ix = np.clip(np.floor(fx).astype(np.int64), 0, len(xs) - 2)
    iz = np.clip(np.floor(fz).astype(np.int64), 0, len(zs) - 2)
    tx, tz = fx - ix, fz - iz
    v = (grid[iz, ix] * (1 - tx) + grid[iz, ix + 1] * tx) * (1 - tz) \
        + (grid[iz + 1, ix] * (1 - tx) + grid[iz + 1, ix + 1] * tx) * tz
    return np.where(ok, v, np.nan)


def extract_shores(res: dict, cell: float, step_m: float) -> tuple[list, list]:
    """中心線を弧長 step_m おきにたどり、左右の法線方向へ進んで最初に陸（高さ 0 以上）になる所を岸とする。
    水際は直前の点との線形補間。中心が水でない所（範囲の外・崖の先）は飛ばす。
    蛇行で川が横（±X）を向く所も拾える（行ごとにたどる旧方式は横向きの所で岸を取り違えた）"""
    h = res["height"]
    xs, zs = res["xs"], res["zs"]
    beyond = res["beyond"].astype(np.float32)
    line = res["line"]
    seg = np.diff(line, axis=0)
    seg_len = np.linalg.norm(seg, axis=1)
    acc = np.concatenate([[0.0], np.cumsum(seg_len)])
    s = np.arange(0.0, acc[-1], step_m)
    k = np.clip(np.searchsorted(acc, s, side="right") - 1, 0, len(seg) - 1)
    t = (s - acc[k]) / np.maximum(seg_len[k], 1e-9)
    cx = line[k, 0] + seg[k, 0] * t
    cz = line[k, 1] + seg[k, 1] * t
    tdir = seg[k] / np.maximum(seg_len[k], 1e-9)[:, None]
    hc = sample_bilinear_vec(h, xs, zs, cx, cz)
    bc = sample_bilinear_vec(beyond, xs, zs, cx, cz)
    valid = ~np.isnan(hc) & (hc < 0.0) & (bc < 0.02)

    def walk(nx_: np.ndarray, nz_: np.ndarray) -> list:
        found = np.full(s.size, np.nan)
        prev = hc.copy()
        alive = valid.copy()
        dstep = cell * 0.5
        d = dstep
        while d < 800.0 and alive.any():
            hv = sample_bilinear_vec(h, xs, zs, cx + nx_ * d, cz + nz_ * d)
            gone = alive & np.isnan(hv)
            alive &= ~gone
            land = alive & (hv >= 0.0)
            frac = prev / np.where(land, prev - hv, 1.0)
            found = np.where(land, d - dstep + frac * dstep, found)
            alive &= ~land
            prev = np.where(alive, hv, prev)
            d += dstep
        pts = []
        for i in np.nonzero(~np.isnan(found))[0]:
            pts.append([round(float(cx[i] + nx_[i] * found[i]), 2), round(float(cz[i] + nz_[i] * found[i]), 2)])
        return pts
    # 下流を向いて左 = (-tz, tx)、右 = (tz, -tx)
    left = walk(-tdir[:, 1], tdir[:, 0])
    right = walk(tdir[:, 1], -tdir[:, 0])
    return left, right


def export_shores(res: dict, recipe: dict, project_root: Path, name: str) -> None:
    left, right = extract_shores(res, float(recipe["cell_m"]), 4.0)
    info = {
        "name": name,
        "note": "左右の岸（水際 = 高さ 0 の線）。[x, z] を下流へ向かう順（中心線の弧長 4m おき）。左右は下流を向いて。崖の先（谷）は含まない",
        "waterHeight": 0.0,
        "left": left,
        "right": right,
    }
    path = project_root / TERRAIN_JSON_DIR / f"{name}_Shore.json"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(info, ensure_ascii=False, separators=(",", ":")), encoding="utf-8")
    print(f"shore    : {TERRAIN_JSON_DIR}/{name}_Shore.json  左 {len(left)} 点 / 右 {len(right)} 点")


# ---------------------------------------------------------------- 塊（メッシュ）への切り分け

def chunk_list(recipe: dict) -> list[dict]:
    if recipe["chunks"]:
        return recipe["chunks"]
    ext = recipe["extent"]
    return [{"name": recipe["name"], **ext, "streamGroup": STREAM_GROUP, "river_map": True, "shore": True}]


def chunk_slices(res: dict, ch: dict) -> tuple[slice, slice]:
    xs, zs = res["xs"], res["zs"]
    eps = 1e-6
    cols = np.nonzero((xs >= ch["x_min"] - eps) & (xs <= ch["x_max"] + eps))[0]
    rows = np.nonzero((zs >= ch["z_min"] - eps) & (zs <= ch["z_max"] + eps))[0]
    return slice(rows[0], rows[-1] + 1), slice(cols[0], cols[-1] + 1)


def crop_res(res: dict, rs: slice, cs: slice) -> dict:
    out = dict(res)
    for k, v in res.items():
        if isinstance(v, np.ndarray) and v.ndim >= 2 and v.shape[:2] == res["height"].shape:
            out[k] = v[rs, cs]
    out["xs"] = res["xs"][cs]
    out["zs"] = res["zs"][rs]
    return out


def main() -> None:
    ap = argparse.ArgumentParser(description="地形のハイトマップを手続き生成する（12_TerrainRendering.md）")
    ap.add_argument("--recipe", type=Path, help="レシピ JSON（既定値へ上書きマージ）")
    ap.add_argument("--seed", type=int, help="レシピの seed を上書き")
    ap.add_argument("--gltf", action="store_true", help="glTF とスプラットマップを Assets/ に書いて cook する")
    ap.add_argument("--scene", action="store_true", help="StagePlay.json に <name> のエントリを書く")
    ap.add_argument("--river-map", action="store_true", help="川のマップと共通の .json を書いて cook する")
    ap.add_argument("--shore", action="store_true", help="左右の岸の線を JSON に書く（L6 / L10 用）")
    ap.add_argument("--visibility", action="store_true", help="見通しチェック（後から読む塊が画面に映る割合）")
    ap.add_argument("--no-cook", action="store_true")
    args = ap.parse_args()

    recipe = DEFAULT_RECIPE
    if args.recipe:
        recipe = merge(recipe, json.loads(args.recipe.read_text(encoding="utf-8")))
    if args.seed is not None:
        recipe["seed"] = args.seed

    project_root = Path(__file__).resolve().parents[2]
    name = recipe["name"]
    cell = float(recipe["cell_m"])
    out_dir = project_root / "Generated" / "Terrain" / name
    out_dir.mkdir(parents=True, exist_ok=True)

    res = build_height(recipe)
    h = res["height"]
    report(res, cell)

    np.save(out_dir / f"{name}_Height.npy", h)
    hmin, hmax = float(h.min()), float(h.max())
    h16 = np.round((h - hmin) / max(hmax - hmin, 1e-6) * 65535.0).astype(np.uint16)
    Image.fromarray(h16).save(out_dir / f"{name}_Height16.png")
    ext = recipe["extent"]
    info = {
        "name": name,
        "seed": recipe["seed"],
        "x_min": ext["x_min"], "x_max": ext["x_max"], "z_min": ext["z_min"], "z_max": ext["z_max"],
        "cell_m": cell,
        "width": int(h.shape[1]), "height": int(h.shape[0]),
        "height_min_m": hmin, "height_max_m": hmax,
        "note": "行 0 = z_max、列 0 = x_min（上 = +Z、右 = +X）。Height16.png の 0..65535 は height_min_m..height_max_m",
    }
    (out_dir / f"{name}_Height.json").write_text(json.dumps(info, indent=2, ensure_ascii=False), encoding="utf-8")
    make_preview(res, cell).save(out_dir / f"{name}_Preview.png")
    color = view_colors(res, cell)
    make_views(res, cell, out_dir, name, color)
    print(f"\n出力: {out_dir}")
    if args.visibility:
        visibility_check(res, recipe, out_dir, name, color)

    chunks = chunk_list(recipe)
    if args.gltf:
        # スプラット・色合い・法線は全体で求めてから切り出す（塊の境目で塗りと陰影を揃える）
        splat_full = build_splat(res, recipe)
        tint_full = build_tint(res, recipe, splat_full)
        n_full, dhdx_full = surface_normals(res["height"], cell)
    for ch in chunks:
        rs, cs = chunk_slices(res, ch)
        sub = crop_res(res, rs, cs)
        print(f"\n[{ch['name']}] X {sub['xs'][0]:.0f}..{sub['xs'][-1]:.0f} / Z {sub['zs'][-1]:.0f}..{sub['zs'][0]:.0f}"
              f"  {sub['height'].shape[1]} x {sub['height'].shape[0]}（{sub['height'].size:,} 頂点）  streamGroup = {ch['streamGroup']}")
        if args.gltf:
            export_gltf(sub, recipe, project_root, ch["name"], splat_full[rs, cs], tint_full[rs, cs],
                        (n_full[rs, cs], dhdx_full[rs, cs]))
        if args.river_map and ch.get("river_map"):
            ext = {k: ch[k] for k in ("x_min", "x_max", "z_min", "z_max")}
            export_river_map(sub, recipe, project_root, out_dir, ch["name"], ext)
        if args.shore and ch.get("shore"):
            export_shores(sub, recipe, project_root, ch["name"])
    if (args.gltf or args.river_map) and not args.no_cook:
        run_cook(project_root, [ch["name"] for ch in chunks])
    if args.scene:
        for ch in chunks:
            write_scene_entry(project_root, ch["name"], ch["streamGroup"])


if __name__ == "__main__":
    main()
