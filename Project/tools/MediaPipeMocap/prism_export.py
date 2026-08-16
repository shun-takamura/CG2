"""rig.solve() の結果を PRISMEngine ネイティブの .skel + .anim (v1) で直接書き出す。

バイナリ仕様は PRISMEngine の cook_assets.py (_write_skel_v1 / _write_anim_v1) と同一。
座標系は cook_assets.py の glTF 経路と同じく RH → LH の X 軸ミラーを適用する:
  - translation: x 反転
  - quaternion (x,y,z,w): y/z 反転
  - 4x4 行列（列優先）: (行0 ⊕ 列0) の片方だけ真の成分の符号反転

エンジン側の ApplyAnimation が空チャンネルを想定していないため、
全ジョイントに T/R/S キーを必ず出力する（静的なものは 1 キー）。
"""

from __future__ import annotations

import struct
from pathlib import Path

SKEL_MAGIC = b"SKEL"
SKEL_VERSION = 1
ANIM_MAGIC = b"ANIM"
ANIM_VERSION = 1
ANIM_HEADER_SIZE = 24
ANIM_CHANNEL_SIZE = 64 + 4 * 6


def _pad_string(s: str, length: int) -> bytes:
    raw = s.encode("utf-8")[: length - 1]
    return raw + b"\x00" * (length - len(raw))


def _mirror_t(t):
    return (-t[0], t[1], t[2])


def _mirror_r(r):
    return (r[0], -r[1], -r[2], r[3])


def _mirror_m4(m):
    out = list(m)
    for i in range(4):
        for j in range(4):
            if (i == 0) != (j == 0):
                out[i + 4 * j] = -out[i + 4 * j]
    return tuple(out)


def export_prism(result, out_base_path):
    """<out_base_path>.skel と <out_base_path>.anim を書き出す。"""
    base = Path(out_base_path)
    base.parent.mkdir(parents=True, exist_ok=True)
    skel_path = base.with_suffix(".skel")
    anim_path = base.with_suffix(".anim")

    joints = result["joints"]

    # ---- .skel ----
    with skel_path.open("wb") as f:
        f.write(SKEL_MAGIC)
        f.write(struct.pack("<III", SKEL_VERSION, len(joints), 0))
        for jd in joints:
            px, py, pz = jd["bind_p"]
            # バインド回転=単位なので IBM は translate(-p)（列優先）→ X ミラー
            ibm = _mirror_m4((1, 0, 0, 0,
                              0, 1, 0, 0,
                              0, 0, 1, 0,
                              -px, -py, -pz, 1))
            f.write(_pad_string(jd["name"], 64))
            f.write(struct.pack("<i", jd["parent"]))
            f.write(struct.pack("<16f", *ibm))
            f.write(struct.pack("<3f", *_mirror_t(jd["bind_t"])))
            f.write(struct.pack("<4f", 0.0, 0.0, 0.0, 1.0))
            f.write(struct.pack("<3f", 1.0, 1.0, 1.0))

    # ---- .anim ----
    # チャンネル構築: 全ジョイント T/R/S 必須
    channels = []  # (name, t_keys, r_keys, s_keys)
    for jd in joints:
        name = jd["name"]
        r_keys = [(t, _mirror_r(q)) for t, q in result["rot_keys"][name]]
        if jd["parent"] < 0:
            t_keys = [(t, _mirror_t(p)) for t, p in result["root_trans_keys"]]
        else:
            t_keys = [(0.0, _mirror_t(jd["bind_t"]))]
        s_keys = [(0.0, (1.0, 1.0, 1.0))]
        channels.append((name, t_keys, r_keys, s_keys))

    _write_anim_v1(anim_path, result["duration"], channels)
    return [skel_path, anim_path]


def _write_anim_v1(anim_path: Path, duration: float, channels: list) -> None:
    """.anim v1 を書き出す。channels = [(name, t_keys, r_keys, s_keys)]（ミラー適用済み）"""
    keyframes_start = ANIM_HEADER_SIZE + len(channels) * ANIM_CHANNEL_SIZE
    cursor = keyframes_start
    layouts = []
    for _, tk, rk, sk in channels:
        t_ofs = cursor; cursor += len(tk) * 16   # time(4) + vec3(12)
        r_ofs = cursor; cursor += len(rk) * 20   # time(4) + vec4(16)
        s_ofs = cursor; cursor += len(sk) * 16
        layouts.append((t_ofs, r_ofs, s_ofs))

    with anim_path.open("wb") as f:
        f.write(ANIM_MAGIC)
        f.write(struct.pack("<I", ANIM_VERSION))
        f.write(struct.pack("<f", duration))
        f.write(struct.pack("<I", len(channels)))
        f.write(struct.pack("<I", ANIM_HEADER_SIZE))
        f.write(struct.pack("<I", 0))
        for (name, tk, rk, sk), (t_ofs, r_ofs, s_ofs) in zip(channels, layouts):
            f.write(_pad_string(name, 64))
            f.write(struct.pack("<IIIIII", len(tk), len(rk), len(sk),
                                t_ofs, r_ofs, s_ofs))
        for name, tk, rk, sk in channels:
            for t, v in tk:
                f.write(struct.pack("<f3f", t, *v))
            for t, q in rk:
                f.write(struct.pack("<f4f", t, *q))
            for t, v in sk:
                f.write(struct.pack("<f3f", t, *v))


def export_anim_only(retargeted: dict, out_anim_path) -> list:
    """リターゲット結果を .anim 単体で書き出す（ターゲットの .skel は生成しない）。

    アニメーションするのはマッピング済みジョイントのみ。未マッピングのジョイントは
    チャンネル自体を書かない（エンジンは .skel のバインド値のまま表示するので、
    静止したまま = 意図した挙動になる）。
    """
    anim_path = Path(out_anim_path).with_suffix(".anim")
    anim_path.parent.mkdir(parents=True, exist_ok=True)

    name_to_idx = {n: i for i, n in enumerate(retargeted["joint_names"])}
    channels = []
    for name, r_keys in retargeted["rot_local_keys"].items():
        k = name_to_idx[name]
        r = [(t, _mirror_r(q)) for t, q in r_keys]
        if name == retargeted["root_joint"] and retargeted["root_trans_keys"]:
            t_keys = [(t, _mirror_t(p))
                      for t, p in retargeted["root_trans_keys"]]
        else:
            t_keys = [(0.0, _mirror_t(retargeted["bind_local_t"][k]))]
        s_keys = [(0.0, tuple(retargeted["bind_local_s"][k]))]
        channels.append((name, t_keys, r, s_keys))

    _write_anim_v1(anim_path, retargeted["duration"], channels)
    return [anim_path]
