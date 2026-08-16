"""MediaPipeMocap — カメラ/動画から姿勢推定してボーンアニメーションを書き出すツール。

使い方: python main.py
"""

from __future__ import annotations

import sys
import tempfile
import threading
import traceback
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import rig
import gltf_export
import prism_export
import settings
import capture_project
import blender_link
import keyreduce

# Blender ライブ確認用の一時 glTF 出力先（毎回上書き）
_PREVIEW_DIR = Path(tempfile.gettempdir()) / "MediaPipeMocap_preview"

try:
    import capture
except ImportError as e:
    _root = tk.Tk()
    _root.withdraw()
    messagebox.showerror(
        "MediaPipeMocap — 起動できません",
        f"必要なライブラリが見つかりません: {e.name}\n\n"
        "このフォルダで次を実行してください:\n"
        "    pip install -r requirements.txt\n\n"
        f"現在のPython: {sys.executable}\n"
        f"バージョン: {sys.version.split()[0]}\n\n"
        "複数のPythonがインストールされている場合、requirements.txt を\n"
        "インストールしたのと同じPythonで起動しているか確認してください。")
    sys.exit(1)


class App:
    def __init__(self, root: tk.Tk):
        self.root = root
        root.title("MediaPipeMocap")
        root.geometry("480x400")
        root.resizable(False, False)
        root.protocol("WM_DELETE_WINDOW", self._on_close)

        self.frames = None          # キャプチャ結果
        self.stop_event = threading.Event()
        self.worker = None
        self._settings = settings.load()
        self._pending_preview_rect = self._settings.get("preview_rect")

        pad = {"padx": 8, "pady": 4}

        # ---- 入力ソース ----
        src_frame = ttk.LabelFrame(root, text="入力ソース")
        src_frame.pack(fill="x", **pad)

        self.source_var = tk.StringVar(value="webcam")
        ttk.Radiobutton(src_frame, text="Web カメラ（ライブキャプチャ）",
                        variable=self.source_var, value="webcam",
                        command=self._update_widgets).pack(anchor="w", **pad)

        cam_row = ttk.Frame(src_frame)
        cam_row.pack(fill="x", padx=(24, 8))
        ttk.Label(cam_row, text="カメラ:").pack(side="left")
        self.camera_var = tk.StringVar()
        self.camera_combo = ttk.Combobox(cam_row, textvariable=self.camera_var,
                                         state="readonly", width=6)
        self.camera_combo.pack(side="left", padx=(4, 4))
        self.camera_refresh_btn = ttk.Button(cam_row, text="検出",
                                             command=self._refresh_cameras)
        self.camera_refresh_btn.pack(side="left")
        self._cameras_probed = False
        saved_cam = str(self._settings["camera_index"])
        self.camera_combo.configure(values=[saved_cam])
        self.camera_var.set(saved_cam)

        ttk.Radiobutton(src_frame, text="動画ファイル",
                        variable=self.source_var, value="video",
                        command=self._update_widgets).pack(anchor="w", **pad)

        file_row = ttk.Frame(src_frame)
        file_row.pack(fill="x", **pad)
        self.video_path_var = tk.StringVar()
        self.video_entry = ttk.Entry(file_row, textvariable=self.video_path_var)
        self.video_entry.pack(side="left", fill="x", expand=True)
        self.browse_btn = ttk.Button(file_row, text="参照…", command=self._browse)
        self.browse_btn.pack(side="left", padx=(4, 0))

        # ---- キャプチャ ----
        cap_frame = ttk.LabelFrame(root, text="キャプチャ")
        cap_frame.pack(fill="x", **pad)

        btn_row = ttk.Frame(cap_frame)
        btn_row.pack(fill="x", **pad)
        self.start_btn = ttk.Button(btn_row, text="開始", command=self._start)
        self.start_btn.pack(side="left")
        self.stop_btn = ttk.Button(btn_row, text="停止", command=self._stop,
                                   state="disabled")
        self.stop_btn.pack(side="left", padx=(4, 0))

        self.smoothing_var = tk.BooleanVar(value=self._settings["smoothing"])
        ttk.Checkbutton(cap_frame, text="スムージング（ジッタ軽減）",
                        variable=self.smoothing_var).pack(anchor="w", **pad)

        reduce_row = ttk.Frame(cap_frame)
        reduce_row.pack(fill="x", **pad)
        self.reduce_var = tk.BooleanVar(value=self._settings["reduce_keys"])
        ttk.Checkbutton(reduce_row, text="キー削減（許容誤差°）",
                        variable=self.reduce_var).pack(side="left")
        self.tolerance_var = tk.StringVar(value=str(self._settings["tolerance_deg"]))
        ttk.Entry(reduce_row, textvariable=self.tolerance_var, width=5).pack(
            side="left", padx=(4, 0))

        # ---- 出力 ----
        out_frame = ttk.LabelFrame(root, text="出力形式")
        out_frame.pack(fill="x", **pad)

        self.format_var = tk.StringVar(value="gltf")
        ttk.Radiobutton(out_frame, text="glTF (.gltf + .bin + .png) — 汎用",
                        variable=self.format_var, value="gltf").pack(anchor="w", **pad)
        ttk.Radiobutton(out_frame, text="PRISMEngine (.skel + .anim) — エンジン直接",
                        variable=self.format_var, value="prism").pack(anchor="w", **pad)

        save_row = ttk.Frame(out_frame)
        save_row.pack(fill="x", **pad)
        self.retarget_btn = ttk.Button(save_row, text="リターゲット…",
                                       command=self._open_retarget)
        self.retarget_btn.pack(side="left")
        self.save_btn = ttk.Button(save_row, text="保存…", command=self._save,
                                   state="disabled")
        self.save_btn.pack(side="right")
        self.blender_btn = ttk.Button(save_row, text="Blenderで確認",
                                      command=self._send_to_blender,
                                      state="disabled")
        self.blender_btn.pack(side="right", padx=(0, 4))

        # ---- ステータス ----
        self.status_var = tk.StringVar(value="待機中")
        ttk.Label(root, textvariable=self.status_var, anchor="w").pack(
            fill="x", padx=8, pady=(0, 8))

        self._update_widgets()

    # ------------------------------------------------------------
    def _update_widgets(self):
        is_video = self.source_var.get() == "video"
        state = "normal" if is_video else "disabled"
        self.video_entry.configure(state=state)
        self.browse_btn.configure(state=state)
        self.camera_combo.configure(state="disabled" if is_video else "readonly")
        self.camera_refresh_btn.configure(state="disabled" if is_video else "normal")
        self.start_btn.configure(text="処理開始" if is_video else "録画開始")
        if not is_video and not self._cameras_probed:
            self._refresh_cameras()

    def _refresh_cameras(self):
        self._cameras_probed = True
        self._set_status("カメラを検出中…")
        self.camera_refresh_btn.configure(state="disabled")

        def work():
            found = capture.list_cameras()
            self.root.after(0, lambda: self._on_cameras_found(found))

        threading.Thread(target=work, daemon=True).start()

    def _on_cameras_found(self, found):
        self.camera_refresh_btn.configure(state="normal")
        if not found:
            self._set_status("カメラが見つかりませんでした")
            return
        values = [str(i) for i in found]
        self.camera_combo.configure(values=values)
        if self.camera_var.get() not in values:
            self.camera_var.set(values[0])
        self._set_status(f"カメラ {len(found)} 台検出: {', '.join(values)}")

    def _browse(self):
        path = filedialog.askopenfilename(
            title="動画ファイルを選択",
            filetypes=[("動画", "*.mp4 *.avi *.mov *.mkv *.webm"), ("すべて", "*.*")])
        if path:
            self.video_path_var.set(path)

    def _set_status(self, text):
        self.root.after(0, lambda: self.status_var.set(text))

    def _save_settings(self):
        try:
            camera_index = int(self.camera_var.get())
        except (ValueError, TypeError):
            camera_index = self._settings["camera_index"]
        # プレビューウィンドウの位置・サイズは最後にキャプチャした値を優先
        rect = self._pending_preview_rect or self._settings.get("preview_rect")
        self._settings["preview_rect"] = rect
        settings.save({"camera_index": camera_index,
                       "smoothing": self.smoothing_var.get(),
                       "preview_rect": rect,
                       "blender_path": self._settings.get("blender_path"),
                       "reduce_keys": self.reduce_var.get(),
                       "tolerance_deg": self._tolerance_deg()})

    def _tolerance_deg(self):
        try:
            return float(self.tolerance_var.get())
        except (ValueError, TypeError):
            return self._settings["tolerance_deg"]

    def _maybe_reduce(self, result):
        """設定に応じて出力用にキー削減した結果を返す（元の result は変更しない）"""
        if not self.reduce_var.get():
            return result
        reduced = keyreduce.reduce_solved(result, self._tolerance_deg())
        st = reduced["reduction_stats"]
        self._set_status(f"キー削減: {st['before']} → {st['after']} "
                         f"({st['ratio'] * 100:.0f}%)")
        return reduced

    def _on_preview_rect(self, rect):
        # ワーカースレッドから呼ばれる。単純代入なのでスレッドセーフ。
        self._pending_preview_rect = rect

    def _on_close(self):
        self._save_settings()
        self.root.destroy()

    # ------------------------------------------------------------
    def _start(self):
        if self.worker and self.worker.is_alive():
            return
        source = self.source_var.get()
        if source == "video" and not self.video_path_var.get():
            messagebox.showwarning("MediaPipeMocap", "動画ファイルを選択してください")
            return

        self.frames = None
        self.stop_event.clear()
        self.start_btn.configure(state="disabled")
        self.stop_btn.configure(state="normal")
        self.save_btn.configure(state="disabled")
        self._save_settings()

        def work():
            try:
                if source == "webcam":
                    try:
                        camera_index = int(self.camera_var.get())
                    except (ValueError, TypeError):
                        camera_index = 0
                    frames = capture.run_webcam(
                        self.stop_event, camera_index=camera_index,
                        status_cb=self._set_status,
                        preview_rect=self._pending_preview_rect,
                        preview_rect_cb=self._on_preview_rect)
                else:
                    frames = capture.run_video(
                        self.video_path_var.get(),
                        stop_event=self.stop_event,
                        status_cb=self._set_status,
                        preview_rect=self._pending_preview_rect,
                        preview_rect_cb=self._on_preview_rect)
                self.frames = frames
                self._set_status(
                    f"キャプチャ完了: {len(frames)} フレーム"
                    + (f" / {frames[-1][0] - frames[0][0]:.1f}s" if frames else ""))
            except Exception as e:
                traceback.print_exc()
                self._set_status(f"エラー: {e}")
            finally:
                self.root.after(0, self._on_capture_done)

        self.worker = threading.Thread(target=work, daemon=True)
        self.worker.start()

    def _stop(self):
        self.stop_event.set()

    def _on_capture_done(self):
        self.start_btn.configure(state="normal")
        self.stop_btn.configure(state="disabled")
        if self.frames:
            self.save_btn.configure(state="normal")
            self.blender_btn.configure(state="normal")
        # プレビューで調整した位置・サイズを即座に永続化
        self._save_settings()

    def _send_to_blender(self):
        if not self.frames:
            return
        self.blender_btn.configure(state="disabled")
        self._set_status("Blender へ送信中…")

        def work():
            try:
                result = getattr(self, "last_result", None)
                if result is None:
                    result = rig.solve(self.frames,
                                       smoothing=self.smoothing_var.get())
                    self.last_result = result
                _PREVIEW_DIR.mkdir(parents=True, exist_ok=True)
                out = _PREVIEW_DIR / "capture_preview.gltf"
                gltf_export.export_gltf(self._maybe_reduce(result), out)
                ok, msg = blender_link.send_or_launch(
                    out, blender_path=self._settings.get("blender_path"))
                self._set_status(msg)
                if not ok:
                    self.root.after(
                        0, lambda: messagebox.showwarning("MediaPipeMocap", msg))
            except Exception as e:
                traceback.print_exc()
                self._set_status(f"エラー: {e}")
            finally:
                self.root.after(
                    0, lambda: self.blender_btn.configure(state="normal"))

        threading.Thread(target=work, daemon=True).start()

    # ------------------------------------------------------------
    def _save(self):
        if not self.frames:
            return
        fmt = self.format_var.get()
        if fmt == "gltf":
            path = filedialog.asksaveasfilename(
                title="glTF として保存", defaultextension=".gltf",
                filetypes=[("glTF", "*.gltf")])
        else:
            path = filedialog.asksaveasfilename(
                title=".skel / .anim として保存（拡張子は自動付与）",
                filetypes=[("PRISM anim", "*.anim")])
        if not path:
            return

        try:
            self._set_status("変換中…")
            result = rig.solve(self.frames, smoothing=self.smoothing_var.get())
            self.last_result = result
            # 中間データ(.mocapdata.json)は削減前の元データを保存する
            # （後からリターゲットする際は元の精度で計算したいため）
            mocap_path = capture_project.save(result, path)
            export_result = self._maybe_reduce(result)
            if fmt == "gltf":
                written = gltf_export.export_gltf(export_result, path)
            else:
                written = prism_export.export_prism(export_result, path)
            written.append(mocap_path)
            names = "\n".join(str(p) for p in written)
            self._set_status(f"保存完了 ({result['duration']:.1f}s, "
                             f"{len(result['times'])} キー)")
            messagebox.showinfo("MediaPipeMocap", f"保存しました:\n{names}")
        except Exception as e:
            traceback.print_exc()
            self._set_status(f"エラー: {e}")
            messagebox.showerror("MediaPipeMocap", str(e))

    def _open_retarget(self):
        import retarget_ui
        session = getattr(self, "last_result", None)
        if session is None and self.frames:
            # 保存前でもキャプチャ済みならその場で solve して渡す
            try:
                session = rig.solve(self.frames,
                                    smoothing=self.smoothing_var.get())
                self.last_result = session
            except Exception:
                session = None
        retarget_ui.RetargetDialog(self.root, session_result=session)


def main():
    root = tk.Tk()
    App(root)
    root.mainloop()


if __name__ == "__main__":
    main()
