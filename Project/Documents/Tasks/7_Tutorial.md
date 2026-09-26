# 7. チュートリアルステージ

> **優先度 B**（ただし評価軸には明示的にある）
> 学校の評価軸「操作ガイド」の**現状は「無し」**。一番ダメな例は「操作説明は ReadMe を読め」。
> 目指すところは「**ReadMe を読まなくても操作方法が分かる仕組み。画面に絵を表示しつづけるなど。動きがあると◎**」。
>
> `5_TitleScene.md` §5 で Hub にチュートリアルへの導線を置くと決めたため発生したタスク。

最終更新: 2026-09-25

---

## 0. このドキュメントの位置づけ

`5_TitleScene.md` の画面構成決定（Hub にチュートリアル導線）から派生。

**最初の論点は「チュートリアルの中身」ではなく「ステージを2つ持てるようにする」こと**（§2）。
ここが解けないと着手できない。

---

## 1. 現状

| 要素 | 状態 |
|---|---|
| チュートリアル | ❌ 存在しない |
| 操作ガイド表示 | ❌ 存在しない |
| `operation.dds` | 📌 `Resources/Textures/operation.dds` は**前作で文字テキストの操作説明を出すために作ったもの。今回は使わない**（プレイアブルチュートリアルにするため） |
| ステージの複数対応 | ❌ **StagePlayScene は stage1 固定** |

---

## 2. 最初の論点：ステージを2つ持てるようにする

### 2.1 現状のハードコード

ステージのデータ経路は**3箇所に散らばって直書き**されている。

| # | 対象 | 場所 | 形 |
|---|---|---|---|
| 1 | Wave（敵配置） | `RailStagePart.h:140` | `std::string wavePath_ = "Resources/Json/Waves/stage1.json";` ← **メンバ変数なので setter を足すだけで済む** |
| 2 | Tuning（調整値） | `StagePlayScene.cpp:72` | `constexpr const char* kStagePlayTuningPath = "Resources/Json/Tuning/StagePlay.json";` |
| 3 | Scene（地形・スプライン） | `StagePlayScene.cpp:2667` | `const std::string kAutoLoadPath = "Resources/Json/Scenes/StagePlay.json";` |

**1 は軽い。2 と 3 は定数なので、ステージ名から組み立てる形に変える必要がある。**

### 2.2 シーンへの引数渡しが無い

```cpp
void ChangeScene(const std::string& sceneName, TransitionType transitionType);
```

`SceneManager` はシーン名の文字列しか受け取らない。**「どのステージか」を渡す口が無い。**

### 2.3 ✅ 解決策：`BossRetryState` と同じパターンを踏襲する

このプロジェクトには**既に先例がある**。`Game/Score/BossRetryState.h` は、
「GameOverScene が選択結果を書き込み、StagePlayScene::Initialize が読む」というシングルトンで、
まさに**シーンを跨いで選択を渡す**用途に使われている。

```cpp
// GameOverScene 側
BossRetryState::GetInstance()->RequestBossRetry();
SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Fade);

// StagePlayScene::Initialize 側
if (BossRetryState::GetInstance()->ConsumeBossRetryRequest()) { ... }
```

**→ 同じ形の `StageSelection` シングルトンを作る。**

```cpp
// Hub 側
StageSelection::GetInstance()->SetStage("tutorial");   // または "stage1"
SceneManager::GetInstance()->ChangeScene("STAGEPLAY", TransitionType::Stripe);

// StagePlayScene::Initialize 側
const std::string id = StageSelection::GetInstance()->GetStage();  // 既定 "stage1"
// → Waves/<id>.json / Tuning/<id>.json / Scenes/<id>.json
```

**利点**: `SceneManager` の API を変えずに済む。既存の流儀に合う。実装が小さい。

⚠️ 新規ファイルになるので、**着手前にユーザーへ相談 + `CG2_0_1.vcxproj` / `.filters` への登録**が必要（`CLAUDE.md` のルール）。

### 2.4 ボスフェーズをスキップできるようにする

`StagePlayScene` の `Phase{Rail, Landing, Boss}` は、`seekMaxSec_` 到達で **自動的に Landing → Boss へ遷移する**（`StagePlayScene.cpp:2789-2803`）。

**チュートリアルの最後にボスが出てくるのは不適切**なので、フェーズ遷移を止める手段が要る。

**推奨**: Tuning JSON の `phase` セクション（既存。`seekMaxSec` / `landingDurationSec` を持つ）に **`hasBoss`（bool, 既定 true）** を足す。
チュートリアルの Tuning で `false` にすれば、Rail 終了で Result へ抜ける。**新しい仕組みを足さずに済む。**

---

## 3. 何を教えるか

プレイヤーの実装済みアクションは多い（移動 / 射撃 / チャージ / 精密 / ロック / 回避 / ジャスト回避 / 分身カウンター4方向 / 近接弱4段 / 近接強 / 回復 / 必殺技2種）。
**全部やると長すぎて誰も最後まで遊ばない。**

### 3.1 チュートリアルで扱う（5項目）

| 順 | 項目 | 教え方 |
|---|---|---|
| 1 | **移動** | 何も出さず、進みながら動かせることだけ示す |
| 2 | **通常射撃** | 動かない的を数体出す |
| 3 | **チャージ射撃** | **通常弾では割れない硬い的**を出す。→ `4_StageGimmick.md` の岩柱と同じ仕組みが使える |
| 4 | **回避** | 敵弾を1発ずつ、間隔を空けて撃たせる |
| 5 | **ジャスト回避 → 分身カウンター** | **最重要。** スロー演出が入るので、4方向の選択肢を落ち着いて見せられる |

**5 を必ず入れること。** ジャスト回避＋分身カウンター（4方向派生）は実装上の最大の投資でありながら、
現状の道中では発動機会がほとんど無い（`4_StageGimmick.md` 参照）。
チュートリアルは**確実に発動させられる唯一の場所**で、かつ**動画素材としても強い**。

### 3.2 チュートリアルでは扱わない

近接コンボ / 近接強 / 精密射撃 / 必殺技 / 回復 / ロック。

→ **本編の道中で、初出のタイミングに画面端へボタン表示を出す**方式にする（§4）。
チュートリアルを長くするより、こちらの方が評価軸の「画面に絵を表示しつづける」に合う。

---

## 4. 見せ方

評価軸は「**画面に絵を表示しつづける。動きがあると◎**」。文字だけの説明は評価されない。

| 要素 | 方針 |
|---|---|
| **ボタン表示** | 実際のボタン形状の絵を出す。**キーコンフィグを反映する**こと（`PhysicalBinding::ToString()` がある） |
| **動き** | ボタンの絵を明滅させる / 押すと沈むアニメ。「動きがあると◎」はここ |
| **配置** | 画面端に出し続ける。中央を塞がない |
| **パッド/キーボードの切替** | 最後に使った入力デバイスに応じて絵を差し替えられると良い（余裕があれば） |

⚠️ **`operation.dds` は使わない。** 前作で「操作説明を文字テキストで一覧表示する」ために作ったもので、
今回はプレイアブルチュートリアル方式なので方向性が違う。**文字の一覧表は評価軸の「ReadMe を読め」に近く、狙いと逆。**

---

## 5. 進行の作り方

**「できたら次へ」方式**にする（時間で流すと、できていない人が置いていかれる）。

```
ステップ開始 → ガイド表示 → プレイヤーが該当アクションを実行 → 成功演出 → 次のステップ
                                   ↑ 一定時間できなければヒントを強調
```

- 各アクションの実行検出は **`InputActionMap` の `IsTriggered` で足りる**ものが多い
- ジャスト回避だけは**成立イベント**を見る必要がある（`StagePlayScene` が既に判定・演出を持っている）
- **スキップ可能にする**こと。2周目以降に強制されると苦痛

⚠️ Wave システムは `trigger_sec`（時刻駆動）なので、**「できたら次へ」とは噛み合わない**。
チュートリアルの敵出現は Wave JSON ではなく**チュートリアル側の進行管理から `SpawnEnemyAt` を直接呼ぶ**のが素直。

---

## 6. 落とし穴

### 6-1. ステージIDの既定値を `stage1` にする
`StageSelection` を通さずに `STAGEPLAY` へ入る経路（Debug の直接起動、GameOver からのリトライ）があるため、**未設定時は必ず `stage1` にフォールバック**すること。

### 6-2. リトライでステージIDを維持する
`GameOverScene` の「リトライ」は `ChangeScene("STAGEPLAY")` を呼ぶだけ（`GameOverScene.cpp:61-63`）。
**チュートリアル中に死んで stage1 に飛ばされないよう**、`StageSelection` は `Consume` せず保持する設計にする
（`BossRetryState::ConsumeBossRetryRequest()` は消費型だが、**ステージIDは消費してはいけない**）。

### 6-3. Tuning JSON の自動保存に注意
`StagePlayScene` は ImGui の変更を Tuning JSON へ**自動保存**する。
ステージIDでパスを切り替えると、**チュートリアルを開いたまま調整すると `tutorial.json` に書かれる**。意図通りだが、混同しないこと。

### 6-4. シーンJSONの自動保存も同様
`SceneEditorWindow` の `Auto Save`（既定ON）が効くため、チュートリアルのシーンを開いている間の編集は `Scenes/tutorial.json` に入る。

### 6-5. `RailStagePart::RebindCameraPath()`
シーンをロードし直すと `dynamicSplines_` が作り直される。`OnAfterSceneLoad()` で必ず呼ぶ（`2_StageArt.md` §7-12）。

### 6-6. チュートリアルにも環境が要る
真っ黒な空間では評価されない。ただし**専用の環境を作る必要は無い**——
`SkyAboveClouds` セクションを流用し、短いレール（制御点を少なく）で足りる。**谷は不要なので `2_StageArt.md` の Step 4 を待たない。**

---

## 7. 実装ステップ

```
Step 1  StageSelection シングルトンを作る       BossRetryState と同じ形。要事前相談 + vcxproj 登録
Step 2  3つのハードコードパスをステージID駆動へ   RailStagePart.h:140 / StagePlayScene.cpp:72, 2667
Step 3  Tuning の phase に hasBoss を追加        false でボス戦へ遷移しない
Step 4  Hub にチュートリアル導線を追加            → 5_TitleScene.md Step 9
Step 5  チュートリアル用の3つのJSONを作る         Waves / Tuning / Scenes の tutorial.json
Step 6  短いレールと環境を用意                   SkyAboveClouds 流用。制御点は少なく
Step 7  進行管理を実装                          「できたら次へ」+ スキップ
Step 8  ガイド表示UIを実装                      ボタンの絵 + 明滅。キーコンフィグ反映
Step 9  5項目のステップを作る                    移動 → 射撃 → チャージ → 回避 → ジャスト回避
Step 10 本編にボタン表示を出す                   近接/必殺技/精密などの初出タイミング
```

**Step 1〜3 は「ステージを2つ持てるようにする」基盤で、チュートリアル以外にも効く**
（ステージを増やす場合の前提そのもの）。ここだけ先に済ませておく価値がある。

**Step 3（hasBoss）は `4_StageGimmick.md` Step 5 のチャージ弾に依存**——
チュートリアルでチャージを教えるには、チャージ弾プレハブが存在している必要がある。

---

## 8. 関連ファイル索引

| 役割 | パス |
|---|---|
| シーン跨ぎ状態の先例 | `DirectXGame/Game/Score/BossRetryState.h` |
| Wave パス（メンバ変数） | `DirectXGame/Game/Scene/RailStagePart.h:140` |
| Tuning パス（定数） | `DirectXGame/Game/Scene/StagePlayScene.cpp:72` |
| シーンJSONパス（定数） | `DirectXGame/Game/Scene/StagePlayScene.cpp:2667` |
| フェーズ定義 | `DirectXGame/Game/Scene/StagePlayScene.h:39-43` |
| フェーズ遷移 | `DirectXGame/Game/Scene/StagePlayScene.cpp:2788-2803` |
| シーン切替API | `DirectXGame/Game/Scene/SceneManager.h:61-73`（**引数はシーン名のみ**） |
| リトライ導線 | `DirectXGame/Game/Scene/GameOverScene.cpp:61-63` |
| バインドの文字列化 | `DirectXGame/GameEngine/Core/Input/PhysicalBinding.h` |
| 未使用の操作説明画像 | `Resources/Textures/operation.dds` |
| ステージデータ（既存） | `Resources/Json/Waves/stage1.json` / `Tuning/StagePlay.json` / `Scenes/StagePlay.json` |
