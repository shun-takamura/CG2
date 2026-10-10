"""板の雲（smoke_00 を貼った Plane）を N 枚に増やした計測用シーンを作る。

高高度フェーズ（0〜51秒、Y=900）の足元の雲海を板で埋めた場合のコストを測るため、
StagePlay.json から今の板の雲（smoke_00 の Plane）を抜き、雲海の層（既定 Y 750〜850）へ
N 枚の板を撒いたシーンを Generated/Bench/ に書き出す。板はプレハブ CloudPlaneBench
（Full ビルボード・通常ブレンド・深度書き込み OFF＝半透明の板として正しい設定）で出す。

撒き方: --hold-sec の時点のカメラ位置（CameraPath の制御点を 3 秒/点で線形補間）から見て、
前方 ±--half-angle 度・距離 --near〜--far を「距離の対数で一様」に撒き、板の大きさを距離に比例させる
（=どの距離の板も画面上でほぼ同じ大きさ。遠くまで画面を埋めるのに枚数が爆発しない）。

    py tools\\Python\\gen_cloud_plane_bench.py -n 200
    py tools\\Python\\gen_cloud_plane_bench.py -n 50 100 200 400 800 --hold-sec 0

出力（Project/ 基準）: ../Generated/Bench/CloudPlanes_<N>.json と Resources/Json/Prefabs/CloudPlaneBench.json。
起動は run_cloud_bench.py --preset stage-planes が --scene-json で渡す。
"""
import argparse
import json
import math
import random
import sys
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parents[2]
SCENE_PATH = PROJECT_DIR / "Resources" / "Json" / "Scenes" / "StagePlay.json"
PREFAB_PATH = PROJECT_DIR / "Resources" / "Json" / "Prefabs" / "CloudPlaneBench.json"
OUT_DIR = PROJECT_DIR.parent / "Generated" / "Bench"
PREFAB_NAME = "CloudPlaneBench"
SMOKE_TEXTURE = "Resources/Textures/Effect/smoke_00.dds"
SEC_PER_POINT = 3.0  # 高高度区間は CameraPath の制御点 1 つ = 3 秒（idx17 = 51 秒）

PREFAB = {
    "kind": "Primitive",
    "model": {"dir": "", "file": "", "animated": False},
    "tag": "None",
    "defaults": {"scale": [1.0, 1.0, 1.0], "rotate": [0, 0, 0]},
    "primitive": {
        "primitiveType": 0,
        "texturePath": SMOKE_TEXTURE,
        "color": [1.0, 1.0, 1.0, 1.0],
        "blendMode": 1,
        "depthWrite": False,
        "alphaReference": 0,
        "cullBackface": False,
        "samplerMode": 0,
        "uvAutoScroll": False,
        "uvScrollSpeed": [0, 0],
        "uvOffset": [0, 0],
        "uvScale": [1, 1],
        "uvFlipU": False,
        "uvFlipV": False,
        "billboardMode": "Full",
        "timeGroup": "World",
    },
}


def read_json(path):
    raw = path.read_bytes()
    return json.loads(raw.decode("utf-8-sig"))


def camera_at(points, sec):
    """CameraPath の制御点を 3 秒/点で線形補間した位置と進行方向（XZ）"""
    f = max(0.0, sec / SEC_PER_POINT)
    i = min(int(f), len(points) - 2)
    u = f - i
    a, b = points[i], points[i + 1]
    eye = [a[k] + (b[k] - a[k]) * u for k in range(3)]
    fx, fz = b[0] - a[0], b[2] - a[2]
    n = math.hypot(fx, fz) or 1.0
    return eye, (fx / n, fz / n)


def is_cloud_plane(o):
    return o.get("type") == "Primitive" and o.get("texture") == SMOKE_TEXTURE


def make_planes(n, eye, fwd, args, rng):
    planes = []
    half = math.radians(args.half_angle)
    yaw0 = math.atan2(fwd[0], fwd[1])
    ln_near, ln_far = math.log(args.near), math.log(args.far)
    for i in range(n):
        d = math.exp(rng.uniform(ln_near, ln_far))
        yaw = yaw0 + rng.uniform(-half, half)
        x = eye[0] + math.sin(yaw) * d
        z = eye[2] + math.cos(yaw) * d
        y = rng.uniform(args.bottom, args.top)
        s = max(args.min_size, d * args.size_ratio)
        planes.append({
            "type": "Prefab",
            "name": f"CloudPlaneBench_{i:04d}",
            "prefab": PREFAB_NAME,
            "transform": {"scale": [s, s, 1.0], "rotate": [0, 0, 0], "translate": [x, y, z]},
        })
    return planes


def main(argv):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", type=int, nargs="+", default=[200], help="板の枚数（複数可）")
    ap.add_argument("--hold-sec", type=float, default=0.0, help="カメラ位置を取るステージ秒（run_cloud_bench の --bench-hold と揃える。0 = 今の板の雲が見える開始位置）")
    ap.add_argument("--top", type=float, default=850.0, help="雲海の最高点 Y（11 §3.0）")
    ap.add_argument("--bottom", type=float, default=750.0, help="雲海の最低点 Y（深さは未定。仮に 100m）")
    ap.add_argument("--near", type=float, default=40.0, help="撒く距離の最小 [m]")
    ap.add_argument("--far", type=float, default=4500.0, help="撒く距離の最大 [m]（farClip 5000 の手前）")
    ap.add_argument("--half-angle", type=float, default=70.0, help="前方の撒く角度の半分 [度]")
    ap.add_argument("--size-ratio", type=float, default=0.25, help="板の一辺 = 距離 × この値")
    ap.add_argument("--min-size", type=float, default=10.0, help="板の一辺の最小 [m]（今の板と同じ 10m）")
    ap.add_argument("--seed", type=int, default=1, help="乱数の種（同じ種なら同じ配置）")
    args = ap.parse_args(argv)

    scene = read_json(SCENE_PATH)
    cam = next((o for o in scene["objects"] if o.get("type") == "Spline" and o.get("name") == "CameraPath"), None)
    if cam is None:
        sys.exit("CameraPath が StagePlay.json に無い")
    eye, fwd = camera_at(cam["points"], args.hold_sec)
    base = [o for o in scene["objects"] if not is_cloud_plane(o)]
    removed = len(scene["objects"]) - len(base)

    PREFAB_PATH.write_text(json.dumps(PREFAB, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    print(f"カメラ（{args.hold_sec:.1f}s）: ({eye[0]:.0f}, {eye[1]:.0f}, {eye[2]:.0f}) 前方 ({fwd[0]:.2f}, {fwd[1]:.2f})"
          f" / 元の板の雲 {removed} 枚を除外 / 層 Y {args.bottom:.0f}〜{args.top:.0f}")
    for n in args.n:
        rng = random.Random(args.seed)
        out = dict(scene)
        out["objects"] = base + make_planes(n, eye, fwd, args, rng)
        path = OUT_DIR / f"CloudPlanes_{n}.json"
        path.write_text(json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"wrote {path}")
    print(f"wrote {PREFAB_PATH}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
