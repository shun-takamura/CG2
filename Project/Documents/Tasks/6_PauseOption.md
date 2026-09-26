# 6. ポーズメニューと設定画面

> **優先度 A** — 現状のポーズは**「なし」より悪い**。
> `paused_` を立てて `Update()` を早期 return するだけで、**画面には何も出ない**。
> 描画は走り続けるので絵は残るが、操作を一切受け付けない止まった画面になる。
> **プレイヤーにはフリーズしたバグと区別がつかない。**
>
> 設定画面は `5_TitleScene.md` Phase 2（タイトルの「設定」）から呼ばれるため、**そちらをブロックしている**。

最終更新: 2026-09-25

---

## 0. このドキュメントの位置づけ

優先度6番目だが、**`5_TitleScene.md` Phase 2 の前提**。設定画面はタイトルとポーズで**同じものを共用する**。

依存: **音量設定は `1_Sound.md` Step 1（バス実装）が終わっていないと作れない。**

---

## 1. 現状

### 1.1 ポーズ

`StagePlayScene.cpp:2760-2766` の**7行がすべて**：

```cpp
// Pause アクションでポーズトグル（メニュー実装は後で）
if (actions->IsTriggered(static_cast<int>(Action::Pause))) {
    paused_ = !paused_;
}
if (paused_) {
    return;
}
```

- `Update()` を抜けるだけなので **`Draw()` は走り続ける**＝**直前のフレームの絵が出たまま固まる**
- メニューも「PAUSE」の文字も出ない
- BGM も無い（そもそも音が無い）ので、**完全に無反応な画面**になる

### 1.2 設定画面

**存在しない。**

---

## 2. 現状詳細

### 2.1 ✅ 使える土台

| 要素 | 実体 |
|---|---|
| `Action::Pause` | 定義済み・バインド済み（`GameActions.h:19` / `KeyConfig.cpp:114`） |
| メニュー操作アクション6種 | `MenuConfirm` / `MenuCancel` / `MenuUp` / `MenuDown` / `MenuLeft` / `MenuRight`（`GameActions.h:20-25`）。既に Hub / GameOver / Result で使用中 |
| **設定の保存機構** | `KeyConfig::SaveUser()` が **`bindings` と `options` の両方**を `Resources/Json/Setting/keyconfig.user.json` へ書く実装済み |
| **オプション構造体** | `KeyConfig::Options`（`KeyConfig.h:18-21`）。既に `precisionAimMode`（"hold" / "toggle"）を持つ |
| 読み込み | `Game.cpp:167` で `KeyConfig::LoadAndApply(*actionMap, keyConfigOptions_)`。**user → default → 組み込みデフォルト**の優先順位 |
| バインドの文字列化 | `PhysicalBinding::ToString()` / `FromString()`（リバインドUIの表示に使える） |
| 時間制御 | `TimeGroup{World, Player, UI, Effect}` のグループ別倍率 |

### 2.2 ❌ 足りないもの

| # | 問題 | 詳細 |
|---|---|---|
| **1** | ポーズ中に何も表示されない | §1.1。**最優先** |
| **2** | **`KeyConfig::SaveUser()` が一度も呼ばれていない** | 実装は完成しているが**呼び出し元がゼロ**（`Game.cpp:167` の `LoadAndApply` のみ）。→ **`keyconfig.user.json` は存在しない**（`Setting/` にあるのは `editor_prefs.json` と `keyconfig.default.json` だけ）。**書き出し経路が丸ごと死んでいる** |
| **3** | キーコンフィグUIが無い | 仕組みは全部あるのに**変更する画面が無い**ので、実質リバインド不可 |
| **4** | 音量APIが無い | → `1_Sound.md` §2.2-2。**このタスクの前提** |
| **5** | `precisionAimMode` の切替UIが無い | `Options` に存在し保存もされるのに、変える手段が無い |
| **6** | **共通メニューUIが無い** | `UIButton` / `UIEditor` クラスは存在しない。各シーンが `TextRenderer` で**同じ縦並び選択ループを手書き**している（`HubScene.cpp:88-97` と `GameOverScene.cpp:96-105` がほぼ同一コード）。ここにタイトルとポーズを足すと**4箇所に重複**する |
| **7** | 解像度・フルスクリーン設定は作れない | Release は **1600×900 固定・リサイズ不可**（PostEffect が Swapchain へ直書きする構造のため）。→ §3 で対象外にする |

---

## 3. 設定項目（確定）

| 項目 | 内容 | 依存 |
|---|---|---|
| **音量** | Master / BGM / SE の3スライダ | `1_Sound.md` Step 1 |
| **キーコンフィグ** | アクションごとに2スロット（`InputActionMap::kSlotCount`）を再バインド | 無し（仕組み完成済み） |
| **精密射撃モード** | hold / toggle トグル | 無し（`Options` に既存） |

### 対象外（意図的）

| 項目 | 理由 |
|---|---|
| 解像度・フルスクリーン | Release がリサイズ非対応（§2.2-7）。対応には PostEffect の出力経路の作り直しが要る |
| 言語切替 | 日本語のみで進める判断 |
| 難易度設定 | 評価軸の「目指すところ」にはあるが、**1ステージ分の難易度調整すら未着手**なので時期尚早 |

---

## 4. 設定の保存先

### 4.1 新しいセーブ機構は要らない

`KeyConfig::SaveUser()` が既に `bindings` + `options` を1ファイルに書く。
**`Options` に音量を3つ足すだけで済む。**

```json
// Resources/Json/Setting/keyconfig.user.json
{
  "bindings": { "Fire": ["...", "..."], ... },
  "options": {
    "precisionAimMode": "hold",
    "volumeMaster": 1.0,
    "volumeBGM": 0.8,
    "volumeSE": 1.0
  }
}
```

### 4.2 ⚠️ ファイル名が実態と合わなくなる

`keyconfig` という名前なのに音量まで入ることになる。

| 案 | 評価 |
|---|---|
| **A. そのまま `keyconfig.user.json` を使う** | 変更ゼロ。名前は不正確だが動く |
| **B. `settings.user.json` にリネーム** | 意味は正確。`kDefaultPath` / `kUserPath`（`KeyConfig.cpp:19-20`）の2定数を書き換えるだけ。**`keyconfig.default.json` のリネームも必要** |

**→ B を推奨。** 今なら `user.json` が存在しない（§2.2-2）ので**移行データがゼロ**。後からやると配布済みの設定が飛ぶ。
クラス名も `KeyConfig` → `GameSettings` 等に変えるなら**このタイミングが最も安い**。

---

## 5. ポーズの仕様

### 5.1 メニュー項目

```
── PAUSE ──
 再開
 設定          → 設定画面（タイトルと共用）
 リトライ      → STAGEPLAY を再ロード
 タイトルに戻る → TITLE（確認を挟む）
```

⚠️ 評価軸の「一番ダメな例」は**「確認もなくいきなりタイトルに戻す」**。
**「タイトルに戻る」には必ず確認を挟む**こと。

### 5.2 音

**音量を落として再生を継続する**（`1_Sound.md` §3 で決定済み）。
狙いは**フリーズと区別させること**。実装は `SetBusVolume(Bus::Master, 0.3f)` の1行。

⚠️ ポーズ中も `SoundManager::Update()` は回し続ける必要がある（止めると終了ボイスが溜まる）。
`1_Sound.md` Step 0 で `Update()` を **Framework 側**に置くので、`StagePlayScene::Update()` が早期 return しても影響しない。**Step 0 がここでも効く。**

### 5.3 何を止めるか

現状は `Update()` ごと止めているため、**ポーズメニュー自身も動かせない**。
→ **`TimeGroup` を使い分ける**：

| グループ | ポーズ中 |
|---|---|
| `World` / `Player` / `Effect` | **0.0**（ゲームが止まる） |
| `UI` | **1.0**（メニューのカーソル・点滅・フェードが動く） |

こうすると `return` せずに済み、**ポーズ中の入力処理とメニュー描画が自然に書ける**。
既存のジャスト回避演出（`World=0.3` など）と同じ仕組みなので**新規機構は不要**。

### 5.4 ⚠️ Debug ショートカットとの競合

`StagePlayScene.cpp:2768-2786` に `#ifdef _DEBUG` の F2/F3/F4 ショートカットがある。
ポーズ中は早期 return の後ろにあるため現在は効かない。**TimeGroup 方式に変えると効くようになる**ので、ポーズ中は弾くこと。

---

## 6. 共通メニューUIの切り出し（推奨）

### 6.1 現状

`HubScene.cpp:88-97` と `GameOverScene.cpp:96-105` が**ほぼ同一のコード**：

```cpp
for (size_t i = 0; i < options_.size(); ++i) {
    const bool selected = (i == selectedIndex_);
    const Vector4 color = selected ? 黄 : 白;
    std::string label = (selected ? "> " : "  ") + ラベル;
    const float w = tr->MeasureWidth(label, scale);
    tr->DrawText(label, { 中央, y + lineHeight * i }, scale, color, 2.0f, 黒);
}
```

上下移動のロジックも同様に重複している。

### 6.2 これから増えるもの

タイトル（スタート/設定/終了）、Hub の作り直し、ポーズ、設定画面、タイトルに戻る確認ダイアログ。
**このまま行くと6箇所以上に同じコードが散る。**

### 6.3 方針

`componentization_policy` の「**機能が固まったら必ず切り出す**」に従い、
**`Game/UI/` に縦並び選択メニューのクラスを1つ作る**（項目リスト・選択index・上下移動・描画・決定コールバック）。

`pending_tasks` の「UIボタン配置機能」は**エディタでの配置まで含む重い計画**だが、
ここで要るのは**キーボード/パッドで操作する縦メニュー**だけなので、**まず軽い方だけ作る**。

⚠️ **新規ファイルになるので、着手前にユーザーへ相談 + `CG2_0_1.vcxproj` / `.filters` への登録が必要**（`CLAUDE.md` のルール）。

---

## 7. 落とし穴

### 7-1. ポーズ中も `SoundManager::Update()` を回す
§5.2。`1_Sound.md` Step 0 を先に済ませておけば自動的に解決する。

### 7-2. 「タイトルに戻る」に確認を挟む
§5.1。評価軸の「一番ダメな例」に直撃する項目。

### 7-3. 設定ファイルのリネームは今やる
§4.2。`keyconfig.user.json` が**まだ存在しない**ので移行コストがゼロ。後からだと配布済み設定が飛ぶ。

### 7-4. リバインド中に「そのキー」を拾わない
キーコンフィグUIで入力待ちにすると、**決定に使ったキーがそのまま新しいバインドとして入る**。
1フレーム待つか、キーを離すまで待つこと。

### 7-5. `MenuConfirm` 等を潰せないようにする
リバインドで `MenuConfirm` / `MenuCancel` を変な物に割り当てると**設定画面から出られなくなる**。
→ メニュー操作系は**リバインド対象から外す**か、**リセットボタンを必ず置く**。

### 7-6. 2スロット構成を忘れない
`InputActionMap` は1アクションにつき**2スロット**（キーボードとパッドを両方持つ想定）。UIもスロット2つ分を出すこと。

### 7-7. ポーズ中の Debug ショートカット
§5.4。

---

## 8. 実装ステップ

```
前提: 1_Sound.md Step 0-1（Update接続 + バス実装）が完了していること

Step 1  TimeGroup 方式のポーズへ書き換え        World/Player/Effect=0, UI=1。早期returnをやめる
Step 2  縦メニューUIクラスを作る                Game/UI/。要事前相談 + vcxproj 登録
Step 3  ポーズメニューを実装                    再開 / 設定 / リトライ / タイトルに戻る（確認付き）
Step 4  ポーズ中のバス音量ダッキング             SetBusVolume(Master, 0.3)
Step 5  設定ファイルをリネーム                  keyconfig.* → settings.*（今なら移行コスト0）
Step 6  Options に音量3項目を追加               保存/読込は既存の SaveUser/LoadAndApply に乗る
Step 7  設定画面を実装                          音量 / キーコンフィグ / 精密射撃モード
Step 8  SaveUser() を実際に呼ぶ                 現在デッドコードになっている書き出し経路を生かす
Step 9  タイトルから設定画面を呼べるようにする    → 5_TitleScene.md Step 11
Step 10 既存シーンを共通メニューUIへ移行         Hub / GameOver の重複解消
```

**Step 1〜4 でポーズは形になる。** Step 5〜8 が設定画面本体。

---

## 9. 関連ファイル索引

| 役割 | パス |
|---|---|
| ポーズ処理（現状7行） | `DirectXGame/Game/Scene/StagePlayScene.cpp:2760-2766` |
| Debug ショートカット | `DirectXGame/Game/Scene/StagePlayScene.cpp:2768-2786` |
| アクション定義 | `DirectXGame/Game/Config/GameActions.h:19-25`（Pause + Menu系6種） |
| キーコンフィグ本体 | `DirectXGame/Game/Config/KeyConfig.h` / `.cpp`（`:19-20` パス定数、`:139` Load、`:154` Save） |
| 読み込み呼び出し | `DirectXGame/Game/Game.cpp:167` |
| バインドの文字列化 | `DirectXGame/GameEngine/Core/Input/PhysicalBinding.h` |
| 入力アクション層 | `DirectXGame/GameEngine/Core/Input/InputAction.h`（`kSlotCount` = 2） |
| 時間グループ | `DirectXGame/GameEngine/Core/TimeGroup.h` |
| 重複しているメニュー描画 | `DirectXGame/Game/Scene/HubScene.cpp:88-97` / `GameOverScene.cpp:96-105` |
| 文字描画 | `TextRenderer`（初期化は `Framework.cpp:374`） |
| 設定ファイル | `Resources/Json/Setting/keyconfig.default.json`（`user.json` は**未生成**） |
