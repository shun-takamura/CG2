"""シーン内リターゲット（D4）。

キャプチャ結果（直前のセッション、または .mocapdata.json）を、シーン内の
既存アーマチュアへ直接リターゲットする。retarget.retarget() のコア数学は
無改造で再利用し、glTF由来かBlenderシーン由来かは BlenderTargetModel が
アダプタ層として吸収する。
"""

from __future__ import annotations

import json
from pathlib import Path

import bpy
from mathutils import Matrix, Quaternion, Vector

import capture_project
import keyreduce
import retarget
from rig import JOINTS, Q_IDENTITY, q_mul

from . import capture_operator


# ============================================================
# BlenderTargetModel: gltf_import.TargetModel と同じ属性形で
# 既存アーマチュアのボーンを読み出すアダプタ
# ============================================================
class BlenderTargetModel:
    def __init__(self, armature_obj):
        self.armature_obj = armature_obj
        arm = armature_obj.data

        self.joint_names: list[str] = []
        self.parent: list[int] = []
        self.bind_local_t: list[tuple] = []
        self.bind_local_r: list[tuple] = []
        self.bind_local_s: list[tuple] = []
        self.bind_global_r: list[tuple] = []

        # edit_bones ベースだと Edit Mode 遷移が要るため、bpy.types.Bone
        # (Object Data の rest pose) を使う。親→子の順に並べる必要があるので
        # トポロジカル順（親が必ず先）に並べ替える。
        bones = list(arm.bones)
        name_to_bone = {b.name: b for b in bones}
        ordered: list = []
        visited: set[str] = set()

        def visit(b):
            if b.name in visited:
                return
            if b.parent is not None and b.parent.name not in visited:
                visit(b.parent)
            visited.add(b.name)
            ordered.append(b)

        for b in bones:
            visit(b)

        name_to_index = {b.name: i for i, b in enumerate(ordered)}

        for b in ordered:
            self.joint_names.append(b.name)
            p_idx = name_to_index[b.parent.name] if b.parent else -1
            self.parent.append(p_idx)

            # Blenderのボーンは「レストポーズのローカル行列 = matrix_local」を
            # 親のmatrix_localの逆行列と掛けてローカルTRSを得る
            if b.parent:
                local_mat = b.parent.matrix_local.inverted() @ b.matrix_local
            else:
                local_mat = b.matrix_local
            t, r, s = local_mat.decompose()  # Vector, Quaternion, Vector
            self.bind_local_t.append((t.x, t.y, t.z))
            self.bind_local_r.append((r.x, r.y, r.z, r.w))
            self.bind_local_s.append((s.x, s.y, s.z))

            parent_g = self.bind_global_r[p_idx] if p_idx >= 0 else Q_IDENTITY
            self.bind_global_r.append(q_mul(parent_g, self.bind_local_r[-1]))

    def bind_global_position(self, joint_idx: int) -> tuple:
        """レストポーズのグローバル位置（matrix_local の平行移動成分をそのまま使う）"""
        bone = self.armature_obj.data.bones[self.joint_names[joint_idx]]
        t = bone.matrix_local.translation
        return (t.x, t.y, t.z)

    def joint_index(self, name: str) -> int:
        return self.joint_names.index(name)


# ============================================================
# 結果をアーマチュアへ適用（bpy直接操作）
# ============================================================
def apply_retargeted_to_armature(retargeted: dict, armature_obj,
                                 action_name="MediaPipeMocap_Retarget"):
    """retarget.retarget() の結果を pose_bone にキーフレームとして焼き込む。

    重要な変換:
      retarget.retarget() が返す rot_local_keys / root_trans_keys は
      glTF の node.rotation と同じ「親からのローカル変換そのもの（レスト姿勢込みの絶対値）」。
      一方 Blender の pose_bone.location/rotation_quaternion は
      「**レストポーズからの差分**（ボーンのレスト空間で表現）」= matrix_basis。

      両者の関係は  L_anim = L_rest @ basis  なので、
        basis = L_rest⁻¹ @ L_anim
      を求めてから decompose して代入する必要がある。
      絶対値をそのまま入れるとレスト回転が二重に掛かってポーズが壊れる。
    """
    if armature_obj.animation_data is None:
        armature_obj.animation_data_create()
    action = bpy.data.actions.new(action_name)
    armature_obj.animation_data.action = action

    pose = armature_obj.pose
    scenes = armature_obj.id_data.users_scene
    fps = scenes[0].render.fps if scenes else 30

    # 既存ポーズをレストへリセットする。
    # アクションを差し替えてもポーズ値自体は残るため、キーを打たない
    # 未マッピングのボーン（指・ひねり用ボーン等）に前のポーズが残留し、
    # 「未マッピング = バインドポーズのまま」という retarget の前提が崩れる。
    for pb in pose.bones:
        pb.location = (0.0, 0.0, 0.0)
        pb.rotation_quaternion = (1.0, 0.0, 0.0, 0.0)
        pb.rotation_euler = (0.0, 0.0, 0.0)
        pb.scale = (1.0, 1.0, 1.0)

    name_to_idx = {n: i for i, n in enumerate(retargeted["joint_names"])}
    root_joint = retargeted.get("root_joint")
    root_trans_keys = retargeted.get("root_trans_keys")

    for name, r_keys in retargeted["rot_local_keys"].items():
        pb = pose.bones.get(name)
        if pb is None:
            continue
        pb.rotation_mode = "QUATERNION"
        k = name_to_idx[name]

        t_rest = Vector(retargeted["bind_local_t"][k])
        bx, by, bz, bw = retargeted["bind_local_r"][k]
        q_rest = Quaternion((bw, bx, by, bz))       # Blender は (w,x,y,z)
        s_rest = Vector(retargeted["bind_local_s"][k])
        rest_inv = Matrix.LocRotScale(t_rest, q_rest, s_rest).inverted()

        # 回転と並進は basis 上で独立（回転成分は t_anim に依存せず、
        # 並進成分は q_anim に依存しない）。よってチャンネルごとに別々の
        # キー数でも安全に打てる（キー削減後は本数が食い違うため重要）。
        for t, (x, y, z, w) in r_keys:
            basis = rest_inv @ Matrix.LocRotScale(t_rest, Quaternion((w, x, y, z)),
                                                  s_rest)
            pb.rotation_quaternion = basis.to_quaternion()
            pb.keyframe_insert(data_path="rotation_quaternion", frame=t * fps)

        if name == root_joint and root_trans_keys:
            for t, tv in root_trans_keys:
                basis = rest_inv @ Matrix.LocRotScale(Vector(tv), q_rest, s_rest)
                pb.location = basis.to_translation()
                pb.keyframe_insert(data_path="location", frame=t * fps)

    # ベイクしたモーキャプは LINEAR 補間にする。
    # 既定のベジェはキー間でオーバーシュートし、キー削減の「許容誤差n度」が
    # 実測2〜3倍に膨らむ（実測: tol=1.0でベジェ2.94° / リニア1.45°）。
    # glTF 出力側も LINEAR サンプラーなので、こちらに合わせると挙動も一致する。
    _set_linear_interpolation(action)

    max_t = retargeted["times"][-1] if retargeted["times"] else 0.0
    scene = bpy.context.scene
    scene.frame_start = 0
    scene.frame_end = max(1, int(max_t * fps))


def _set_linear_interpolation(action):
    for fc in action.fcurves:
        for kp in fc.keyframe_points:
            kp.interpolation = "LINEAR"
        fc.update()


# ============================================================
# 設定・マッピング（Scene に常設。ダイアログだと21行の表を扱いにくいため）
# ============================================================
def _poll_armature(self, obj):
    return obj.type == "ARMATURE"


class MOCAP_PG_bone_map(bpy.types.PropertyGroup):
    source: bpy.props.StringProperty(name="ソースジョイント")
    target: bpy.props.StringProperty(
        name="ターゲットボーン",
        description="空欄なら未割当（そのボーンはバインドポーズのまま動かない）")
    twist_deg: bpy.props.FloatProperty(
        name="ひねり補正", default=0.0, min=-180.0, max=180.0,
        description="ボーン軸まわりの補正角。曲がる方向がおかしい時に調整する")


class MOCAP_PG_retarget(bpy.types.PropertyGroup):
    armature: bpy.props.PointerProperty(
        type=bpy.types.Object, name="ターゲット", poll=_poll_armature)
    source_mode: bpy.props.EnumProperty(
        name="ソース",
        items=[("session", "直前のキャプチャ", ""),
               ("file", ".mocapdata.json", "")],
        default="session")
    mocap_path: bpy.props.StringProperty(name="ファイル", subtype="FILE_PATH")
    bind_pose: bpy.props.EnumProperty(
        name="バインドポーズ",
        items=[("t", "Tポーズ", "Mixamo/UE系など腕が真横"),
               ("a", "Aポーズ", "VRoid系など腕が斜め下"),
               ("none", "補正なし", "動画の最初のフレームをモデルのバインドに直接対応")],
        default="t")
    arm_down_angle: bpy.props.FloatProperty(
        name="腕下げ角度", default=45.0, min=0.0, max=90.0)
    reduce_keys: bpy.props.BoolProperty(
        name="キーを削減する", default=False,
        description="補間で再現できる不要なキーを削除して編集しやすくする")
    tolerance_deg: bpy.props.FloatProperty(
        name="許容誤差 (度)", default=0.5, min=0.0, max=30.0,
        description="この角度以内の差なら補間で代用してキーを削る")
    mappings: bpy.props.CollectionProperty(type=MOCAP_PG_bone_map)
    show_mapping: bpy.props.BoolProperty(name="マッピングを表示", default=True)


def _ensure_mapping_rows(settings):
    """21ジョイント分の行を用意する（既存の割当は保持）"""
    existing = {m.source: (m.target, m.twist_deg) for m in settings.mappings}
    settings.mappings.clear()
    for name, _ in JOINTS:
        row = settings.mappings.add()
        row.source = name
        if name in existing:
            row.target, row.twist_deg = existing[name]


def _collect_mapping(settings, target_names):
    """UIの表から mapping / corrections を作る。存在しないボーン名は無視。"""
    mapping, corrections = {}, {}
    for row in settings.mappings:
        tgt = row.target.strip()
        if not tgt or tgt not in target_names:
            continue
        mapping[row.source] = tgt
        if abs(row.twist_deg) > 1e-9:
            corrections[tgt] = row.twist_deg
    return mapping, corrections


# ============================================================
# Operator
# ============================================================
class MOCAP_OT_init_mapping(bpy.types.Operator):
    bl_idname = "mocap.init_mapping"
    bl_label = "マッピング表を作成"
    bl_description = "21ジョイント分の割当表を用意する"

    def execute(self, context):
        _ensure_mapping_rows(context.scene.mocap_retarget)
        return {"FINISHED"}


class MOCAP_OT_guess_mapping(bpy.types.Operator):
    bl_idname = "mocap.guess_mapping"
    bl_label = "名前から推測"
    bl_description = ("ボーン名から割当候補を自動入力する（確認・修正してから実行）")
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        st = context.scene.mocap_retarget
        if st.armature is None:
            self.report({"ERROR"}, "ターゲットアーマチュアを選択してください")
            return {"CANCELLED"}
        _ensure_mapping_rows(st)
        names = [b.name for b in st.armature.data.bones]
        guessed = retarget.guess_mapping(names)
        for row in st.mappings:
            row.target = guessed.get(row.source, "")
        self.report({"INFO"},
                   f"{len(guessed)}/{len(JOINTS)} ジョイントを推測しました")
        return {"FINISHED"}


class MOCAP_OT_clear_mapping(bpy.types.Operator):
    bl_idname = "mocap.clear_mapping"
    bl_label = "割当をクリア"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        for row in context.scene.mocap_retarget.mappings:
            row.target = ""
            row.twist_deg = 0.0
        return {"FINISHED"}


class MOCAP_OT_save_mapping(bpy.types.Operator):
    bl_idname = "mocap.save_mapping"
    bl_label = "マッピングを保存"
    bl_description = "スタンドアロンアプリと同じ .retarget.json 形式で保存する"
    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.json", options={"HIDDEN"})

    def execute(self, context):
        st = context.scene.mocap_retarget
        names = ([b.name for b in st.armature.data.bones] if st.armature else [])
        mapping, corrections = _collect_mapping(st, names)
        try:
            Path(self.filepath).write_text(
                json.dumps({"mapping": mapping, "corrections": corrections},
                           indent=2, ensure_ascii=False), encoding="utf-8")
        except OSError as e:
            self.report({"ERROR"}, f"保存に失敗しました: {e}")
            return {"CANCELLED"}
        self.report({"INFO"}, f"保存しました: {Path(self.filepath).name}")
        return {"FINISHED"}

    def invoke(self, context, event):
        if not self.filepath:
            self.filepath = "mapping" + retarget.MAPPING_SUFFIX
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}


class MOCAP_OT_load_mapping(bpy.types.Operator):
    bl_idname = "mocap.load_mapping"
    bl_label = "マッピングを読込"
    filepath: bpy.props.StringProperty(subtype="FILE_PATH")
    filter_glob: bpy.props.StringProperty(default="*.json", options={"HIDDEN"})

    def execute(self, context):
        st = context.scene.mocap_retarget
        try:
            data = json.loads(Path(self.filepath).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as e:
            self.report({"ERROR"}, f"読み込みに失敗しました: {e}")
            return {"CANCELLED"}
        _ensure_mapping_rows(st)
        mapping = data.get("mapping", {})
        corrections = data.get("corrections", {})
        names = {b.name for b in st.armature.data.bones} if st.armature else set()
        missing = 0
        for row in st.mappings:
            tgt = mapping.get(row.source, "")
            if tgt and names and tgt not in names:
                missing += 1
                tgt = ""
            row.target = tgt
            row.twist_deg = corrections.get(tgt, 0.0) if tgt else 0.0
        msg = f"読み込みました: {Path(self.filepath).name}"
        if missing:
            msg += f"（ターゲットに無いボーン {missing} 件は未割当）"
        self.report({"INFO"}, msg)
        return {"FINISHED"}

    def invoke(self, context, event):
        context.window_manager.fileselect_add(self)
        return {"RUNNING_MODAL"}


class MOCAP_OT_retarget(bpy.types.Operator):
    bl_idname = "mocap.retarget"
    bl_label = "リターゲット実行"
    bl_options = {"REGISTER", "UNDO"}

    def execute(self, context):
        st = context.scene.mocap_retarget
        armature_obj = st.armature
        if armature_obj is None or armature_obj.type != "ARMATURE":
            self.report({"ERROR"}, "ターゲットアーマチュアを選択してください")
            return {"CANCELLED"}

        if st.source_mode == "session":
            path = capture_operator._state.get("last_mocap_path")
            if not path:
                self.report({"ERROR"}, "このセッションでのキャプチャがありません")
                return {"CANCELLED"}
        else:
            if not st.mocap_path:
                self.report({"ERROR"}, "mocapdataファイルを指定してください")
                return {"CANCELLED"}
            path = bpy.path.abspath(st.mocap_path)
        try:
            mocap = capture_project.load(path)
        except (OSError, ValueError) as e:
            self.report({"ERROR"}, str(e))
            return {"CANCELLED"}

        target = BlenderTargetModel(armature_obj)
        mapping, corrections = _collect_mapping(st, set(target.joint_names))
        if not mapping:
            self.report({"ERROR"},
                       "マッピングが空です。「名前から推測」または手動で割り当ててください")
            return {"CANCELLED"}

        result = retarget.retarget(
            mocap, target, mapping, corrections,
            tpose_reference=(st.bind_pose != "none"),
            arm_down_angle=st.arm_down_angle if st.bind_pose == "a" else 0.0)

        msg = (f"リターゲット完了: マッピング {len(mapping)}/{len(JOINTS)}, "
               f"スケール {result['root_scale']:.3f}")
        if st.reduce_keys:
            result = keyreduce.reduce_retargeted(result, st.tolerance_deg)
            rs = result["reduction_stats"]
            msg += f" / キー {rs['before']}→{rs['after']} ({rs['ratio'] * 100:.0f}%)"

        apply_retargeted_to_armature(result, armature_obj)
        self.report({"INFO"}, msg)
        return {"FINISHED"}


class MOCAP_OT_reduce_keys(bpy.types.Operator):
    """既存アクションのキーを後から削減する（リターゲット済みに限らず使える）"""
    bl_idname = "mocap.reduce_keys"
    bl_label = "キー削減"
    bl_options = {"REGISTER", "UNDO"}

    tolerance_deg: bpy.props.FloatProperty(
        name="許容誤差 (度)", default=0.5, min=0.0, max=30.0,
        description="この角度以内の差なら補間で代用してキーを削る")
    set_linear: bpy.props.BoolProperty(
        name="リニア補間にする", default=True,
        description="削減の誤差計算は線形補間が前提。ベジェのままだと"
                    "キー間でオーバーシュートし、実際の誤差が2〜3倍になる")

    @classmethod
    def poll(cls, context):
        obj = context.active_object
        return (obj is not None and obj.type == "ARMATURE"
                and obj.animation_data and obj.animation_data.action)

    def execute(self, context):
        obj = context.active_object
        action = obj.animation_data.action

        # data_path ごとに F-Curve をまとめる（回転4本 / 位置3本を1組として扱う）
        groups = {}
        for fc in action.fcurves:
            groups.setdefault(fc.data_path, {})[fc.array_index] = fc

        before = after = 0
        for data_path, chans in groups.items():
            if data_path.endswith("rotation_quaternion") and len(chans) == 4:
                fcs = [chans[i] for i in range(4)]
                times = [kp.co.x for kp in fcs[0].keyframe_points]
                if len(times) <= 2:
                    continue
                # Blender は (w,x,y,z)、keyreduce は (x,y,z,w)
                keys = [(times[i],
                         (fcs[1].keyframe_points[i].co.y,
                          fcs[2].keyframe_points[i].co.y,
                          fcs[3].keyframe_points[i].co.y,
                          fcs[0].keyframe_points[i].co.y))
                        for i in range(len(times))]
                kept = {k[0] for k in keyreduce.reduce_rotation_keys(
                    keys, self.tolerance_deg)}
            elif data_path.endswith("location") and len(chans) == 3:
                fcs = [chans[i] for i in range(3)]
                times = [kp.co.x for kp in fcs[0].keyframe_points]
                if len(times) <= 2:
                    continue
                keys = [(times[i], (fcs[0].keyframe_points[i].co.y,
                                    fcs[1].keyframe_points[i].co.y,
                                    fcs[2].keyframe_points[i].co.y))
                        for i in range(len(times))]
                tol = keyreduce.auto_translation_tolerance(keys)
                kept = {k[0] for k in keyreduce.reduce_vector_keys(keys, tol)}
            else:
                continue

            before += len(times) * len(fcs)
            for fc in fcs:
                for kp in reversed(list(fc.keyframe_points)):
                    if kp.co.x not in kept:
                        fc.keyframe_points.remove(kp)
                fc.update()
                after += len(fc.keyframe_points)

        if before == 0:
            self.report({"WARNING"}, "削減できるキーがありませんでした")
            return {"CANCELLED"}
        if self.set_linear:
            _set_linear_interpolation(action)
        self.report({"INFO"},
                   f"キー削減: {before} → {after} ({after / before * 100:.0f}%)")
        return {"FINISHED"}

    def invoke(self, context, event):
        return context.window_manager.invoke_props_dialog(self)


_classes = (MOCAP_PG_bone_map, MOCAP_PG_retarget,
            MOCAP_OT_init_mapping, MOCAP_OT_guess_mapping, MOCAP_OT_clear_mapping,
            MOCAP_OT_save_mapping, MOCAP_OT_load_mapping,
            MOCAP_OT_retarget, MOCAP_OT_reduce_keys)


def register():
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.types.Scene.mocap_retarget = bpy.props.PointerProperty(
        type=MOCAP_PG_retarget)


def unregister():
    if hasattr(bpy.types.Scene, "mocap_retarget"):
        del bpy.types.Scene.mocap_retarget
    for c in reversed(_classes):
        try:
            bpy.utils.unregister_class(c)
        except RuntimeError:
            pass
