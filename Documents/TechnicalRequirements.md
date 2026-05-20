# 技術要件書

## 1. 開発環境

| 項目 | 内容 |
|---|---|
| **言語** | C++17 / HLSL (Shader Model 6.0) |
| **API** | DirectX 12 |
| **IDE** | Visual Studio 2022 (`v143`) |
| **ソリューション** | `Project/CG2_0_1.sln` |
| **構成** | Debug / Development / Release × x64 |
| **外部依存** | DirectXTex / DXC / ImGui (docking版) / assimp / quirc / DirectStorage 1.4 |

## 2. 開発ルール（厳守）

### 2.1 ファイル構成
- `DirectXGame/GameEngine/` — エンジン汎用コード（ゲーム固有を入れない）
  - `Core/` — DX12基盤、ウィンドウ、入力、リソース管理
  - `Graphics/` — Sprite / Object3D / Particle / Skybox / Light / Camera / Primitive 等
  - `Math/` — Vector・Matrix・Quaternion
  - `Sound/` `Utility/` — 汎用
- `DirectXGame/Game/` — ゲーム固有
  - `Actors/` `Scene/` `Components/` `Config/`
- `DirectXGame/ImGUIManager/` — ImGui統合
- 新規ファイル作成時は **必ず事前相談**、作成後に `vcxproj` / `vcxproj.filters` に登録

### 2.2 コーディング規約
- クラス名 PascalCase / メンバ変数末尾 `_`
- DirectXリソースは `Microsoft::WRL::ComPtr`
- 所有権 `std::unique_ptr`、参照のみ生ポインタ
- `new`/`delete` 直接使用禁止
- ヘッダで重い include を避け前方宣言を優先
- コメントは日本語、自明なコメントは書かない

### 2.3 不変ルール（絶対遵守）
- **DemoScene は絶対削除しない**（技術検証用、新機能を試す足場）
- **JSON は自作のみ**（nlohmann/json 等の外部ライブラリ追加禁止）
- マネージャ系は Initialize / Update / Draw / Finalize の流れを揃える
- インスタンスを1つだけ作る必要のあるものはシングルトン

## 3. エンジン全体アーキテクチャ

```
WinMain → Game (Framework派生) → SceneManager → BaseScene
                            ├ DirectXCore  (D3D12デバイス / コマンドリスト)
                            ├ InputManager (Keyboard / Mouse / Controller + InputActionMap)
                            ├ SpriteManager / Object3DManager / SkyboxManager
                            ├ SRVManager
                            ├ PostEffect (マルチパス)
                            ├ ImGuiManager (Debug only)
                            └ DStorageManager
```

## 4. 実装済みエンジン機能

### 4.1 自作JSONライブラリ
| ファイル | 役割 |
|---|---|
| `GameEngine/Utility/Json/JsonValue.h/.cpp` | 値型（Null/Bool/Int/Double/String/Array/Object）+ アクセサ |
| `JsonParser.h/.cpp` | パーサ。`//` と `/* */` コメント許容、行/列エラー報告 |
| `JsonWriter.h/.cpp` | シリアライザ。pretty/compact |

### 4.2 入力アクション層
| ファイル | 役割 |
|---|---|
| `GameEngine/Core/Input/InputAction.h/.cpp` | `InputActionMap`：2スロット制バインド、Pressed/Triggered/Released、LT/RT対応 |
| `PhysicalBinding.h/.cpp` | `kb.Space` / `gp.RT` 文字列⇔Enum変換 |
| `Game/Config/KeyConfig.h/.cpp` | user→default→組み込みデフォルトの優先ロード |
| `Game/Config/GameActions.h` | `enum class Action` 15個 |

### 4.3 シーン管理
| ファイル | 内容 |
|---|---|
| `BaseScene.h/.cpp` | 仮想 Initialize/Update/Draw/Finalize、Seek(t)、elapsedSeconds_、SaveSceneToJson/LoadSceneFromJson |
| `SceneFactory.cpp` | TITLE / HUB / STAGEPLAY / RESULT / DEMO の5シーン分岐 |
| `SceneManager.cpp` | Update順序：currentScene->Update → TickElapsed → CollisionManager::Update → SceneServices |

シーン構成: Title / Hub / StagePlay / Result（雛形）+ **DemoScene（削除禁止）**

### 4.4 TimeGroup（多重時間スケール）
| ファイル | 役割 |
|---|---|
| `GameEngine/Core/TimeGroup.h` | `World / Player / UI` の3グループ enum |
| `BaseScene::timeScales_[3]` | グループ別倍率（dxCore グローバル × グループ倍率） |
| `GetScaledDeltaTime(TimeGroup g)` | グループ別 dt 取得 |

**用途例**:
- HitStop: World=0.05 / Player=0.05 / UI=1.0
- Just Dodge: World=0.3 / Player=1.0 / UI=1.0
- BotW Rush: World=0.2 / Player=1.5 / UI=1.0
- ディスラプター全停止: World=0.0 / Player=0.0 / UI=1.0

### 4.5 タグシステム
`Game/Components/EntityTag.h`:
- 12種類 + スプライン4種（PlayerRail/EnemyPath/FloatingPath/CameraPath）= **計16タグ**
- `GetTagName/TagFromName/GetTagColor` ヘルパ

### 4.6 IImGuiEditable（編集可能インタフェース）
- `tag_`（EntityTag）
- `visibleInEditor_`（Debug描画ON/OFF、Releaseでは効かない）
- `collider_`（SphereCollider）
- 自動 Register/Unregister（ImGuiManager + CollisionManager の両方）
- `NoAutoRegister` 経由のコンストラクタもあり（EffectEditor等の独立 Hierarchy 用）

### 4.7 ImGuiエディタウィンドウ群
| ウィンドウ | 役割 |
|---|---|
| **Viewport** | メイン3Dシーン描画。ドロップでY=0平面に0.5刻みスナップ配置 |
| **Hierarchy** | タグごとCollapsingHeaderで分類、目玉トグル、検索フィルタ |
| **Inspector** | Name編集/Tagコンボ/Visible/Collider/Geometry/UV/Material/Texture/Prefab保存 |
| **Scene Editor** | モデル/テクスチャ/プリミティブ/スプライン/プリファブのDrag Source、シーンSave/Load |
| **Scene Timeline** | 経過秒シークバー（Seek対応）、現在シーン名表示 |
| **TimeControl** | グループ別TimeScaleスライダー、プリセット（HitStop/Just Dodge/BotW Rush） |
| **Collision** | コライダー描画グローバルON/OFF |
| **Log** | LogBuffer出力 |
| **FPS** | 履歴グラフ |
| **その他** | Camera / Light / PostEffect / Particle / Transition / WebCam / QR / Effect Editor |

### 4.8 DebugDraw（線描画API）
`GameEngine/Graphics/Primitive/DebugDraw.h/.cpp` — `LineRenderer` のラッパ：
- `Sphere(center, radius, color, segments)`
- `AABB(min, max, color)` / `OBB(center, axes, halfSize, color)`
- `Cross(pos, size, color)` / `Ray(origin, dir, length, color)`
- `Grid(center, size, step, color)`
- `CatmullRomSpline(controlPoints, color, samples)`

### 4.9 Primitive（拡張済み）
| プリミティブ | 仕様 |
|---|---|
| Plane / Box / Sphere | 標準 |
| **Ring** | `RingParams`：内外半径、divisions、内外カラー、startAngle/endAngle、fadeStart/End、UV、外周半径配列 |
| **Cylinder** | `CylinderParams`：上下別半径、上下別カラー、startAngle/endAngle、divisions |

**全プリミティブ共通**:
- `BlendMode`（None/Normal/Add/Subtract/Multiply/Screen）
- `DepthWrite` ON/OFF
- **背面カリング** ON/OFF（個別、PSO は [Blend][DepthWrite][Cull] 3次元 = 24 PSO）
- **Discard 閾値**（alphaReference）
- **サンプラーモード** 3種類（0=WrapAll / 1=WrapU+ClampV / 2=ClampAll）。Ring/CylinderはデフォルトWrapU+ClampV
- **UVスクロール / UV手動オフセット / UVスケール / Flip U/V**
- **TimeGroup 連動**：UVスクロールがシーンのTimeGroup倍率で進む
- Inspector で Ring/Cylinder のパラメータ変更時は **1フレーム遅延** で再生成（D3D12 ERROR #921 回避）

### 4.10 コリジョン
| 要素 | 役割 |
|---|---|
| `SphereCollider`（struct） | enabled / radius / offset / showDebug / onCollision コールバック / isCollidingThisFrame |
| `CollisionMatrix` | IsCollidableTag, ShouldCollide（タグペア許可マトリクス）。Player↔PlayerAttack等を早期return |
| `CollisionManager`（シングルトン） | Register/Unregister/Update（O(N²)）、グローバル描画ON/OFF、衝突時は赤色描画 |

### 4.11 スプライン
`Game/Actors/SplineCurveActor.h/.cpp`:
- **Catmull-Rom**（制御点を通る）
- 制御点を**内部所有** → 異なるスプライン同士で混在しない
- 4種のタグで役割識別（PlayerRail / EnemyPath / FloatingPath / CameraPath）
- 選択中の制御点を `GetEditableTranslate()` で返してギズモ操作可
- Inspector で Add/Clear/個別XYZドラッグ/削除

### 4.12 プリファブ
| ファイル | 内容 |
|---|---|
| `Game/Components/Prefab.h` | PrefabDef：name / modelDir / modelFile / tag / defaultScale/Rotate / hasCollider / colliderRadius / colliderOffset |
| `PrefabManager.h/.cpp` | `Resources/Json/Prefabs/*.json` をスキャン、Save、Find |

- Inspector の **Save as Prefab** ボタン（Object3Dのみ）
- SceneEditor の **Prefabs** セクション（drag source）
- ドロップで Y=0 配置、タグ/コライダー込みで複製

### 4.13 ポストエフェクト
- マルチパス、ピンポンRenderTexture
- Grayscale / Gaussian / Smoothing / Vignette / RadialBlur / Sepia / Dissolve / OutlineDepth / OutlineNormal
- メインViewportクリアカラー：`(0.1, 0.25, 0.5, 1.0)` 水色

## 5. アセット形式

### 5.1 ファイル形式 v2
- `.mesh` / `.skel` / `.mat` / `.anim` の4分割
- submesh 対応
- PBR は `.mat` v2 で将来拡張

### 5.2 DirectStorage パイプライン計画
BC7 + GDeflate + DirectStorage 1.4 で **VRAM節約 & 極限ロード**
- Phase 0: 地ならし
- Phase A: Cooker
- Phase B: Packer
- Phase C: Engine統合

Zstd は GPU 対応外なので採用しない。

### 5.3 リソース配置
```
Resources/
  ├ Models/<name>/        (.mesh, .skel, .mat, .anim, テクスチャ)
  ├ Textures/             (.dds)
  ├ Sounds/
  ├ Shaders/
  ├ Cubemaps/
  └ Json/
    ├ Setting/             keyconfig.default.json / keyconfig.user.json
    ├ Scenes/              demo.json 等
    ├ Prefabs/             <name>.json
    └ Effects/             <name>.json (未実装)
```

## 6. 未実装エンジン機能（実装順）

### 段階2: Helix（次に着手）
- チューブ型螺旋プリミティブ（蓋なし）
- パラメータ:
  - `startHelixRadius / endHelixRadius`（螺旋カーブの半径、入口/出口で別）
  - `startTubeRadius / endTubeRadius`（チューブ太さ、入口/出口で別）
  - `pitch`（1巻あたりの長さ）/ `turns`（巻数）
  - `circleSegments`（チューブ円周）/ `lengthSegments`（螺旋長さ方向）
  - `startColor / endColor`（先端と末尾のグラデ）
- UVスクロールは U軸（螺旋長さ方向）デフォルト推奨
- **ユースケース**: プレイヤー弾の弾道（中心の白光+赤橙の螺旋）

### 段階3: ビルボード
- **2モード**: Full（完全カメラ向き）/ YAxis（縦軸維持）
- 対象: Primitive + GPU Particle
- enum: `BillboardMode::None / Full / YAxis`
- Inspector で選択
- GPU Particle は TimeGroup 連動も同時に実装

### 段階4: Effect Editor
- コンセプト: シーンエディタの要領で「プリミティブ + GPUパーティクル + 光源」を組み合わせて1エフェクト
- **コンポーネント3種**:
  - PrimitiveComponent: meshType, offset, startTime, lifetime, startScale→endScale, startColor→endColor, texture, BlendMode, billboard
  - ParticleComponent (GPU): groupName, offset, startTime, duration, burstCount, billboard
  - LightComponent: PointLight + SpotLight, color, intensity, range, startIntensity→endIntensity
- **時間制御**: 絶対オフセット + 寿命のみ（カーブ後回し）
- **専用ビューポート**: メインとは別RenderTexture + 新規ImGuiウィンドウ "Effect Preview"
- **ランタイム**: `EffectManager` シングルトン（LoadDefs / Play(name, pos) / Update / activeInstances[]）
- 配置: `Resources/Json/Effects/<name>.json`

## 7. アニメーション関連（保留）

以下が揃うまで Animated プリファブ対応は保留：
- `.anim` ロード機構
- クリップ間補間（クロスフェード）
- ランタイムクリップ切替

## 8. 各種制約

- **ウィンドウリサイズ**: 未対応（後回し）
- **マルチプラットフォーム**: Windowsのみ
- **マルチプレイ**: 想定なし
- **オンライン**: 想定なし

## 9. ポートフォリオ用デバッグ機能

- **Scene Timeline シークバー**：任意の経過時刻にジャンプ（タイトル放置→デモ再生のデバッグ高速化、敵配置調整、ボス戦から開始 等）
  - 各シーンに `virtual void Seek(float t)` を持たせ、決定論的再構築する設計が前提

## 10. CLAUDE.md 既存ルール

- `Framework`（汎用）→ `Game`（固有）の継承構造を維持
- マネージャは Initialize/Update/Draw/Finalize を揃える
- パフォーマンス向上に気付いたら**提案**（勝手に大規模書き換えしない）
- Claude モデル戦略：計画フェーズ=Haiku / 実装フェーズ=Opus
