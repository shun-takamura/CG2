"""Blender ライブ連携のクライアント側。

send_or_launch() がメインの入口:
  1. 起動中の Blender（アドオン/ブートストラップがリッスン中）へ送信を試す
  2. つながらなければ Blender を自動起動して初回インポートさせる

ポート/ホストは blender_addon/mediapipemocap_live.py と一致させること。
"""

from __future__ import annotations

import json
import socket
import subprocess
import sys
from pathlib import Path

HOST = "127.0.0.1"
PORT = 47829

# frozen(exe) 実行時は実行ファイルの隣、ソース実行時はこのファイルの隣を基準にする
if getattr(sys, "frozen", False):
    _APP_DIR = Path(sys.executable).parent
else:
    _APP_DIR = Path(__file__).parent

_BOOTSTRAP = _APP_DIR / "blender_addon" / "launch_bootstrap.py"

# Blender 実行ファイルの標準的な探索場所（新しい版を優先）
_BLENDER_GLOB_ROOTS = [
    Path(r"C:\Program Files\Blender Foundation"),
    Path(r"C:\Program Files (x86)\Blender Foundation"),
]


def send_gltf(gltf_path, clear=True, host=HOST, port=PORT, timeout=2.0):
    """起動中の Blender へ glTF パスを送る。(ok, message) を返す。

    ConnectionRefusedError（リッスンしていない）は呼び出し側で捕捉できるよう
    そのまま送出する。
    """
    msg = json.dumps({"gltf": str(gltf_path), "clear": clear}) + "\n"
    with socket.create_connection((host, port), timeout=timeout) as s:
        s.sendall(msg.encode("utf-8"))
        s.settimeout(timeout)
        try:
            reply = s.recv(64)
        except socket.timeout:
            reply = b""
    if reply.startswith(b"OK"):
        return True, "Blender に送信しました"
    return False, f"Blender の応答が不正です: {reply!r}"


def find_blender(explicit_path=None):
    """blender.exe のパスを返す（見つからなければ None）。"""
    if explicit_path:
        p = Path(explicit_path)
        if p.exists():
            return p
    candidates = []
    for root in _BLENDER_GLOB_ROOTS:
        if root.exists():
            for exe in root.glob("Blender */blender.exe"):
                candidates.append(exe)
    if not candidates:
        return None
    # "Blender 4.4" のような版名で新しい方を優先（文字列ソートで概ね妥当）
    candidates.sort(key=lambda p: p.parent.name, reverse=True)
    return candidates[0]


def launch_blender(gltf_path, blender_path=None):
    """Blender を自動起動して glTF を初回インポートさせる。(ok, message)。"""
    exe = find_blender(blender_path)
    if exe is None:
        return False, ("Blender が見つかりませんでした。\n"
                       "settings.json の blender_path でパスを指定してください。")
    if not _BOOTSTRAP.exists():
        return False, f"起動スクリプトが見つかりません: {_BOOTSTRAP}"
    try:
        subprocess.Popen([str(exe), "--python", str(_BOOTSTRAP),
                          "--", str(gltf_path)])
    except OSError as e:
        return False, f"Blender の起動に失敗しました: {e}"
    return True, f"Blender を起動しています…（{exe.parent.name}）"


def send_or_launch(gltf_path, clear=True, blender_path=None):
    """まず起動中の Blender へ送信、ダメなら自動起動する。(ok, message)。"""
    try:
        return send_gltf(gltf_path, clear=clear)
    except (ConnectionRefusedError, socket.timeout, OSError):
        # リッスンしていない → 自動起動
        return launch_blender(gltf_path, blender_path=blender_path)
