"""CG2 Level Pipeline — 自作 DirectX12 エンジン用の Blender レベル配置アドオン。

インストール:
    Blender → 編集 → プリファレンス → アドオン → 「ディスクからインストール」
    → このファイル(cg2_blender_addon.py)を選択 → チェックを入れて有効化
    → アドオン設定の「Project フォルダ」に CG2_0_1/Project の絶対パスを設定

使い方(3Dビューのサイドバー N → 「CG2」タブ):
    1. オブジェクトを選んで役割を設定する
         メッシュ   … 「アセットとして書き出す」ON + タグ(Terrain 等)
                       → Assets/Models/<名前>/ に書き出してクックし、シーンにも配置する
         エンプティ … プレハブ名(敵/プレイヤー) or 既存モデル(dir/file)を指定して「置くだけ」
         カーブ     … スプライン種別(敵の経路 / カメラ経路 等)を指定
    2. 「Export & Cook」ボタンを押す
       → glTF 書き出し → cook_assets.py → layout_to_scene_json.py まで自動で走る
       → エンジンを再ビルドする必要は無い(実行中なら再読み込みで反映)

## 設計上の重要な約束ごと(ここを外すと壊れる)

- **アセットは必ず原点で書き出す**。cook_assets.py は glTF ノードのワールド変換を
  頂点へ焼き込むため、位置を持ったまま書き出すと「メッシュに焼かれた位置」と
  「シーン JSON の位置」で二重に動いてしまう。
  そこで書き出し時だけ一時的に変換を単位行列へ戻し、実際の配置はシーン JSON 側に持たせる。
  こうするとエンジン内エディタで後から掴んで動かせる(既存のドラッグ&ドロップ配置と同じ扱い)。

- **同じ物を複数置くならエンプティで置く**。メッシュを複製して並べると cook_assets.py が
  全部を 1 つの巨大メッシュに統合してしまい、個別に動かせず使い回しも効かなくなる。
  1 回だけアセット化 → あとはエンプティ(dir/file 指定)を Alt+D で複製して並べる。

- **カーブの制御点は文字列プロパティへ焼く**。glTF はカーブを表現できず、Blender は
  カーブの制御点を書き出さない(実測で確認済み)。そのため点列を glTF ワールド空間へ
  変換したうえで engine_spline_points(JSON文字列)に載せて渡す。

- **法線マップは全マテリアルに割り当てる**。エンジンは PSO(シェーダー)を submesh[0] の
  マテリアルだけで選ぶため、法線マップの付いていないマテリアルが先頭に来ると
  そのモデル全体が PBR にならない。凹凸不要な面にも中立な法線マップを割り当てること。
"""

bl_info = {
    "name": "CG2 Level Pipeline",
    "author": "CG2_0_1",
    "version": (1, 0, 0),
    "blender": (4, 2, 0),
    "location": "View3D > Sidebar (N) > CG2",
    "description": "自作エンジン用のレベル配置(地形/プロップ/プレハブ/スプライン)を書き出す",
    "category": "Import-Export",
}

import hashlib
import json
import os
import subprocess
import sys

import bpy
from bpy.props import BoolProperty, EnumProperty, FloatProperty, StringProperty
from mathutils import Matrix

# EntityTag.h と手で同期させる。滅多に増えないので列挙で持つ。
_ENGINE_TAGS = [
    "None", "Player", "PlayerBullet", "PlayerMelee", "Enemy", "EnemyAttack",
    "Boss", "Terrain", "Skybox", "RailControlPoint", "SpawnPoint",
    "CameraPoint", "FX",
]
_SPLINE_TAGS = [
    "PlayerRailSpline", "EnemyPathSpline", "FloatingPathSpline",
    "CameraPathSpline", "CameraLookAtSpline",
]

_TAG_ITEMS = [(t, t, "") for t in _ENGINE_TAGS]
_SPLINE_ITEMS = [("", "(なし)", "スプラインとして書き出さない")] + [(t, t, "") for t in _SPLINE_TAGS]


# ============================================================
# プリファレンス
# ============================================================

class CG2Preferences(bpy.types.AddonPreferences):
    bl_idname = __name__

    project_root: StringProperty(
        name="Project フォルダ",
        description="CG2_0_1/Project の絶対パス(Assets/ と Resources/ がある場所)",
        subtype='DIR_PATH',
        default="",
    )

    def draw(self, context):
        self.layout.prop(self, "project_root")


def _project_root(context) -> str:
    prefs = context.preferences.addons[__name__].preferences
    return bpy.path.abspath(prefs.project_root) if prefs.project_root else ""


def _list_prefabs(context) -> list:
    """Resources/Json/Prefabs/*.json を読んでプレハブ名の候補を作る。"""
    root = _project_root(context)
    if not root:
        return []
    d = os.path.join(root, "Resources", "Json", "Prefabs")
    if not os.path.isdir(d):
        return []
    return sorted(os.path.splitext(f)[0] for f in os.listdir(d) if f.endswith(".json"))


# ============================================================
# オブジェクトごとの設定(カスタムプロパティとして保存される)
# ============================================================

class CG2ObjectProps(bpy.types.PropertyGroup):
    export_asset: BoolProperty(
        name="アセットとして書き出す",
        description="このメッシュを Assets/Models/<名前>/ に書き出してクックする",
        default=False,
    )
    tag: EnumProperty(name="タグ", items=_TAG_ITEMS, default="Terrain")
    prefab: StringProperty(
        name="プレハブ名",
        description="Resources/Json/Prefabs/<名前>.json のファイル名(拡張子なし)",
        default="",
    )
    model_dir: StringProperty(
        name="モデルの場所",
        description='既にクック済みのモデルを置く場合。例: "Resources/Models/Rock"',
        default="",
    )
    model_file: StringProperty(
        name="モデルのファイル名",
        description='例: "rock.mesh"',
        default="",
    )
    spline_tag: EnumProperty(name="スプライン種別", items=_SPLINE_ITEMS, default="")

    # --- ウェーブ敵（固定砲台）: Wave JSON へ書き出す（シーンJSONではない）---
    # エンプティにチェックを入れると、そのワールド座標に出現する固定砲台になる。
    # ScreenHover（画面相対）の敵はインエンジンの Wave Editor 側で置くこと。
    is_turret: BoolProperty(
        name="ウェーブ敵として出す(固定砲台)",
        description="このエンプティのワールド座標に出現する固定砲台にする。Wave JSON へ書き出される",
        default=False,
    )
    trigger_sec: FloatProperty(
        name="出現秒", description="ステージ開始から何秒で出現するか", default=5.0, min=0.0)
    retreat_sec: FloatProperty(
        name="退避秒", description="-1 なら退避しない（撃破まで居座る）", default=-1.0)
    shoot_interval_sec: FloatProperty(
        name="射撃間隔秒", description="0 で射撃なし", default=5.0, min=0.0)
    enemy_type: StringProperty(
        name="敵タイプ",
        description='攻撃ロール（Static プレハブなら通常 "Drone" のままで可）',
        default="Drone")


# ============================================================
# 書き出し
# ============================================================

def _asset_safe_name(name: str) -> str:
    """オブジェクト名をアセットのフォルダ名/ファイル名に使える ASCII へ正規化する。

    エンジンはモデルパスを std::string で持ち、Windows の既定ロケールでファイルを開くため、
    非 ASCII のパスだと開けずに 0 バイト扱いになり assert(sizeInBytes > 0) で落ちる。
    （Blender の日本語 UI だと立方体・平面… と非 ASCII の既定名が付くので普通に踏む）

    cook_assets.py の _safe_name は Python の str.isalnum() が Unicode 対応なため
    '立方体'.isalnum() == True となり日本語を素通しする。ここでは使えない。
    """
    out = "".join(c if (c.isascii() and (c.isalnum() or c in "-_")) else "_" for c in name)
    out = out.strip("_")
    if not out:
        # 名前が丸ごと非 ASCII（"立方体" 等）だと空になる。ここで固定文字にすると
        # "立方体" と "平面" が両方同じフォルダに書き出されて上書きし合うので、
        # 元名のハッシュで一意にする（同じ名前なら常に同じフォルダ＝再クックが安定する）
        out = "Asset_" + hashlib.md5(name.encode("utf-8")).hexdigest()[:6]
    if out[0].isdigit():
        out = "A" + out
    return out


def _blender_to_gltf_point(v) -> list:
    """Blender(Z-up) の点を glTF(Y-up) 空間へ。実測: Blender(1,2,3) → glTF(1,3,-2)"""
    return [v.x, v.z, -v.y]


def _write_extras(obj, props, context) -> dict:
    """このオブジェクトが glTF の extras として持つべき値を組み立てて書き込む。"""
    extras = {}

    if props.spline_tag:
        extras["engine_spline_tag"] = props.spline_tag
        pts = []
        for spline in obj.data.splines:
            src = spline.bezier_points if spline.type == 'BEZIER' else spline.points
            for p in src:
                world = obj.matrix_world @ p.co.to_3d()
                pts.append(_blender_to_gltf_point(world))
        # glTF はカーブを表現できないので、点列を glTF ワールド空間へ変換して文字列で渡す
        extras["engine_spline_points"] = json.dumps(pts)
    else:
        extras["engine_tag"] = props.tag
        if props.prefab:
            extras["engine_prefab"] = props.prefab
        elif props.model_dir and props.model_file:
            extras["engine_model_dir"] = props.model_dir
            extras["engine_model_file"] = props.model_file

    for k, v in extras.items():
        obj[k] = v
    return extras


def _clear_extras(obj) -> None:
    for k in ("engine_tag", "engine_prefab", "engine_model_dir", "engine_model_file",
              "engine_spline_tag", "engine_spline_points"):
        if k in obj:
            del obj[k]


def _export_asset(obj, root: str) -> tuple[str, str]:
    """メッシュ 1 個を原点で Assets/Models/<名前>/<名前>.gltf へ書き出す。

    cook_assets.py がワールド変換を頂点へ焼くので、書き出し中だけ変換を単位行列に戻す。
    配置はシーン JSON 側が持つ(= エンジン内で後から動かせる)。
    """
    name = _asset_safe_name(obj.name)
    out_dir = os.path.join(root, "Assets", "Models", name)
    os.makedirs(out_dir, exist_ok=True)
    out_path = os.path.join(out_dir, f"{name}.gltf")

    saved = obj.matrix_world.copy()
    saved_sel = [o for o in bpy.context.selected_objects]
    saved_active = bpy.context.view_layer.objects.active
    try:
        obj.matrix_world = Matrix.Identity(4)
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        bpy.ops.export_scene.gltf(
            filepath=out_path,
            export_format='GLTF_SEPARATE',
            use_selection=True,
            export_extras=True,
            export_yup=True,
            export_apply=True,          # モディファイアを適用してから書き出す
        )
    finally:
        obj.matrix_world = saved
        bpy.ops.object.select_all(action='DESELECT')
        for o in saved_sel:
            o.select_set(True)
        bpy.context.view_layer.objects.active = saved_active

    return f"Resources/Models/{name}", f"{name}.mesh"


_layout_module_cache = {"root": None, "mod": None}


def _load_layout_module(root: str):
    """layout_to_scene_json を import して返す（検証済みの座標変換を再利用するため）。

    自動同期はクック/glTF書き出しを通さず bpy から直接シーン JSON を組むが、
    座標変換だけは Export 経路と同じ関数を使いたい（別実装にすると必ずズレる）。
    毎 tick 呼ばれるので import はキャッシュする。
    """
    if _layout_module_cache["root"] == root and _layout_module_cache["mod"] is not None:
        return _layout_module_cache["mod"]
    lp = os.path.join(root, "tools", "BlenderPipeline")
    if lp not in sys.path:
        sys.path.insert(0, lp)
    import layout_to_scene_json as L
    _layout_module_cache["root"] = root
    _layout_module_cache["mod"] = L
    return L


def _blender_matrix_to_engine_trs(mw, L) -> tuple:
    """Blender の matrix_world → エンジンの (scale, euler, translate)。

    Export 経路（glTF が Y-up 変換 → layout が mirror+decompose）と一致させるため、
    ここでは Y-up 変換を自前で掛けてから layout の decompose を呼ぶ。
    """
    from mathutils import Matrix
    # Blender(Z-up) → glTF(Y-up)
    y = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    mirror = Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0))
    m_engine = mirror @ (y @ mw @ y.inverted()) @ mirror
    m_tuple = tuple(tuple(m_engine[i][j] for j in range(4)) for i in range(4))
    return L.decompose(m_tuple)  # (scale, euler, translate)


def _scene_data_from_bpy(context, root: str) -> dict:
    """bpy のオブジェクトから直接シーン JSON の dict を組む（クック無し・高速）。

    layout_to_scene_json.py が glTF から作るのと同じ形・同じ座標にする。
    固定砲台エンプティは Wave 行きなのでここには含めない。
    """
    L = _load_layout_module(root)
    scene_name = context.scene.cg2_scene_name or "StagePlay"
    entries = []

    def trs_json(mw):
        sc, eu, tr = _blender_matrix_to_engine_trs(mw, L)
        return {"scale": list(sc), "rotate": list(eu), "translate": list(tr)}

    for obj in context.scene.objects:
        p = obj.cg2
        if obj.type == 'CURVE' and p.spline_tag:
            pts = []
            for spline in obj.data.splines:
                src = spline.bezier_points if spline.type == 'BEZIER' else spline.points
                for bp in src:
                    w = obj.matrix_world @ bp.co.to_3d()
                    g = _blender_to_gltf_point(w)
                    pts.append([-g[0], g[1], g[2]])  # glTF→エンジン(X反転)
            entries.append({"type": "Spline", "name": obj.name,
                            "tag": p.spline_tag, "points": pts})
            continue

        if obj.type == 'EMPTY':
            if p.is_turret:
                continue  # 砲台は Wave JSON 行き
            if p.prefab:
                entries.append({"type": "Prefab", "name": obj.name,
                                "prefab": p.prefab, "transform": trs_json(obj.matrix_world)})
            elif p.model_dir and p.model_file:
                entries.append({"type": "Object3D", "name": obj.name, "tag": p.tag,
                                "dir": p.model_dir, "file": p.model_file,
                                "transform": trs_json(obj.matrix_world)})
            continue

        if obj.type == 'MESH' and p.export_asset and p.model_dir and p.model_file:
            # アセット化メッシュは配置プロキシと同じ扱い（位置はここ、実体はクック済み .mesh）
            entries.append({"type": "Object3D", "name": obj.name, "tag": p.tag,
                            "dir": p.model_dir, "file": p.model_file,
                            "transform": trs_json(obj.matrix_world)})

    return {"scene": scene_name, "objects": entries}


def _collect_turrets(context) -> list:
    """固定砲台エンプティ → Wave エントリ用の dict 一覧。"""
    turrets = []
    for obj in context.scene.objects:
        if obj.type == 'EMPTY' and obj.cg2.is_turret and obj.cg2.prefab:
            g = _blender_to_gltf_point(obj.matrix_world.translation)
            turrets.append({
                "prefab": obj.cg2.prefab,
                "enemy_type": obj.cg2.enemy_type or "Drone",
                "trigger_sec": obj.cg2.trigger_sec,
                "retreat_sec": obj.cg2.retreat_sec,
                "shoot_interval_sec": obj.cg2.shoot_interval_sec,
                "world": [-g[0], g[1], g[2]],
            })
    return turrets


def _merge_wave_json(root: str, turrets: list) -> tuple[int, int]:
    """固定砲台エントリを Wave JSON へマージする。

    Blender 管轄の識別は **構造**で行う: 「positions を持つエントリ = Blender の固定砲台」。
    スプライン敵は spline_id、ホバー敵は camera_offset を持ち positions は持たないので、
    それらは温存される。マーカーキー方式にしないのは、インエンジンの Wave Editor が
    保存し直すと未知キーが消えてしまい識別できなくなるため（positions は保存される）。

    turrets: [{prefab, enemy_type, trigger_sec, retreat_sec, shoot_interval_sec, world}]
    戻り値: (温存した既存エントリ数, 書き出した砲台数)
    """
    wave_path = os.path.join(root, "Resources", "Json", "Waves", "stage1.json")

    existing = {"name": "stage1", "spawn_entries": []}
    if os.path.isfile(wave_path):
        try:
            with open(wave_path, "r", encoding="utf-8") as f:
                existing = json.load(f)
        except (OSError, json.JSONDecodeError):
            pass

    # positions を持たない既存エントリだけ残す（＝スプライン敵・ホバー敵は温存）
    kept = [e for e in existing.get("spawn_entries", [])
            if not (isinstance(e.get("positions"), list) and len(e["positions"]) > 0)]

    for t in turrets:
        kept.append({
            "enemy_type": t["enemy_type"],
            "prefab": t["prefab"],
            "trigger_sec": t["trigger_sec"],
            "retreat_sec": t["retreat_sec"],
            "traverse_sec": 8.0,
            "spline_id": "",
            "count": 1,
            "shoot_interval_sec": t["shoot_interval_sec"],
            "spawn_interval_sec": 5.0,
            "spawn_limit": 4,
            "child_prefab": "",
            "child_spline_id": "",
            "positions": [t["world"]],  # ワールド座標をそのまま（1エンプティ=1エントリ）
        })

    out = {"name": existing.get("name", "stage1"), "spawn_entries": kept}
    os.makedirs(os.path.dirname(wave_path), exist_ok=True)
    # 上書き前に .bak（シーンJSONと同じ安全策）
    if os.path.isfile(wave_path):
        try:
            import shutil
            shutil.copy2(wave_path, wave_path + ".bak")
        except OSError:
            pass
    with open(wave_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2, ensure_ascii=False)
    return (len(kept) - len(turrets), len(turrets))


def _run(cmd: list, cwd: str, label: str) -> tuple[bool, str]:
    try:
        r = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True)
    except OSError as e:
        return False, f"{label} を起動できません: {e}"
    if r.returncode != 0:
        tail = (r.stderr or r.stdout or "").strip().splitlines()
        return False, f"{label} 失敗: " + (tail[-1] if tail else f"exit {r.returncode}")
    return True, (r.stdout or "").strip()


class CG2_OT_export_and_cook(bpy.types.Operator):
    bl_idname = "cg2.export_and_cook"
    bl_label = "Export & Cook"
    bl_description = "アセット書き出し → クック → シーン JSON 生成 をまとめて実行する"

    def execute(self, context):
        root = _project_root(context)
        if not root or not os.path.isdir(os.path.join(root, "Assets")):
            self.report({'ERROR'}, "プリファレンスで Project フォルダを正しく設定してください")
            return {'CANCELLED'}

        scene_name = context.scene.cg2_scene_name or "Stage1"
        objects = [o for o in context.scene.objects if o.type in ('MESH', 'EMPTY', 'CURVE')]

        # 0) アセット名の衝突チェック。
        #    "Rock" と "岩Rock" は正規化するとどちらも Rock/ になり、後勝ちで黙って上書きされる。
        #    自動で番号を振ると "Cube.001" 等まで名前が汚れるので、ここは止めて直してもらう。
        claimed: dict = {}
        for obj in objects:
            if obj.type != 'MESH' or not obj.cg2.export_asset:
                continue
            safe = _asset_safe_name(obj.name)
            if safe in claimed:
                self.report({'ERROR'},
                            f"アセット名の衝突: 「{claimed[safe]}」と「{obj.name}」がどちらも "
                            f"{safe}/ に書き出されます。片方を別名（半角英数）にしてください")
                return {'CANCELLED'}
            claimed[safe] = obj.name

        # 1) アセット化するメッシュを個別に原点で書き出す
        exported = 0
        for obj in objects:
            p = obj.cg2
            if obj.type == 'MESH' and p.export_asset:
                try:
                    d, f = _export_asset(obj, root)
                except RuntimeError as e:
                    self.report({'ERROR'}, f"{obj.name} の書き出しに失敗: {e}")
                    return {'CANCELLED'}
                # 配置は proxy と同じ扱いに統一する(レイアウト側は dir/file だけ見ればよい)
                p.model_dir, p.model_file = d, f
                exported += 1

        # 2) クック(glTF → .mesh/.mat, PNG → .dds)
        if exported:
            ok, msg = _run([sys.executable, os.path.join(root, "tools", "Python", "cook_assets.py")],
                           root, "cook_assets.py")
            if not ok:
                self.report({'ERROR'}, msg)
                return {'CANCELLED'}

        # 3) レイアウト glTF を書き出す(extras に配置情報を載せる)
        #    固定砲台エンプティは Wave JSON 行きなので、シーンのレイアウトからは除外する。
        layout_dir = os.path.join(root, "Assets", "Scenes")
        os.makedirs(layout_dir, exist_ok=True)
        layout_path = os.path.join(layout_dir, f"{scene_name}_layout.gltf")

        turret_objs = [o for o in objects if o.type == 'EMPTY' and o.cg2.is_turret]
        layout_objs = [o for o in objects if o not in turret_objs]
        # 砲台は Wave 行きなのでシーン側 extras を持たせない（過去の残留も掃除）
        for o in turret_objs:
            _clear_extras(o)
        written = [o for o in layout_objs if _write_extras(o, o.cg2, context)]
        try:
            bpy.ops.export_scene.gltf(
                filepath=layout_path,
                export_format='GLTF_SEPARATE',
                export_extras=True,
                export_yup=True,
            )
        finally:
            for o in written:
                _clear_extras(o)   # .blend にゴミを残さない

        # 4) レイアウト glTF → シーン JSON
        out_json = os.path.join(root, "Resources", "Json", "Scenes", f"{scene_name}.json")
        ok, msg = _run([sys.executable,
                        os.path.join(root, "tools", "BlenderPipeline", "layout_to_scene_json.py"),
                        "--layout", layout_path, "--out", out_json], root, "layout_to_scene_json.py")
        if not ok:
            self.report({'ERROR'}, msg)
            return {'CANCELLED'}

        # 5) 固定砲台 → Wave JSON へマージ（スプライン敵・ホバー敵は温存）
        turrets = _collect_turrets(context)
        kept, ntur = _merge_wave_json(root, turrets)

        self.report({'INFO'},
                    f"完了: アセット {exported} / 砲台 {ntur}（既存 wave {kept} 温存） → "
                    f"{os.path.relpath(out_json, root)}")
        return {'FINISHED'}


# ============================================================
# 取り込み（エンジン → Blender）
# ============================================================

def _engine_trs_to_blender_matrix(scale, rotate, translate) -> Matrix:
    """エンジンのシーン JSON の transform を Blender のワールド行列へ。

    書き出し側(layout_to_scene_json.py)の逆変換。mathutils は列ベクトル規約。

      エンジンの MakeAffineMatrix は行ベクトルで S·(Rx·Ry·Rz)·T
        → 列ベクトルへ転置すると  M = T·(Rz·Ry·Rx)·S
      RH→LH の鏡映 S=diag(-1,1,1) は自分自身が逆行列なので M_gltf = S·M_engine·S
      Blender→glTF の Y-up 変換 Y を戻して  M_blender = Y⁻¹·M_gltf·Y
    """
    t = Matrix.Translation(translate)
    rz = Matrix.Rotation(rotate[2], 4, 'Z')
    ry = Matrix.Rotation(rotate[1], 4, 'Y')
    rx = Matrix.Rotation(rotate[0], 4, 'X')
    s = Matrix.Diagonal((scale[0], scale[1], scale[2], 1.0))
    m_engine = t @ rz @ ry @ rx @ s

    mirror = Matrix.Diagonal((-1.0, 1.0, 1.0, 1.0))
    m_gltf = mirror @ m_engine @ mirror

    # Y: Blender(Z-up) → glTF(Y-up) = (x, z, -y)
    y = Matrix(((1, 0, 0, 0), (0, 0, 1, 0), (0, -1, 0, 0), (0, 0, 0, 1)))
    return y.inverted() @ m_gltf @ y


def _gltf_point_to_blender(p) -> tuple:
    """glTF ワールド空間の点を Blender へ（_blender_to_gltf_point の逆）。"""
    return (p[0], -p[2], p[1])


def _engine_point_to_blender(p) -> tuple:
    """エンジン空間の点を Blender へ。X 反転で glTF に戻してから Y-up を解く。"""
    return _gltf_point_to_blender((-p[0], p[1], p[2]))


def _apply_scene_to_bpy(context, data: dict) -> tuple[int, int]:
    """シーン JSON の dict を Blender へ反映する（Import 本体。操作とタイマーで共用）。

    タイマーからも呼ぶのでオペレータや context.collection に依存せず、
    bpy.data / scene.collection の直接 API だけを使う。
    """
    coll = context.scene.collection
    updated = created = 0
    for entry in data.get("objects", []):
        etype = entry.get("type")
        name = entry.get("name", "")
        if not name or etype not in ("Object3D", "Prefab", "Spline"):
            continue

        obj = context.scene.objects.get(name)

        if etype == "Spline":
            pts = [_engine_point_to_blender(p) for p in entry.get("points", [])]
            if obj is None or obj.type != 'CURVE':
                cd = bpy.data.curves.new(name, type='CURVE')
                cd.dimensions = '3D'
                obj = bpy.data.objects.new(name, cd)
                coll.objects.link(obj)
                created += 1
            else:
                updated += 1
            obj.data.splines.clear()
            # エンジンは Catmull-Rom＝制御点を通る曲線。Blender 側もそれに合わせて
            # 「制御点を通る」ベジェ(AUTO ハンドル)で作る。POLY だと折れ線表示になり、
            # 実際にゲーム内で描かれる曲線と見た目が食い違って調整できない。
            # 制御点は bezier_points[i].co と 1 対 1（書き出し側もそこだけ読む＝往復で崩れない）。
            sp = obj.data.splines.new('BEZIER')
            if pts:
                sp.bezier_points.add(len(pts) - 1)
                for i, p in enumerate(pts):
                    bp = sp.bezier_points[i]
                    bp.co = p
                    bp.handle_left_type = 'AUTO'
                    bp.handle_right_type = 'AUTO'
            obj.matrix_world = Matrix.Identity(4)
            obj.cg2.spline_tag = entry.get("tag", "") or ""
            continue

        tf = entry.get("transform", {})
        mw = _engine_trs_to_blender_matrix(
            tf.get("scale", [1, 1, 1]), tf.get("rotate", [0, 0, 0]), tf.get("translate", [0, 0, 0]))

        if obj is None:
            # Blender 側に無い＝エンジン内エディタで置かれたもの。エンプティで受ける。
            obj = bpy.data.objects.new(name, None)
            obj.empty_display_type = 'PLAIN_AXES'
            coll.objects.link(obj)
            created += 1
        else:
            # 既にある＝Blender で作った本体。メッシュはそのままに配置だけ更新する。
            updated += 1

        obj.matrix_world = mw
        if etype == "Prefab":
            obj.cg2.prefab = entry.get("prefab", "")
        else:
            obj.cg2.tag = entry.get("tag", "None")
            obj.cg2.model_dir = entry.get("dir", "")
            obj.cg2.model_file = entry.get("file", "")

    return updated, created


class CG2_OT_import_scene(bpy.types.Operator):
    bl_idname = "cg2.import_scene"
    bl_label = "Import Scene"
    bl_description = "エンジンのシーン JSON を読み込み、配置を Blender 側へ反映する"

    def execute(self, context):
        root = _project_root(context)
        if not root:
            self.report({'ERROR'}, "プリファレンスで Project フォルダを設定してください")
            return {'CANCELLED'}

        scene_name = context.scene.cg2_scene_name or "StagePlay"
        path = os.path.join(root, "Resources", "Json", "Scenes", f"{scene_name}.json")
        if not os.path.isfile(path):
            self.report({'ERROR'}, f"シーン JSON がありません: {os.path.relpath(path, root)}")
            return {'CANCELLED'}

        try:
            with open(path, "r", encoding="utf-8") as f:
                data = json.load(f)
        except (OSError, json.JSONDecodeError) as e:
            self.report({'ERROR'}, f"読み込み失敗: {e}")
            return {'CANCELLED'}

        updated, created = _apply_scene_to_bpy(context, data)
        self.report({'INFO'}, f"取り込み完了: 更新 {updated} 件 / 新規 {created} 件")
        return {'FINISHED'}


# ============================================================
# 自動同期（双方向）: 1 本のタイマーが両方向を捌く
# ============================================================
#
# 反響（ping-pong）防止の要は「共有の同期済みハッシュ」1 つ。
#   scene_hash : いま同期が取れているシーン JSON の正規化ハッシュ（双方向の錨）
#   turret_hash: 砲台の状態（砲台は Blender 側だけが編集する＝一方向）
# 毎 tick:
#   1. Blender のシーンが scene_hash と違う → Blender が変えた → JSON 書き出し、両ハッシュ更新
#   2. そうでなく、ファイルが scene_hash と違う → エンジンが変えた → Import、scene_hash 更新
# 片方が書けば相手のハッシュが一致して読まないので、書き合いが起きない。
# 同時に両方が変わった時だけ「その tick で先に判定された側」が勝つ（1 手だけ相手が負ける）。

_sync_state = {"scene_hash": None, "turret_hash": None}


def _atomic_write_json(path: str, data: dict) -> None:
    """一時ファイルへ書いてから rename（原子的）。読み手が半端な JSON を掴まないように。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8") as f:
        json.dump(data, f, indent=2, ensure_ascii=False)
    os.replace(tmp, path)


def _round_floats(o, nd=4):
    if isinstance(o, float):
        return round(o, nd)
    if isinstance(o, list):
        return [_round_floats(x, nd) for x in o]
    if isinstance(o, dict):
        return {k: _round_floats(v, nd) for k, v in o.items()}
    return o


def _normalized_hash(scene_dict: dict) -> int:
    """浮動小数の桁を丸めてからハッシュ化。エンジンと Blender で JSON の文字形が
    違っても（値が同じなら）同じハッシュになる＝反響を正しく検出できる。"""
    return hash(json.dumps(_round_floats(scene_dict), sort_keys=True))


def _sync_tick():
    """双方向同期のタイマー本体。戻り値が次回までの秒（None で停止）。"""
    ctx = bpy.context
    scene = getattr(ctx, "scene", None)
    if scene is None or not getattr(scene, "cg2_auto_sync", False):
        return None

    root = _project_root(ctx)
    if not root or not os.path.isdir(os.path.join(root, "Assets")):
        return 1.0

    try:
        scene_data = _scene_data_from_bpy(ctx, root)
        turrets = _collect_turrets(ctx)
        scene_h = _normalized_hash(scene_data)
        turret_h = hash(json.dumps(turrets, sort_keys=True))
        scene_path = os.path.join(root, "Resources", "Json", "Scenes", scene_data["scene"] + ".json")

        # 初回：現状を基準にするだけ（無駄な書き/読みをしない）
        if _sync_state["scene_hash"] is None:
            _sync_state["scene_hash"] = scene_h
            _sync_state["turret_hash"] = turret_h
            return 0.3

        # 方向1: Blender が変わった → 書き出し
        if scene_h != _sync_state["scene_hash"] or turret_h != _sync_state["turret_hash"]:
            _atomic_write_json(scene_path, scene_data)
            _merge_wave_json(root, turrets)
            _sync_state["scene_hash"] = scene_h
            _sync_state["turret_hash"] = turret_h
            return 0.3

        # 方向2: エンジンがファイルを変えた → Import
        if os.path.isfile(scene_path):
            try:
                with open(scene_path, "r", encoding="utf-8") as f:
                    file_data = json.load(f)
            except (OSError, json.JSONDecodeError):
                return 0.3  # 書き込み途中。次の tick で拾い直す
            file_h = _normalized_hash(file_data)
            if file_h != _sync_state["scene_hash"]:
                _apply_scene_to_bpy(ctx, file_data)
                # 取り込んだ内容を基準にする＝次 tick で「Blender が変えた」と誤検出して
                # 書き戻さない（＝反響を断つ）。Blender 側から作り直した scene_data で取り直す。
                _sync_state["scene_hash"] = _normalized_hash(_scene_data_from_bpy(ctx, root))
    except Exception as e:  # タイマーは例外で止まると復帰しないので握る
        print("[cg2 sync] error:", e)
    return 0.3


def _set_auto_sync(enable: bool) -> None:
    if enable:
        _sync_state["scene_hash"] = None   # 有効化直後は現状を基準に取り直す
        _sync_state["turret_hash"] = None
        if not bpy.app.timers.is_registered(_sync_tick):
            bpy.app.timers.register(_sync_tick, first_interval=0.3)
    else:
        if bpy.app.timers.is_registered(_sync_tick):
            bpy.app.timers.unregister(_sync_tick)


def _on_auto_sync_toggled(self, context):
    _set_auto_sync(self.cg2_auto_sync)


# ============================================================
# UI
# ============================================================

class CG2_PT_panel(bpy.types.Panel):
    bl_label = "CG2 Level Pipeline"
    bl_idname = "CG2_PT_panel"
    bl_space_type = 'VIEW_3D'
    bl_region_type = 'UI'
    bl_category = "CG2"

    def draw(self, context):
        layout = self.layout

        if not _project_root(context):
            layout.label(text="プリファレンスで Project フォルダを設定", icon='ERROR')

        layout.prop(context.scene, "cg2_scene_name")
        row = layout.row(align=True)
        row.operator("cg2.export_and_cook", icon='EXPORT')
        row.operator("cg2.import_scene", icon='IMPORT')
        layout.prop(context.scene, "cg2_auto_sync")
        if context.scene.cg2_auto_sync:
            layout.label(text="双方向で自動同期中（配置のみ）", icon='CHECKMARK')
            layout.label(text="※ジオメトリを作り直した時は Export & Cook", icon='INFO')
        layout.separator()

        obj = context.active_object
        if not obj:
            layout.label(text="オブジェクトを選択してください")
            return

        box = layout.box()
        box.label(text=f"{obj.name} ({obj.type})", icon='OBJECT_DATA')
        p = obj.cg2

        if obj.type == 'CURVE':
            box.prop(p, "spline_tag")
            if not p.spline_tag:
                box.label(text="種別を選ぶとスプラインとして出力", icon='INFO')
            return

        if obj.type == 'MESH':
            box.prop(p, "export_asset")
            if p.export_asset:
                safe = _asset_safe_name(obj.name)
                box.label(text=f"→ Assets/Models/{safe}/", icon='CHECKMARK')
                if safe != obj.name:
                    # 非 ASCII のままだとエンジンがパスを開けず落ちるので黙って直す。
                    # 直したことは見えるようにしておく（フォルダ名が変わるため）
                    box.label(text=f"名前を {safe} に正規化しました", icon='INFO')
                    box.label(text="（エンジンは非ASCIIパスを開けません）", icon='BLANK1')
                box.label(text="法線マップは全マテリアルに割当を", icon='INFO')

        if obj.type == 'MESH':
            box.prop(p, "tag")

        if obj.type == 'EMPTY':
            # 固定砲台モード（Wave JSON 行き）か、通常のシーン配置か
            box.prop(p, "is_turret")
            prefabs = _list_prefabs(context)

            if p.is_turret:
                col = box.column(align=True)
                col.prop(p, "prefab")
                if p.prefab and prefabs and p.prefab not in prefabs:
                    col.label(text=f"該当プレハブ無し: {p.prefab}", icon='ERROR')
                elif not p.prefab:
                    col.label(text="砲台プレハブ名が必要（例: turret）", icon='ERROR')
                col.prop(p, "enemy_type")
                col.prop(p, "trigger_sec")
                col.prop(p, "retreat_sec")
                col.prop(p, "shoot_interval_sec")
                col.label(text="→ Wave JSON へ（ワールド座標で出現）", icon='CHECKMARK')
                col.label(text="画面追従のホバー敵はエンジンのWave Editorで", icon='INFO')
                return

            box.prop(p, "tag")
            col = box.column(align=True)
            col.prop(p, "prefab")
            if p.prefab:
                if prefabs and p.prefab not in prefabs:
                    col.label(text=f"該当プレハブ無し: {p.prefab}", icon='ERROR')
                else:
                    col.label(text="→ シーンにプレハブ配置", icon='CHECKMARK')
            if prefabs and not p.prefab:
                col.label(text="候補: " + ", ".join(prefabs[:4]) + ("..." if len(prefabs) > 4 else ""))

            if not p.prefab:
                col = box.column(align=True)
                col.prop(p, "model_dir")
                col.prop(p, "model_file")
                if not (p.model_dir and p.model_file):
                    col.label(text="プレハブ名 か モデル指定 のどちらかが必要", icon='ERROR')


_classes = (CG2Preferences, CG2ObjectProps, CG2_OT_export_and_cook, CG2_OT_import_scene, CG2_PT_panel)


def register():
    for c in _classes:
        bpy.utils.register_class(c)
    bpy.types.Object.cg2 = bpy.props.PointerProperty(type=CG2ObjectProps)
    # 既定は "StagePlay"。エンジンが起動時に自動ロードするのが
    # Resources/Json/Scenes/StagePlay.json 固定なので、ここを変えると読まれなくなる。
    bpy.types.Scene.cg2_scene_name = StringProperty(
        name="シーン名", description="Resources/Json/Scenes/<名前>.json（StagePlay がエンジンの自動ロード先）",
        default="StagePlay")
    bpy.types.Scene.cg2_auto_sync = BoolProperty(
        name="自動同期(双方向)",
        description="配置の変更を Blender↔エンジンで自動同期する（クックはしない）。"
                    "どちらで動かしても相手に即反映。ジオメトリ作成時のみ Export & Cook",
        default=False, update=_on_auto_sync_toggled)


def unregister():
    _set_auto_sync(False)   # タイマーを止めてから解除
    del bpy.types.Scene.cg2_auto_sync
    del bpy.types.Scene.cg2_scene_name
    del bpy.types.Object.cg2
    for c in reversed(_classes):
        bpy.utils.unregister_class(c)


if __name__ == "__main__":
    register()
