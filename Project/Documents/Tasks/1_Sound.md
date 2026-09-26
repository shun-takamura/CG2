# 1. サウンド実装

> **優先度 S（最優先）** — 無音であることが現状プロジェクト最大の欠陥。
> エンジン基盤はほぼ揃っており、**不足しているのは配線・音量制御・コンテンツ**。
> 費用対効果がプロジェクト中で最も高い。

最終更新: 2026-09-16

---

## 0. このドキュメントの位置づけ

STG完成に向けた課題を優先度順に分割したタスク文書の1本目。
数字プレフィックスが優先度を表す（`1_Sound.md` → `2_StageArt.md` → …）。

**着手前にこのファイルを最初から読むこと。** 特に §8「落とし穴」は実装中に必ず踏む。

---

## 1. 現状サマリ

| 区分 | 状態 |
|---|---|
| エンジン基盤 | XAudio2 + X3DAudio + Media Foundation。**想像より良い** |
| 毎フレーム更新 | ❌ **Framework に繋がっていない**（最優先で修正） |
| 音量制御 | ❌ **APIが1本も無い** |
| 3Dループ | ❌ 無い（2Dループは有る） |
| 音源 | ❌ `fanfare.wav` **1個のみ** |
| エフェクトへの配線 | ❌ Effect JSON 11個すべて `sounds: []`（0/11） |

---

## 2. エンジン現状（詳細）

### 2.1 ✅ できている（そのまま使える）

| 機能 | 実体 |
|---|---|
| XAudio2 / X3DAudio 初期化・終了 | `Framework.cpp:383` / `:572` |
| **Media Foundation デコード** | `SoundManager.cpp:89` — **wav / mp3 / aac をそのまま読める**（事前変換不要） |
| 2D再生 | `Play2DSound` (`SoundManager.cpp:154`) |
| **2Dループ再生** | `Play2DSoundLooped` (`:178`)、`XAUDIO2_LOOP_INFINITE` (`:198`) — チーム制作時に無かったものが統合済み |
| 2D停止 | `Stop2DSound` (`:206`) |
| 3D再生（定位・距離減衰・LPF） | `Play3DSound` (`:216`)、`XAUDIO2_VOICE_USEFILTER` (`:230`) でLPF有効 |
| 3D停止 | `Stop3DSound` (`:283`) |
| 移動音源の追従 | `UpdateEmitter` (`:293`) |
| リスナー（カメラ）反映 | `UpdateListener` (`:302`) |
| 終了ボイスの自動回収 | `Update` (`:68-87`)、`BuffersQueued == 0` 検出 (`:75`) |
| **エフェクトへの統合** | `EffectSoundComponent`（`EffectDef.h:311-329`）= `soundName` / `offset` / `startTime` / `distanceScale` / `volume`。`EffectInstance` が startTime で `Play3DSound` (`EffectInstance.cpp:548`)、Stop/Reset/ホットリロード時に `Stop3DSound` (`:612-618`) まで**完備** |

### 2.2 ❌ できていない（要実装）

| # | 問題 | 詳細 | 対応Step |
|---|---|---|---|
| **1** | **`Update()` / `UpdateListener()` が毎フレーム呼ばれていない** | 呼び出しは `DemoScene.cpp:655-657` **だけ**。`Framework` のループに無い。→ ①リスナーが原点固定で**定位が全部壊れる** ②終了ボイスが回収されず `emitters3D_` が**無限に増えてリーク** | Step 0 |
| **2** | 音量APIが存在しない | `masterVoice_` はある（`SoundManager.h:83`）が `SetVolume` ラッパー無し。SubmixVoice（バス）は**存在すらしない** | Step 1 |
| **3** | `EffectSoundComponent::volume` が無効 | JSONにフィールドはあるが `Play3DSound` に volume 引数が無く**渡されていない**（`EffectInstance.cpp:548`）。設定しても無反応 | Step 2 |
| **4** | **3Dループが無い** | `Play3DSound` は `buf.Flags = XAUDIO2_END_OF_STREAM` のみで `LoopCount` 未設定（`SoundManager.cpp:236`） | Step 3 |
| **5** | BGMのフェード/クロスフェードが無い | シーン遷移・セクション切替がブツ切りになる | Step 3 |
| **6** | **同じ2D音を重ねられない** | `Play2DSound` が冒頭で `Stop2DSound(name)` を呼び (`:159`)、`sourceVoices2D_[name]` で名前ごとに1本管理 (`:175`)。→ **同じSEの連打が自分を切る** | Step 4 |
| **7** | ピッチ変更・ランダム化が無い | 射撃連射・同種の撃破音が機械的に同じ音で鳴る | Step 5 |
| **8** | `LoadFile` が `assert` で落ちる | `SoundManager.cpp:98, 106` 等。Release では assert が消えて**未定義動作**。配布リスク | Step 4 |
| **9** | ロードの一元管理が無い | `DemoScene.cpp:255` にベタ書き1行のみ | Step 4 |

### 2.3 コンテンツ現状

- 音源: `Resources/Sounds/fanfare.wav` **1個のみ**
- Effect JSON 11個すべて `sounds: []`
- 配布: `Resources/Sounds` は FS直読み（→ `release_packaging` 参照）。**ZIP同梱が必要**

---

## 3. 確定した設計判断

| 論点 | 決定 |
|---|---|
| **バス構成** | **Master / BGM / SE の3本**で確定 |
| **鳴らし方の分類** | **`PlayBGM` / `PlaySE2D` / `PlaySE3D` の3系統**で統一 |
| **SEの経路** | エフェクトを持つもの → **エフェクトJSONの `sounds` に埋める**（Play3DSE）<br>UI等 → **Play2DSE + カタログの論理名**で呼ぶ |
| **音だけのエフェクトJSON** | **採用**。見た目を持たないワールド音（回避・ジャスト回避成立 等）はこれで作る |
| **エディタの一覧整理** | **カテゴリ分け + フィルタを即実装**（§6）。フォルダ分割は**不採用** |
| **サウンドカタログ** | **採用**。ホットリロード**あり** |
| **ロードタイミング** | **全部起動時**（困るまでこれでいく） |
| **BGM切替** | STGの**セクションごとに変える**。フェード/クロスフェード必須 |
| **ポーズ時の挙動** | **音量を落として再生継続**（フリーズと区別するため）。→ Pause/Resume API は**不要** |
| **素材調達** | フリー素材 + AI生成 |

---

## 4. API 仕様

```
── BGM ──────────────────────────────
PlayBGM(name, fadeInSec = 0)
StopBGM(fadeOutSec = 0)
CrossfadeBGM(name, sec)

── 2D SE ────────────────────────────
PlaySE2D(name)                              // 撃ちっぱなし・重ね掛け可
PlaySE2DLooped(name)              -> handle

── 3D SE ────────────────────────────
PlaySE3D(name, pos, vel, distScale)       -> handle
PlaySE3DLooped(name, pos, vel, distScale) -> handle
UpdateEmitter(handle, pos, vel)
Stop(handle)

── バス ─────────────────────────────
SetBusVolume(Bus::Master | Bus::BGM | Bus::SE, float v)
GetBusVolume(Bus)
```

### バス構造

```
SourceVoice(射撃音)  ┐
SourceVoice(爆発音)  ┼→ SubmixVoice[SE]  ┐
SourceVoice(UI音)    ┘                    ├→ MasteringVoice[Master] → スピーカー
SourceVoice(BGM)     ──→ SubmixVoice[BGM] ┘
```

**ポーズはこれで1行**：`SetBusVolume(Bus::Master, 0.3f)` → 復帰時に `1.0f`。
必殺技中のダッキングも同じ仕組みで出せる。

### 既存APIとの関係

- `Play2DSound` / `Play2DSoundLooped` / `Play3DSound` / `Stop2DSound` / `Stop3DSound` / `UpdateEmitter` / `UpdateListener` / `Update` は**内部実装として残す**
- 上記の新APIを**外向きの窓口**にする
- ⚠️ `PlaySE2D` は **`Play2DSound` をそのまま呼んではいけない**（名前キーで自分を切るため）。§8-4 参照

---

## 5. サウンドカタログ

### 5.1 目的

「論理名 → 実ファイル + 既定パラメータ」の対応表をJSONで持つ。
コードもエフェクトJSONも**論理名だけを知っていればよくなる**。

得られるもの:

| 効果 | 具体的に |
|---|---|
| **再ビルド不要の差し替え** | フリー素材を試し比べるとき `file` を書き換えるだけ |
| **ミックス調整がコード外** | 全体の音量バランスをJSONだけで整えられる |
| **ピッチrandomize** | 射撃連射が機械的にならない |
| **バス割り当ての一元化** | 呼び出し側が BGM/SE を意識しなくてよい |
| **欠損検出** | ファイルが無ければ起動時に警告 → 配布ZIPの入れ忘れ防止 |
| **論理名の唯一の台帳** | 「どこで何を鳴らしているか」がここを見れば分かる |

### 5.2 スキーマ

`Resources/Json/Sounds/catalog.json`

```json
{
  "sounds": [
    {
      "name":        "player_shot",
      "file":        "Resources/Sounds/SE/shot_01.wav",
      "bus":         "SE",
      "volume":      0.6,
      "pitchRandom": 0.08,
      "loop":        false
    },
    {
      "name":   "bgm_stage_sky",
      "file":   "Resources/Sounds/BGM/sky.mp3",
      "bus":    "BGM",
      "volume": 0.5,
      "loop":   true
    }
  ]
}
```

| キー | 型 | 既定 | 意味 |
|---|---|---|---|
| `name` | string | 必須 | 論理名。コード / エフェクトJSON から参照するキー |
| `file` | string | 必須 | 実ファイルパス |
| `bus` | string | `"SE"` | `"BGM"` / `"SE"` |
| `volume` | float | `1.0` | 既定音量（呼び出し側で上書き可） |
| `pitchRandom` | float | `0.0` | 再生ごとのピッチ揺らぎ幅（±） |
| `loop` | bool | `false` | 既定ループ（BGM用。SEは呼び出し側の Looped 版で指定） |

### 5.3 ホットリロード

- **エフェクトJSONと同じ方式**で実装する（`SceneEditorWindow::RefreshEffectsIfChanged` が参考実装）
- ⚠️ ただしエフェクト側は**ディレクトリのmtime**で判定している（`SceneEditorWindow.cpp:272`）。カタログは**単一ファイル**なのでファイル自身のmtimeを見ればよく、より単純
- リロード時、**既に鳴っている音は止めない**（音量やピッチの変更は次の再生から反映）

---

## 6. エフェクトエディタのカテゴリ分け

### 6.1 背景

音だけのエフェクトJSONを作るとエディタの一覧が圧迫される。
加えて**現状すでに探しづらい**原因が判明:

```cpp
std::vector<std::string> EffectManager::ListDefNames() const {
    for (const auto& pair : defs_) names.push_back(pair.first);  // unordered_map の順
```

`ListDefNames()` が**ソートすらしていない**（`EffectManager.cpp:122-127`）。
呼び出し元は `EffectEditorWindow.cpp:348` の**1箇所のみ**なので、ソートを入れても副作用なし。

### 6.2 決定した方式

**手動タグではなく、`category` フィールド + 成分からの派生フォールバック。**

```cpp
// category が空のときのフォールバック
bool IsSoundOnly(const EffectDef& d) {
    return !d.sounds.empty()
        && d.primitives.empty() && d.particles.empty() && d.lights.empty();
}
```

**なぜ手動タグ単独ではダメか**: あとからパーティクルを足したときにタグの書き換えを忘れると、
見た目があるのに SoundOnly グループに隠れ続ける。派生なら**自動でグループが移動する**。

### 6.3 並び順

```
Effect(未分類) → Player → Enemy → Boss → Special → UI
              → (未知のカテゴリをABC順) → SoundOnly(常に最後)
```

既知カテゴリを優先順配列で持ち、それ以外は末尾にABC順。SoundOnly だけ常に最下段固定。

### 6.4 実装内容（新規ファイルなし・vcxproj変更なし）

| # | ファイル | 内容 |
|---|---|---|
| 1 | `EffectDef.h` | `EffectDef` に `std::string category;`（既定 `""`）を追加 |
| 2 | `EffectDef.cpp` | 読み: `:386` 付近（`name`/`totalDuration`/`loop` の並び）に追加。キーが無ければ空のまま = **既存11個のJSONは無変更で動く**<br>書き: `:580` 付近。**空文字のときは出力しない**（既存JSONに余計な差分を出さない） |
| 3 | `EffectManager.cpp` | `ListDefNames()` (`:122`) をソート |
| 4 | `EffectEditorWindow.cpp` | ・カテゴリ解決（空なら §6.2 の派生判定）<br>・コンボ上に `InputText` フィルタ（部分一致・大小無視）<br>・`ImGui::SeparatorText(カテゴリ名)` でブロック分け（`:354-363`）<br>・`Name` (`:396`) の隣に `Category` 入力欄を追加し Save 対象に含める |
| 5 | `EffectEditorWindow.cpp` | `##mergeSrc` コンボ (`:377-382`) にも同じグループ化を適用（片方だけ整うと逆に混乱するため） |

### 6.5 対象外（意図的）

- **`SceneEditorWindow` の Effects 一覧パネル**（prefab スロットへ DnD するやつ、`:284-296`）は**やらない**。
  あちらはディレクトリ走査で**ファイル名(`stem`)** から一覧を作っており、`def.name` とは別系統。
  グループ化するには `EffectManager::FindDef` 経由でカテゴリを引き直す作業が別途必要。

- **フォルダ分割は不採用**。検討した結果:
  | 論点 | 結果 |
  |---|---|
  | 参照が壊れないか | ✅ 壊れない。`LoadDef` は JSON本体の `name` をキーにしている（`EffectManager.cpp:63`）。パス由来ではない |
  | ロードされるか | ⚠️ `LoadAllDefsInDirectory` は `directory_iterator` = **非再帰**（`:72`）。要変更 |
  | ホットリロード | ❌ **壊れる。** `RefreshEffectsIfChanged` は `last_write_time(effectsDir)` = **ディレクトリ自身のmtime**で判定（`SceneEditorWindow.cpp:272`）。サブフォルダ内のファイルを書き換えても親のmtimeは変わらず、**保存が一覧に反映されなくなる** |

  得られるのは「エクスプローラーでの見やすさ」だけで、**エディタ上の一覧はフラットのまま = UIの問題を解決しない**。

---

## 7. 音源リスト

### 7.1 BGM（5曲）

| 用途 | 備考 |
|---|---|
| タイトル | ループ |
| ハブ / ステージセレクト | ループ |
| STG道中 | **セクション連動で切替**（§7.4） |
| ボス戦 | ループ。レール→ボスのシームレス遷移に合わせクロスフェード |
| リザルト / ゲームオーバー | ジングル（ループ無し）。既存 `fanfare.wav` を流用可 |

### 7.2 SE — プレイヤー

実装済み機能に1:1対応。

- 通常射撃
- チャージ開始 1段階 / 2段階
- **チャージ維持ループ**（← 3Dループ必須）
- チャージ発射
- 精密モード 切替 in / out
- 近接弱 4段（w1〜w4、段ごとに変えると気持ちいい）
- 近接強
- 近接ヒット（本あて / 持続あてで差をつける）
- 回避
- **ジャスト回避成立**（最重要・専用の「決まった」音）
- 分身出現
- 分身カウンター確定（上/右/下/左 の4方向それぞれ）
- 回復（大 / 小）
- 被弾
- 死亡
- 必殺技ゲージMAX通知

### 7.3 SE — 必殺技（動画の見せ場）

**傲慢サンダー**: 発動 / ロックオン音（体数ぶん重なる）/ 一斉発射 / 着弾
**ディスラプター**: チャージ（ループ）/ 一閃 / 世界断裂 / 破片飛散 / 収束

### 7.4 SE — 敵

| 敵種 | 音 |
|---|---|
| Drone | 射撃 / 被弾 / 撃破爆発 |
| Carrier | 子機スポーン / 被弾 / 撃破（大きめ） |
| Rusher | **溜め（telegraph）** ← 回避の予告として機能するため最重要 / 突進 / 撃破 |
| Boss | 近接攻撃 / 遠距離攻撃 / 接近 / ジャンプ / 被弾 / 撃破 |

### 7.5 SE — UI / システム

カーソル移動 / 決定 / キャンセル / ポーズ開閉 / シーン遷移 / スコア加算 / ステージセクション切替

### 7.6 概算

**BGM 5曲 + SE 約45〜55種**

---

## 8. 落とし穴（実装前に必読）

### 8-1. `Update()` / `UpdateListener()` は Framework に置く

現在 `DemoScene.cpp:655-657` にしか無い。**Step 0 で必ず Framework のループへ移す。**

理由が2つある:
1. リスナーがカメラに追従しないと**3D定位が全部壊れる**
2. `Update()` が回らないと終了ボイスが `DestroyVoice` されず**リークする**（`SoundManager.cpp:68-87`）

さらに **ポーズ中も `Update()` を回し続ける必要がある**。
現在 `StagePlayScene::Update()` は `if (paused_) return;` で即抜けする（`StagePlayScene.cpp:2764`）ため、
**シーン側に置くとポーズ中にボイスが溜まる**。Framework 側に置けばこの問題は自動的に回避される。

### 8-2. 3Dループを足すと自動回収が効かなくなる

`Update()` の回収条件は `BuffersQueued == 0`（`SoundManager.cpp:75`）。
ループ再生では**永久に0にならない**ので、明示的な `Stop` が必須。

- **エフェクト経由なら安全**: `EffectInstance` が Stop/Reset/ホットリロード時に `Stop3DSound` を呼ぶ実装済み（`EffectInstance.cpp:612-618`）
- **コードから直接ループを鳴らす場合は自前でハンドル管理が必要**

### 8-3. 3Dで鳴らすSEはモノラル、BGM と 2D SE はステレオ

`Play3DSound` はステレオ音源だと `pChannelAzimuths` 経由で処理され（`SoundManager.cpp:268-272`）、**定位がぼやける**。
Media Foundation が読み込み自体は吸収してしまうため**気づきにくい罠**。

### 8-4. `PlaySE2D` は名前キーにしてはいけない

既存の `Play2DSound` は冒頭で `Stop2DSound(name)` を呼び（`:159`）、`sourceVoices2D_[name]` で1本管理（`:175`）。
→ **同じ音を重ねられず、UI連打や同時多発SEで自分を切る。**

`PlaySE2D` は**使い捨てボイスプール**として実装し、名前キー管理は **BGM だけ**に残す。

### 8-5. `LoadFile` の assert

`SoundManager.cpp:98, 106` 等が `assert(SUCCEEDED(result))`。
Release では assert が消えて**未定義動作**になる。カタログ導入時に**ログ出力 + スキップ**へ置き換える。

### 8-6. 配布ZIP

`Resources/Sounds` は pack を経由せず **FS直読み**（`release_packaging` 参照）。
音源を増やしたら**配布ZIPへの同梱を忘れない**。

### 8-7. 素材のライセンス

- 魔王魂・効果音ラボ等は**表記必須**のものがある
- AI生成音も商用 / ポートフォリオ利用の規約を確認する
- **`Documents/Readme.md` にライセンス表記欄を用意する**

---

## 9. 実装ステップ

```
Step 0  Framework に Update() / UpdateListener() を接続        ← 最優先。無いと全部壊れる
Step 1  バス実装（Master / BGM / SE の SubmixVoice）+ SetBusVolume / GetBusVolume
Step 2  Play3DSound に volume 引数追加 → EffectSoundComponent.volume を有効化
Step 3  3Dループ追加 + BGM フェード / クロスフェード
Step 4  サウンドカタログ（JSON一括ロード + ホットリロード）
        + PlaySE2D のボイスプール化 + LoadFile の assert 撤去
Step 5  ピッチランダム化
Step 6  エフェクトエディタのカテゴリ分け（§6.4）
Step 7  音源の投入と配線                                        ← 作業量の本体
Step 8  ポーズ時のダッキング + オプション画面の音量スライダ
```

**Step 6 は独立しているので、Step 0〜5 と並行して着手してよい。**
**Step 8 の音量スライダは `5_PauseOption.md`（ポーズ/オプション画面）と合流する。**

---

## 10. 関連ファイル索引

| 役割 | パス |
|---|---|
| サウンド本体 | `DirectXGame/GameEngine/Sound/SoundManager.h` / `.cpp` |
| 毎フレーム更新の接続先 | `DirectXGame/GameEngine/Framework.cpp`（`:383` Init / `:572` Final） |
| 既存の使用例（唯一） | `DirectXGame/Game/Scene/DemoScene.cpp:255, 485-551, 655-657` |
| エフェクトの音成分 | `DirectXGame/GameEngine/Graphics/Effect/EffectDef.h:311-329, 345` |
| エフェクトの音再生 | `DirectXGame/GameEngine/Graphics/Effect/EffectInstance.cpp:538-554, 612-618` |
| エフェクト定義IO | `DirectXGame/GameEngine/Graphics/Effect/EffectDef.cpp:386-390, 580-582` |
| エフェクト管理 | `DirectXGame/GameEngine/Graphics/Effect/EffectManager.cpp:63, 67-79, 122-127` |
| エフェクトエディタ | `DirectXGame/GameEngine/Graphics/Effect/EffectEditorWindow.cpp:348, 354-363, 377-382, 396` |
| エフェクト一覧ホットリロード | `DirectXGame/ImGUIManager/SceneEditorWindow.cpp:261-297` |
| 起動時のエフェクトロード | `DirectXGame/Game/Game.cpp:179` |
| ポーズ処理 | `DirectXGame/Game/Scene/StagePlayScene.cpp:2761-2766` |
| ステージセクション（BGM連動先） | `DirectXGame/Game/Scene/StageEnvironment.h` / `.cpp` |
| 音源置き場 | `Resources/Sounds/` |
| カタログ（新規予定） | `Resources/Json/Sounds/catalog.json` |
| エフェクト定義 | `Resources/Json/Effects/*.json` |
