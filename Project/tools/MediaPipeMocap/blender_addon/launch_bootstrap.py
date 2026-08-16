"""Blender 自動起動用のブートストラップ。

MediaPipeMocap アプリが Blender を自動起動する際に使う:
    blender.exe --python launch_bootstrap.py -- <gltf_path>

隣の mediapipemocap パッケージをロードしてライブサーバを立て（以降のアプリからの
送信を受けられるようにし）、コマンドライン末尾の glTF を初回インポートする。
このため、アドオンを事前インストールしていなくても自動起動経路は動く。
"""

import sys
from pathlib import Path

import bpy

_HERE = Path(__file__).parent
if str(_HERE) not in sys.path:
    sys.path.insert(0, str(_HERE))

import mediapipemocap  # noqa: E402

# サーバ + タイマー起動（アドオン register 相当。UIパネルも登録する）
try:
    mediapipemocap.register()
except Exception as e:  # 二重登録などは無視
    print(f"[MediaPipeMocap] register warning: {e}")

# `--` 以降の引数を glTF パスとして初回インポート
argv = sys.argv
gltf_path = None
if "--" in argv:
    rest = argv[argv.index("--") + 1:]
    if rest:
        gltf_path = rest[0]

if gltf_path:
    mediapipemocap.live_server.queue_import(gltf_path, clear=True)
    print(f"[MediaPipeMocap] queued initial import: {gltf_path}")
