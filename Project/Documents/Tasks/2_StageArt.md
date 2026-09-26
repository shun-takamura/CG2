# 2. ステージのアート

> **優先度 S** — 先生の指摘「STGの画面が全体的にダサい」の本体。
> 環境の**枠**（空・光・フォグ・レール）は完成済みだが、**その中に何も置かれていない**。
> 動画の尺の大半がこのステージなので、ここが空白だと他を全部やっても画が持たない。

最終更新: 2026-09-16

---

## 0. このドキュメントの位置づけ

`1_Sound.md` に続く優先度2番目のタスク。数字プレフィックスが優先度。

**着手前に §7「落とし穴」を必読。** Blender連携とレール構造に、黙ってズレる罠が大量にある。

---

## 1. 現状サマリ

「STGの画面がダサい」への対策は `stg_visual_overhaul` 計画としてフェーズ分割済み。

| Phase | 内容 | 状態 |
|---|---|---|
| **A-1** | セクション基盤（空/光/フォグの時間駆動切替）＋尺3分化 | ✅ 実装済み |
| **A-2** | 骨組みレール（60点・全長約4,382m） | ✅ 実装済み |
| **B** | 距離フォグ | ✅ 実装済み（`Fog.hlsli` 実在） |
| **C** | 地形・プロップの散布 | ❌ **未着手** |
| **D** | 4セクションのアート仕上げ | ❌ **未着手** |
| **E** | 敵の置き直し | ❌ 未着手（→ `3_EnemyPlacement.md`） |
| **F** | 画面の動き（近景通過プロップ・速度線・カメラsway） | ❌ 未着手 |

**つまり「舞台装置は建ったが、大道具が1つも置かれていない」状態。**

### 決定的な数字

`Resources/Json/Scenes/StagePlay.json` の中身は **オブジェクト3個**：

| # | tag | 名前 | 中身 |
|---|---|---|---|
| 1 | `Terrain` | `NormalMapTest` | **Blender連携の動作確認用60m四方の平板**。ステージアートではない |
| 2 | `EnemyPathSpline` | `EnemyPath_01` | 制御点5個（Y≈881〜927＝高高度） |
| 3 | `CameraPathSpline` | `CameraPath` | 制御点60個。`[0, 900, 0]` → `[-11.4, -60, 3505]` |

**約4,382m を180秒かけて飛ぶのに、通り過ぎる造形物が試験用の平板1枚しかない。**

---

## 2. 現状詳細

### 2.1 ✅ できている（そのまま使える土台）

| 機能 | 実体 |
|---|---|
| セクション駆動の環境切替 | `Game/Scene/StageEnvironment.h` / `.cpp`。`StageSection{name, startSec, blendSec, skyboxPath, skyboxTint, lightDir, lightColor, lightIntensity, fogColor, fogNear, fogFar, fogDensity}` |
| 境界だけで遷移する補間方式 | 境界の前後 `blendSec/2` の窓の中でだけ遷移し、外は素の値を維持。**丸ごと lerp すると常に中間色になって個性が消える**ため |
| Skybox クロスフェード | `Skybox::BlendTo(path, sec)`。セクション切替に流用済み |
| 距離フォグ | `Resources/Shaders/Object3D/Fog.hlsli`。`Object3d.PS` / `Object3dNoEnv.PS` / `Object3dPBR.PS` の3つが include して末尾で `ApplyFog()`。CBは `Object3DManager` 所有（b6 / ルートパラメータ11番） |
| near/far clip | `nearClip 0.5` / `farClip 5000` を StagePlay 側から明示設定済み |
| レールパス生成ツール | `tools/Python/gen_rail_path.py` — 数値を変えて何度でも引き直せる |
| **Blender 双方向リアルタイム同期** | `tools/BlenderPipeline/cg2_blender_addon.py` + `layout_to_scene_json.py`。**配置の往復にエンジンのリビルド不要**。反響ゼロを両方向で検証済み |
| 手続き型の地形マテリアル生成 | `tools/Python/gen_terrain_material.py` — ノイズ→ハイトフィールド→Sobel勾配で法線マップ＋ベースカラーを生成（numpy+PIL、$0） |
| HDRI → Cubemap 変換 | `tools/Python/convert_hdr_to_dds.py` — Poly Haven の HDRI から **$0 で空を追加できる** |
| PBR / 法線マップ / IBL | 全フェーズ実装済み（`rendering_richness_roadmap`） |
| シャドウ（CSM） | 実装済み（`Shadow.hlsli`） |
| プレハブ配置 | シーンJSONに `"type":"Prefab"` |

### 2.2 ❌ できていない

| # | 問題 | 詳細 |
|---|---|---|
| **1** | **造形物がゼロ** | 上記の通りシーンに試験用平板1枚のみ |
| **2** | **`scatter_props.py` が存在しない** | 計画では散布ツールでシーンJSONへ直生成する想定だったが、`tools/Python/` に**未作成**（実在するのは `gen_rail_path.py` / `gen_terrain_material.py` / `convert_hdr_to_dds.py`） |
| **3** | **カメラの向きが一切オーサリングされていない** | `Tuning/StagePlay.json` の `camera.rotKeys` が **空配列**。`RailCameraController.cpp:53-61` の「接線方向を向く保険」で動いている＝**常に進行方向を見ているだけ**。バンク（roll）も見せ場の注視もゼロ |
| **4** | **Cubemap が3枚しかなく、しかも全部「夜」か「雪」** | `Resources/Cubemaps/` = `rogland_clear_night_8k` / `rogland_clear_night_4k` / `passendorf_snow_8k`。**昼空・雲海・渓谷に合う空が1枚も無い** |
| **5** | セクションの空が仮のまま | `CloudLayer` が**雪景色の cubemap** を流用。`LowAltitude` は `skyboxPath` が **空文字**（＝空が切り替わらない） |
| **6** | 地形テクスチャが1種類 | `Resources/Textures/Terrain/` = `RockyDirt_BaseColor.dds` と `_TestRock_BaseColor.dds` のみ |
| **7** | 近景に流れるものが無い | Phase F 未着手。**速度感は「近くを高速で通過する物」でしか出ない** |
| **8** | Primitive とパーティクルにフォグが掛からない | 意図的（演出が霧で薄まらないため）。ただし **Phase C で雲板をビルボード Primitive にすると霧の外に浮く**ので、そこで判断が要る |

### 2.3 現在のセクション設定（実データ）

| # | name | startSec | blendSec | skybox | fogNear / Far / Density |
|---|---|---|---|---|---|
| 0 | `SkyAboveClouds` | 0 | 4 | `rogland_clear_night_8k` | 800 / 5000 / 0.35 |
| 1 | `CloudLayer` | 54 | 6 | `passendorf_snow_8k` ⚠️ | 20 / 220 / 2.0 |
| 2 | `LowAltitude` | 66 | 6 | **（空文字）** ⚠️ | 150 / 2500 / 0.8 |
| 3 | `Canyon` | 120 | 6 | `rogland_clear_night_4k` | 30 / … / … |

`camera.speed = 1/180`、`phase.seekMaxSec = 180`。

**⚠️ `CloudLayer` の blend 窓 51〜57秒 / 63〜69秒 はレールの降下 idx17(51s)→idx23(69s) と一致させてある。** ズラすと「雲に入った瞬間に白く濁る」が崩れる。

---

## 3. レール構造の制約（作業前に絶対に理解すること）

`SplineCurveActor::Sample` は**弧長ではなく制御点インデックスで等分**している（`SplineCurveActor.cpp:55`）。ここから3つの規則が出る。

1. **制御点の「個数」＝時間配分**（1セクション1分なら物理長に関係なく等しい点数を置く）
2. **制御点の「間隔」＝速度**（間隔が広いほど速い）
3. **制御点を後から挿入/削除すると以降の t が全部ずれる** → `camera.rotKeys[].t` と Wave の `trigger_sec` が破綻する

### したがって

> **レールの制御点は60個で凍結済み（3秒/点 × 60 = 180秒）。以後は点を「動かすだけ」で、増減させない。**

実測値（`gen_rail_path.py` 出力）:
- 総延長 **約4,382m** / 平均 **24.8 m/s**
- 区間別: Sky 29.1 / Low 26.8 / Canyon 18.0 m/s
- **最速は idx19（57秒）の 70.6 m/s ＝ 雲抜け急降下**（意図的）

---

## 4. Phase C：地形とプロップを「作らずに増やす」

### 4.1 方針

**Blender で作る原型は1種類につき1個だけ。物量は散布ツールで出す。**

理由: 4,382m 分を手で置くのは非現実的。かつ手で置くと配置の調整が Blender 往復になって遅い。

### 4.2 `scatter_props.py`（新規作成）

| 項目 | 内容 |
|---|---|
| 置き場所 | `tools/Python/scatter_props.py` |
| 入力 | レールの制御点列（`Resources/Json/Scenes/StagePlay.json` の `CameraPath`）＋散布ルール |
| 出力 | 同 `StagePlay.json` の `objects[]` へ `"type":"Prefab"` または Object3D エントリを直生成 |
| 冪等性 | **生成物に印を付けて、再実行時に前回分だけを消して置き直す**（手置きオブジェクトを巻き込まない） |
| バックアップ | `gen_rail_path.py` と同じく **既存 `.bak` は温存する**（2回目の実行で加工済みがバックアップに化けるのを防ぐ） |

**散布ルールの案**（レール沿いの距離 s をパラメータに）:
- レール中心からの左右オフセット範囲（セクションごとに変える）
- 高さの決め方（地面 Y=0 / 谷底 Y=-150 基準）
- 密度（個/100m）、スケール範囲、Y軸ランダム回転
- seed 固定で**再現可能**にする

### 4.3 作る原型（Blender、最小限）

| セクション | 必要な原型 | 個数 |
|---|---|---|
| SkyAboveClouds | 浮遊岩、雲板 | 2 |
| CloudLayer | 雲板（濃い） | 1（流用可） |
| LowAltitude | 地上の起伏、木/塔などのランドマーク | 2〜3 |
| Canyon | 谷の壁（手続き生成）、岩柱 | 2 |

**合計 6〜8 原型。** これ以上増やさない。バリエーションはスケール・回転・マテリアルで出す。

谷の壁は `gen_terrain_mesh.py` 的な**手続き生成**が向く（Blenderで長大な壁を作るのは重い）。

---

## 5. Phase D：セクションのアート仕上げ

**`SkyAboveClouds` から通し切って完成度の基準を作る。** 4つ並行で進めると全部が中途半端になる。

### 5.1 まず空を調達する（最優先・$0）

Poly Haven の HDRI → `tools/Python/convert_hdr_to_dds.py` で cubemap 化。

| セクション | 欲しい空 |
|---|---|
| SkyAboveClouds | 高高度の澄んだ空（昼 or 夕） |
| CloudLayer | 雲の中（濃霧に近い。空自体は見えなくてよい） |
| LowAltitude | 地平線のある空 ← **現在 `skyboxPath` が空なので必須** |
| Canyon | 谷底から見上げる狭い空 |

⚠️ **DirectStorage のステージングバッファは 8K cubemap 対応済み**（256MB に拡大済み）なので 8K を使ってよい。

### 5.2 セクションごとの狙い

| セクション | 見せたいもの | 手段 |
|---|---|---|
| SkyAboveClouds | 高度感・開放感 | 遠景の雲海板、まばらな浮遊岩。フォグは遠め（near 800） |
| CloudLayer | **通過の緊張**（高高度と低空の中間ではない） | 濃い白フォグ（density 2.0・far 220）で視界を潰す。ここだけ物を減らして「何も見えない」を作る |
| LowAltitude | 速度感 | **近景を高速で通過する物**を最も密に置く。地面が見えることで速度が知覚される |
| Canyon | 圧迫感・終盤感 | 左右の壁を近づける。fog を暗く、lightIntensity を落とす（現 0.5） |

---

## 6. Phase F：画面の動き

**プロップを置いても、カメラが正面を向いたまま等速で進むだけでは「動いている」感じは出ない。**

| 施策 | 実装コスト | 備考 |
|---|---|---|
| **近景通過プロップ** | 低 | Wave の `Static` + `positions[]` で新規コード不要。→ `3_EnemyPlacement.md` と共用 |
| **カメラ回転キーのオーサリング** | 中 | `rotKeys` が空なので白紙から作れる。降下でピッチを下げる、S字でバンク（roll）を入れる。**これが速度感に一番効く** |
| UVスクロール | 低 | Primitive に実装済み（TimeGroup連動） |
| 速度線パーティクル | 低 | GPUパーティクルで。`timeGroup` を Effect にすれば必殺技中も止まらない |
| カメラ idle sway | 低 | 微小な揺れ。止まって見えるのを防ぐ |

---

## 7. 落とし穴（実装前に必読）

### 7-1. レールの制御点を増減させない
§3の通り。`rotKeys[].t` と Wave の `trigger_sec` が全部ずれる。**Phase E（敵の置き直し）に入ったら特に厳禁。**

### 7-2. Blender の回転順序は2種類ある
`MakeRotateMatrix`(Vector3) = Rz·Ry·Rx だが、**オブジェクト配置に使われるのは `MakeAffineMatrix` = Rx·Ry·Rz**（行ベクトル規約）。`Object3DInstance::Update` が使うのは後者。**ズレは黙って発生する。**

### 7-3. アセットは必ず原点で書き出す
`cook_assets.py` はノードのワールド変換を頂点へ焼き込む。配置はシーンJSON側に持たせる。これを外すと二重変換。

### 7-4. 同一glTF内の複数メッシュは1つに統合される
同じ物を複数置くならエンプティ（`engine_model_dir` / `engine_model_file`）で置く。メッシュを複製して並べると個別編集も使い回しも不可能になる。

### 7-5. 非ASCIIパスでエンジンが落ちる
Blender日本語UIの既定名（立方体/平面）をそのままパスにすると開けず0バイト → `assert(sizeInBytes > 0)` @`DirectXCore.cpp`。アドオンで ASCII 正規化済みだが、**`cook_assets.py` の `_safe_name` は Python の `isalnum()` が Unicode 対応のため日本語を素通しする＝流用不可。**

### 7-6. 法線マップの sRGB/線形はファイル名の部分一致だけで決まる
`LINEAR_TEXTURE_HINTS = ("MaskTexture", "NormalMap")`。マテリアルJSONは見ていない。**ファイル名/フォルダ名に "NormalMap" が無いと sRGB 圧縮されて法線が壊れる。**

### 7-7. PSO は submesh[0] のマテリアルだけで選ばれる
`Object3DInstance.cpp:73-88`（`GetMaterialPointer()` が `submeshes_[0]` 固定）。マルチマテリアルの地形は**全マテリアルに法線マップを割り当てる**こと。でないと先頭次第で全体がPBRにならない。

### 7-8. Blender の Export はシーンJSONを全置換する
Blenderに無いものは消える。**必ず Import → 編集 → Export。** `.bak` を必ず残す実装済み。

### 7-9. Primitive とパーティクルにはフォグが掛からない
別ルートシグネチャでカメラCBもライトCBも持たない（b0 material のみ）。演出が霧で薄まらないので**通常は正しい**が、**雲板をビルボード Primitive にすると霧の外に浮く**。
→ Phase C で「雲板だけ Object3D にする」か「Primitive にもフォグを足す」かを判断すること。

### 7-10. 平行光源の既定 intensity は 0
`LightManager.cpp:26`。ただしセクションが平行光源を駆動するので StagePlay では点く。**DemoScene 等では 0 のままなので法線マップの凹凸が見えない。**

### 7-11. `RailStagePart::SaveToJson` は `root["camera"]` を丸ごと差し替える
`nearClip` / `farClip` は必ずその後に書くこと。

### 7-12. `RebindCameraPath()` を忘れない
`dynamicSplines_` はシーンのロードで作り直される。`RailStagePart::RebindCameraPath()` を `OnAfterSceneLoad()` で必ず呼ぶ（忘れると解放済みを指す。**Auto Reload で毎回踏む**）。

---

## 8. 実装ステップ

### ⚠️ 谷の地形は3タスクの共通ボトルネック

```
2_StageArt Phase C（谷の壁・岩柱の原型生成）
        │  これが無いと物理的に置き場所が存在しない
        ├──> 3_EnemyPlacement: Canyon への固定砲台設置
        └──> 4_StageGimmick:   破壊可能障害物（岩柱）の設置
```

ステージ後半60秒（120–180秒）の中身が**全部ここに乗っている**。
`3_EnemyPlacement.md` で「Canyon が無人」としている件の真因もこれ（配置作業をサボっているのではなく、**置く場所が無い**）。

→ 当初 Step 7 に置いていた「谷の壁の生成」を **Step 4 へ繰り上げた。**

```
Step 1  空（Cubemap）の調達         Poly Haven HDRI → convert_hdr_to_dds.py で4枚
        └ LowAltitude の skyboxPath が空文字なので最優先
Step 2  NormalMapTest を退避         試験用平板をステージから外す（削除はしない）
Step 3  原型モデルの作成             Blender で 6〜8 個。原点で書き出し
Step 4  谷の壁と岩柱を手続き生成       ★繰り上げ。3_EnemyPlacement / 4_StageGimmick の前提
Step 5  scatter_props.py の作成      レール沿い散布。seed固定・冪等・.bak温存
Step 6  SkyAboveClouds を通しで仕上げ  ここで完成度の基準を作る
Step 7  残り3セクションへ展開
Step 8  カメラ回転キーのオーサリング   rotKeys は空＝白紙から作れる
Step 9  Phase F（速度線・sway・近景通過プロップ）
```

**Step 1 と Step 3 は独立なので並行可。** Step 8 は Step 6 以降でないと「何を見せるか」が決まらない。

**Step 4 が終わった時点で `3_EnemyPlacement.md` と `4_StageGimmick.md` のブロックが外れる**ので、そこから並行に進められる。

---

## 9. 関連ファイル索引

| 役割 | パス |
|---|---|
| セクション環境 | `DirectXGame/Game/Scene/StageEnvironment.h` / `.cpp` |
| レール本体 | `DirectXGame/Game/Scene/RailStagePart.cpp`（`:472-542` rotKeys IO、`:141` ソート） |
| カメラ向きの評価 | `DirectXGame/Game/Spline/RailCameraController.cpp:47-90`（`:53-61` が rotKeys 空のときの接線フォールバック） |
| スプライン評価 | `DirectXGame/Game/Spline/SplineCurveActor.cpp:55`（**弧長でなくインデックス等分**） |
| フォグシェーダ | `Resources/Shaders/Object3D/Fog.hlsli` |
| フォグCB | `Object3DManager`（`SetFogParams` / `DisableFog` / `BindFog`） |
| シーン（配置） | `Resources/Json/Scenes/StagePlay.json` |
| チューニング（セクション） | `Resources/Json/Tuning/StagePlay.json` の `sections` / `camera` |
| レール生成 | `tools/Python/gen_rail_path.py` |
| 地形マテリアル生成 | `tools/Python/gen_terrain_material.py` |
| HDRI→Cubemap | `tools/Python/convert_hdr_to_dds.py` |
| アセットクック | `tools/Python/cook_assets.py` |
| 散布ツール（新規予定） | `tools/Python/scatter_props.py` |
| Blenderアドオン | `tools/BlenderPipeline/cg2_blender_addon.py` |
| レイアウト変換 | `tools/BlenderPipeline/layout_to_scene_json.py` |
| Blender作業ファイル | `Assets/Scenes/StagePlay_layout.gltf` / `.bin` |
| Cubemap | `Resources/Cubemaps/`（現在3枚） |
| 地形テクスチャ | `Resources/Textures/Terrain/`（現在2枚） |
| 試験用平板 | `Resources/Models/NormalMapTest/` |
