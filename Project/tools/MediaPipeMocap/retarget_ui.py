"""リターゲットのマッピングUI（Toplevelダイアログ）。

フロー:
  1. モーキャプデータ（現在のセッション結果 or .mocapdata.json）を用意
  2. ターゲットモデル (.gltf) を読み込み
  3. 21ソースジョイント → ターゲットジョイントのマッピングを作る
     （「名前から推測」ボタンで候補提案 → ユーザーが確認・修正）
  4. .anim 単体 / 統合 .gltf のどちらかでエクスポート

マッピングは <モデル名>.retarget.json としてモデルの隣に保存し、
同じモデルを再度読み込んだ時に自動で読み込みを提案する。
"""

from __future__ import annotations

import json
import tempfile
import traceback
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

import blender_link
import capture_project
import gltf_import
import gltf_export
import keyreduce
import prism_export
import retarget
from rig import JOINTS
from retarget import UNMAPPED, MAPPING_SUFFIX, guess_mapping  # noqa: F401 (再エクスポート)


class RetargetDialog(tk.Toplevel):
    def __init__(self, parent, session_result=None):
        super().__init__(parent)
        self.title("リターゲット")
        self.geometry("560x820")

        self.mocap = session_result   # rig.solve() 互換 dict
        self.target = None            # gltf_import.TargetModel
        pad = {"padx": 8, "pady": 3}

        # ---- ソース ----
        src_frame = ttk.LabelFrame(self, text="モーキャプデータ（ソース）")
        src_frame.pack(fill="x", **pad)
        row = ttk.Frame(src_frame)
        row.pack(fill="x", **pad)
        self.src_label_var = tk.StringVar(
            value="現在のセッションのキャプチャを使用" if session_result
            else "未読み込み")
        ttk.Label(row, textvariable=self.src_label_var).pack(side="left")
        ttk.Button(row, text=".mocapdata.json を読み込み…",
                   command=self._load_mocap).pack(side="right")

        # ---- ターゲット ----
        tgt_frame = ttk.LabelFrame(self, text="ターゲットモデル")
        tgt_frame.pack(fill="x", **pad)
        row = ttk.Frame(tgt_frame)
        row.pack(fill="x", **pad)
        self.tgt_label_var = tk.StringVar(value="未読み込み")
        ttk.Label(row, textvariable=self.tgt_label_var).pack(side="left")
        ttk.Button(row, text=".gltf を読み込み…",
                   command=self._load_target).pack(side="right")

        # ---- マッピング表（スクロール可能） ----
        map_frame = ttk.LabelFrame(self, text="ボーンマッピング（ソース → ターゲット / ひねり補正°）")
        map_frame.pack(fill="both", expand=True, **pad)

        btn_row = ttk.Frame(map_frame)
        btn_row.pack(fill="x", **pad)
        ttk.Button(btn_row, text="名前から推測",
                   command=self._guess).pack(side="left")
        ttk.Button(btn_row, text="マッピング保存…",
                   command=self._save_mapping).pack(side="left", padx=(4, 0))
        ttk.Button(btn_row, text="マッピング読込…",
                   command=self._load_mapping_dialog).pack(side="left", padx=(4, 0))

        canvas = tk.Canvas(map_frame, highlightthickness=0)
        scrollbar = ttk.Scrollbar(map_frame, orient="vertical",
                                  command=canvas.yview)
        self.table = ttk.Frame(canvas)
        self.table.bind("<Configure>",
                        lambda e: canvas.configure(scrollregion=canvas.bbox("all")))
        canvas.create_window((0, 0), window=self.table, anchor="nw")
        canvas.configure(yscrollcommand=scrollbar.set)
        canvas.pack(side="left", fill="both", expand=True, padx=(8, 0))
        scrollbar.pack(side="right", fill="y")

        self.combo_vars: dict[str, tk.StringVar] = {}
        self.twist_vars: dict[str, tk.StringVar] = {}
        self.combos: dict[str, ttk.Combobox] = {}
        for i, (src, _) in enumerate(JOINTS):
            ttk.Label(self.table, text=src, width=14).grid(
                row=i, column=0, sticky="w", pady=1)
            var = tk.StringVar(value=UNMAPPED)
            combo = ttk.Combobox(self.table, textvariable=var,
                                 state="disabled", width=28)
            combo.grid(row=i, column=1, pady=1)
            twist = tk.StringVar(value="0")
            ttk.Entry(self.table, textvariable=twist, width=6).grid(
                row=i, column=2, padx=(4, 0), pady=1)
            self.combo_vars[src] = var
            self.twist_vars[src] = twist
            self.combos[src] = combo

        # ---- ターゲットのバインドポーズ ----
        pose_frame = ttk.LabelFrame(self, text="ターゲットモデルのバインドポーズ")
        pose_frame.pack(fill="x", **pad)
        self.bind_pose_var = tk.StringVar(value="t")
        row1 = ttk.Frame(pose_frame)
        row1.pack(fill="x", **pad)
        ttk.Radiobutton(row1, text="Tポーズ（Mixamo等・推奨）",
                        variable=self.bind_pose_var, value="t").pack(side="left")
        row2 = ttk.Frame(pose_frame)
        row2.pack(fill="x", **pad)
        ttk.Radiobutton(row2, text="Aポーズ（VRoid等）腕下げ角度:",
                        variable=self.bind_pose_var, value="a").pack(side="left")
        self.apose_angle_var = tk.StringVar(value="45")
        ttk.Entry(row2, textvariable=self.apose_angle_var, width=5).pack(
            side="left", padx=(4, 0))
        ttk.Label(row2, text="°").pack(side="left")
        row3 = ttk.Frame(pose_frame)
        row3.pack(fill="x", **pad)
        ttk.Radiobutton(row3,
                        text="補正なし（動画の最初のフレーム = モデルのバインド）",
                        variable=self.bind_pose_var, value="none").pack(side="left")

        # ---- キー削減 ----
        reduce_row = ttk.Frame(self)
        reduce_row.pack(fill="x", **pad)
        self.reduce_var = tk.BooleanVar(value=False)
        ttk.Checkbutton(reduce_row, text="キー削減（許容誤差°）",
                        variable=self.reduce_var).pack(side="left")
        self.tolerance_var = tk.StringVar(value="0.5")
        ttk.Entry(reduce_row, textvariable=self.tolerance_var, width=5).pack(
            side="left", padx=(4, 0))
        ttk.Label(reduce_row,
                  text="（補間で再現できるキーを削除。編集しやすくなる）").pack(
            side="left", padx=(4, 0))

        # ---- スケール ----
        scale_row = ttk.Frame(self)
        scale_row.pack(fill="x", **pad)
        ttk.Label(scale_row, text="ルート移動スケール:").pack(side="left")
        self.scale_var = tk.StringVar(value="自動")
        ttk.Entry(scale_row, textvariable=self.scale_var, width=12).pack(
            side="left", padx=(4, 0))
        ttk.Label(scale_row, text="（「自動」= Hips↔Head 距離比から算出）").pack(
            side="left", padx=(4, 0))

        # ---- エクスポート ----
        exp_row = ttk.Frame(self)
        exp_row.pack(fill="x", **pad)
        ttk.Button(exp_row, text=".anim 単体で出力…",
                   command=lambda: self._export("anim")).pack(side="left")
        ttk.Button(exp_row, text="統合 .gltf で出力…",
                   command=lambda: self._export("gltf")).pack(
            side="left", padx=(4, 0))
        ttk.Button(exp_row, text="Blenderで確認",
                   command=self._send_to_blender).pack(side="right")

        self.status_var = tk.StringVar(value="")
        ttk.Label(self, textvariable=self.status_var, anchor="w").pack(
            fill="x", padx=8, pady=(0, 8))

    # ------------------------------------------------------------
    def _load_mocap(self):
        path = filedialog.askopenfilename(
            parent=self, title="キャプチャデータを選択",
            filetypes=[("mocapdata", "*.mocapdata.json"), ("すべて", "*.*")])
        if not path:
            return
        try:
            self.mocap = capture_project.load(path)
            self.src_label_var.set(Path(path).name)
        except Exception as e:
            messagebox.showerror("リターゲット", str(e), parent=self)

    def _load_target(self):
        path = filedialog.askopenfilename(
            parent=self, title="ターゲットモデルを選択",
            filetypes=[("glTF", "*.gltf"), ("すべて", "*.*")])
        if not path:
            return
        try:
            self.target = gltf_import.load_gltf(path)
        except Exception as e:
            messagebox.showerror("リターゲット", str(e), parent=self)
            return
        self.tgt_label_var.set(
            f"{Path(path).name}（{len(self.target.joint_names)} ジョイント）")
        values = [UNMAPPED] + list(self.target.joint_names)
        for src in self.combo_vars:
            self.combos[src].configure(values=values, state="readonly")
            self.combo_vars[src].set(UNMAPPED)

        # 同名の保存済みマッピングがあれば読み込みを提案
        saved = Path(path).with_name(Path(path).stem + MAPPING_SUFFIX)
        if saved.exists():
            if messagebox.askyesno(
                    "リターゲット",
                    f"保存済みマッピングが見つかりました:\n{saved.name}\n読み込みますか？",
                    parent=self):
                self._apply_mapping_file(saved)

    def _guess(self):
        if not self.target:
            messagebox.showwarning("リターゲット", "先にターゲットモデルを読み込んでください",
                                   parent=self)
            return
        guessed = guess_mapping(self.target.joint_names)
        for src, tgt in guessed.items():
            self.combo_vars[src].set(tgt)
        n = len(guessed)
        self.status_var.set(f"{n}/{len(JOINTS)} ジョイントを推測しました。"
                            "内容を確認して必要なら修正してください")

    # ------------------------------------------------------------
    def _current_mapping(self) -> tuple[dict, dict]:
        mapping, corrections = {}, {}
        for src, var in self.combo_vars.items():
            tgt = var.get()
            if tgt and tgt != UNMAPPED:
                mapping[src] = tgt
                try:
                    deg = float(self.twist_vars[src].get())
                except ValueError:
                    deg = 0.0
                if abs(deg) > 1e-9:
                    corrections[tgt] = deg
        return mapping, corrections

    def _save_mapping(self):
        if not self.target:
            return
        mapping, corrections = self._current_mapping()
        default = self.target.path.with_name(
            self.target.path.stem + MAPPING_SUFFIX)
        path = filedialog.asksaveasfilename(
            parent=self, title="マッピングを保存",
            initialdir=default.parent, initialfile=default.name,
            defaultextension=".json")
        if not path:
            return
        Path(path).write_text(
            json.dumps({"mapping": mapping, "corrections": corrections},
                      indent=2, ensure_ascii=False),
            encoding="utf-8")
        self.status_var.set(f"マッピングを保存しました: {Path(path).name}")

    def _load_mapping_dialog(self):
        path = filedialog.askopenfilename(
            parent=self, title="マッピングを読み込み",
            filetypes=[("retarget mapping", f"*{MAPPING_SUFFIX}"),
                       ("JSON", "*.json"), ("すべて", "*.*")])
        if path:
            self._apply_mapping_file(Path(path))

    def _apply_mapping_file(self, path: Path):
        try:
            data = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            messagebox.showerror("リターゲット", f"読み込み失敗: {e}", parent=self)
            return
        mapping = data.get("mapping", {})
        corrections = data.get("corrections", {})
        missing = []
        for src, var in self.combo_vars.items():
            tgt = mapping.get(src)
            if tgt and self.target and tgt in self.target.joint_names:
                var.set(tgt)
                self.twist_vars[src].set(str(corrections.get(tgt, 0)))
            else:
                var.set(UNMAPPED)
                self.twist_vars[src].set("0")
                if tgt:
                    missing.append(tgt)
        msg = f"マッピングを読み込みました: {path.name}"
        if missing:
            msg += f"（ターゲットに無いジョイント {len(missing)} 件は未割当に）"
        self.status_var.set(msg)

    # ------------------------------------------------------------
    def _build_retargeted(self):
        """入力を検証してリターゲットを実行。結果 dict を返す（失敗時 None）。"""
        if not self.mocap:
            messagebox.showwarning("リターゲット", "モーキャプデータを読み込んでください",
                                   parent=self)
            return None
        if not self.target:
            messagebox.showwarning("リターゲット", "ターゲットモデルを読み込んでください",
                                   parent=self)
            return None
        mapping, corrections = self._current_mapping()
        if not mapping:
            messagebox.showwarning("リターゲット", "マッピングが空です", parent=self)
            return None

        scale_text = self.scale_var.get().strip()
        root_scale = None
        if scale_text and scale_text != "自動":
            try:
                root_scale = float(scale_text)
            except ValueError:
                messagebox.showwarning("リターゲット",
                                       "スケールは数値か「自動」で指定してください",
                                       parent=self)
                return None

        bind_pose = self.bind_pose_var.get()
        arm_angle = 0.0
        if bind_pose == "a":
            try:
                arm_angle = float(self.apose_angle_var.get())
            except ValueError:
                messagebox.showwarning("リターゲット",
                                       "Aポーズの角度は数値で指定してください",
                                       parent=self)
                return None

        result = retarget.retarget(self.mocap, self.target, mapping,
                                   corrections, root_scale,
                                   tpose_reference=(bind_pose != "none"),
                                   arm_down_angle=arm_angle)
        self.scale_var.set(f"{result['root_scale']:.4f}")

        if self.reduce_var.get():
            try:
                tol = float(self.tolerance_var.get())
            except ValueError:
                messagebox.showwarning("リターゲット",
                                       "許容誤差は数値で指定してください",
                                       parent=self)
                return None
            result = keyreduce.reduce_retargeted(result, tol)
        return result

    def _export(self, fmt: str):
        if fmt == "anim":
            path = filedialog.asksaveasfilename(
                parent=self, title=".anim として出力",
                defaultextension=".anim", filetypes=[("PRISM anim", "*.anim")])
        else:
            path = filedialog.asksaveasfilename(
                parent=self, title="統合 glTF として出力",
                defaultextension=".gltf", filetypes=[("glTF", "*.gltf")])
        if not path:
            return

        try:
            result = self._build_retargeted()
            if result is None:
                return
            if fmt == "anim":
                written = prism_export.export_anim_only(result, path)
            else:
                written = gltf_export.export_retargeted_gltf(
                    result, self.target, path)
            names = "\n".join(str(p) for p in written)
            status = (f"出力完了（{len(result['rot_local_keys'])} ジョイント, "
                      f"スケール {result['root_scale']:.3f}")
            st = result.get("reduction_stats")
            if st:
                status += f", キー {st['before']}→{st['after']}"
            self.status_var.set(status + "）")
            messagebox.showinfo("リターゲット", f"出力しました:\n{names}",
                                parent=self)
        except Exception as e:
            traceback.print_exc()
            messagebox.showerror("リターゲット", str(e), parent=self)

    def _send_to_blender(self):
        try:
            result = self._build_retargeted()
            if result is None:
                return
            out_dir = Path(tempfile.gettempdir()) / "MediaPipeMocap_preview"
            out_dir.mkdir(parents=True, exist_ok=True)
            out = out_dir / "retarget_preview.gltf"
            gltf_export.export_retargeted_gltf(result, self.target, out)
            ok, msg = blender_link.send_or_launch(out)
            self.status_var.set(msg)
            if not ok:
                messagebox.showwarning("リターゲット", msg, parent=self)
        except Exception as e:
            traceback.print_exc()
            messagebox.showerror("リターゲット", str(e), parent=self)
