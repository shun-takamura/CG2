# 5. タイトルシーン

> **優先度 A** — 学校の評価軸で明示的に問われる項目。
> 評価軸は「**展示会で遠目に見て惹きつけられるか**」。つまり**止まっている時点で失格**。
> 現状は黒背景に "Title" という文字が1行出るだけ（65行）。
>
> 就活の観点でも、動画の**冒頭に映る画**なので投資対効果が高い。

最終更新: 2026-09-18

---

## 0. このドキュメントの位置づけ

優先度5番目。**Phase 1（動きのある背景とロゴ）は単独で着手できる。**

Phase 2 は他タスクと繋がる：
- **設定画面**は `6_PauseOption.md` で作るものを共用する
- **チュートリアルステージ**は §5 で新規に発生したスコープ。別タスクとして起こすこと

---

## 1. 現状

`TitleScene.cpp` は **65行**。実質これだけ：

```cpp
void TitleScene::Draw() {
    const char* label = "Title";
    tr->DrawText(label, { 中央, screenH * 0.35f }, 3.0f, 白, 2.0f, 黒);
}
```

- 背景は単色。3Dオブジェクトもスプライトも1つも無い
- BGM も効果音も無い
- カメラは `(0, 0, -10)` に置かれているだけで**何も映していない**

---

## 2. 現状詳細

### 2.1 ✅ 動いているもの

| 機能 | 実体 |
|---|---|
| Press Any Button → Hub | `actionMap->AnyInputTriggered()` で `ChangeScene("HUB", TransitionType::Fade)` |
| 遷移演出 | `TransitionType::Fade` |
| テキスト描画 | `TextRenderer`（中央寄せ・アウトライン付き） |

### 2.2 ❌ 足りないもの / 気づきにくい問題

| # | 問題 | 詳細 |
|---|---|---|
| **1** | **画面に動きが皆無** | 評価軸の「一番ダメな例＝2D静止画・画面のどこも動いていない」に**そのまま該当** |
| **2** | **タイトルモデルが存在するのに未使用** | `Resources/Models/Title/` に `title.mesh` / `title.mat` / `title.dds` が**揃っている**（56KB、2026-06-22作成）が、`TitleScene` から一切参照されていない |
| **3** | **デモプレイが未実装** | `idleSeconds_` は加算しているが使われず、`kDemoTriggerSeconds = 10.0f` は `(void)kDemoTriggerSeconds;` で**警告抑制されているだけ**（`TitleScene.cpp:43`） |
| **4** | **Debug ビルドではタイトルを通らない** | `Game.cpp:196` が `#ifdef _DEBUG` で `ChangeSceneImmediate("STAGEPLAY")`。Release だけ `"TITLE"`。→ **開発中に一度も表示されないので、壊れても気づかない** |
| **5** | **`StripeTransition` が実装済みなのに未使用** | 全シーンが `TransitionType::Fade` 固定。評価軸「ポストエフェクトや矩形を使った凝った画面遷移」に対し、**手持ちの札を使っていない** |
| **6** | `TransitionType::Circle` は enum にあるが未実装 | `BaseTransition.h:26` に宣言はあるが `CircleTransition.cpp` が存在しない（実在は `Fade` / `Stripe` のみ） |
| **7** | **フォントが1種類・1サイズしか初期化されていない** | `Framework.cpp:374` で `MPLUS1p-Medium.ttf` を **32px・アトラス1024** で初期化。→ **タイトルロゴを `scale 3.0` で出すと 32px を3倍に拡大するのでボケる** |
| **8** | フォント資産が眠っている | `Resources/Fonts/` に **8種**（M PLUS 1p 7ウェイト + **ReggaeOne**）。ReggaeOne はディスプレイ書体で**ロゴ向き**だが未使用 |
| **9** | 音が無い | → `1_Sound.md` |

### 2.3 ⚠️ シーン遷移の構造的な問題

現在の導線：

```
TITLE  →  HUB  →  STAGESELECT  →  STAGEPLAY
（文字1行）  （2択）      （Stage1のみ）
```

**1ステージしかないゲームに、プレイ開始まで画面が3枚ある。**

- `HubScene` は `StageSelect` / `ゲーム終了` の**2択のみ**（103行）。設計にあった「スキルショップ / スキル装備」は未実装で、**セーブデータが無いため実装もできない**
- `StageSelectScene` は `"Stage1"` と表示して決定を待つだけ（59行）
- **どの画面からもタイトルに戻れない**（Hub に「タイトルへ」が無い）

評価軸の「不親切」に該当する。→ **§5 で構成を確定済み**（Hub は残し、StageSelect を Hub に統合する）。

---

## 3. 目標像

評価軸の「目指すところ」は **「動きのあるタイトル画面と、しばらく放置するとデモプレイが流れる」**。

ただし優先度に差があるので、**段階を分ける**。

| 段階 | 内容 | 評価軸への効き |
|---|---|---|
| **Phase 1** | 動きのある背景 + ロゴ + Press Any Button | **「遠目に見て惹きつけられる」をここで満たす。最重要** |
| **Phase 2** | メニュー（スタート / 設定 / ゲーム終了）＋ Hub の作り直し | 「不親切」の解消。設定画面は `6_PauseOption.md` と共用 |
| **Phase 3** | デモプレイ | 評価軸では「入れる**のもよい**」＝**任意**。余裕があれば |

**Phase 1 だけで評価軸の主要部分は満たせる。** Phase 3 に時間を使いすぎないこと。

---

## 4. Phase 1：動きのある背景とロゴ

### 4.1 背景の作り方（推奨：既存資産の流用）

**新規に作らず、STG の環境をそのまま使うのが最も安い。**

- `StageEnvironment` の Skybox（cubemap）をそのまま表示し、**ゆっくり回す**
- `2_StageArt.md` で作る浮遊プロップを数個置いて**手前を流す**
- カメラを微小に周回させる（idle sway）

これだけで「動いている」条件を満たす。**タイトル専用のアセットを作らない**のが要点。

### 4.2 ロゴ

**`TextRenderer` で出さないこと。** §2.2-7 の通り 32px アトラスの拡大になりボケる。

| 手段 | 評価 |
|---|---|
| **`title.mesh` を 3D で出す** | ✅ **推奨。** 既に存在する。3Dなので回転・光源・被写界深度が効き、動きも付けられる |
| ロゴをテクスチャ化してスプライト | ⭕ 次善。解像度は自由だが平面的 |
| TextRenderer を大サイズで再初期化 | ❌ アトラスを増やすことになる。ロゴ1枚のために割に合わない |

**→ `title.mesh` を使う。** これが「モデルが存在するのに未使用」を解消する最短路でもある。

### 4.3 演出の足し方

既存の PostEffect フィルタ15種が使える（`FilterEffect/`）。特に：

- `VignetteEffect` — 周辺減光で中央（ロゴ）に視線を集める
- `RadialBlurEffect` — 起動時の一瞬だけ強くかけて収束させる
- `DissolveEffect` — ロゴ出現
- `GaussianEffect` — 背景だけ軽くぼかしてロゴを立たせる

**遷移には `StripeTransition` を使う**（実装済みで未使用）。評価軸の「凝った画面遷移」にそのまま効く。

---

## 5. Phase 2：画面構成（確定）

**Hub は残す。** チュートリアルステージへの導線を持たせるため。
そのうえで **StageSelect を Hub に統合**し、画面を3枚から2枚に減らす。

### 確定した導線

```
TITLE
 ├─ スタート      → HUB
 ├─ 設定          → オプション画面（6_PauseOption.md と共用）
 └─ ゲーム終了    → PostQuitMessage

HUB
 ├─ チュートリアルステージ → STAGEPLAY（チュートリアル）
 ├─ Stage1                → STAGEPLAY
 ├─ （以降ステージを追加）
 └─ タイトルに戻る        → TITLE
```

### 各画面の責務

| 画面 | 持つもの | 備考 |
|---|---|---|
| **TITLE** | スタート / 設定 / ゲーム終了 | 「ゲームを始めるか、環境を整えるか、やめるか」だけを扱う |
| **HUB** | チュートリアル / 各ステージ / タイトルに戻る | **ステージ選択を兼ねる。** ステージが増えてもここに足すだけ |

### この構成の要点

- **`StageSelectScene` は導線から外す**（Hub がその役割を持つため）。ただし**コードは消さない**——`SceneFactory` の登録も残しておく
- **`HubScene` の「ゲーム終了」は削除**し、代わりに「タイトルに戻る」を入れる。終了はタイトルの責務に一本化する
- **設定画面はタイトルとポーズで同じものを使い回す**。→ `6_PauseOption.md` で実装し、タイトルからも呼ぶ
- スキルショップ / スキル装備は**セーブデータが無いので当面作らない**。Hub のタブUI化はステージが増えてから

### ⚠️ 新しく発生したスコープ：チュートリアルステージ

Hub にチュートリアルへの導線を置く方針により、**「プレイできるチュートリアル」が必要になった**。

これは元の課題一覧の「操作ガイド（現状：無し）」に対する回答でもある。評価軸は
「ReadMe を読まなくても操作方法が分かる仕組み。画面に絵を表示しつづけるなど。動きがあると◎」。

**本ドキュメントの範囲外。別タスクとして起こすこと。**
`STAGEPLAY` シーンを wave/tuning 違いで再利用できるか（ステージIDの受け渡し機構が要る）が最初の論点になる。

---

## 6. Phase 3：デモプレイ（任意）

### 6.1 調査結果：動画再生の基盤は「無い」が、近いものはある

当初の計画では「Media Foundation で動画ファイルを再生」としていた。実際に確認したところ：

| 期待 | 実態 |
|---|---|
| 動画ファイル再生の仕組み | ❌ **存在しない** |
| `CameraCapture`（`GameEngine/CameraCapture.h`） | ⚠️ **Webカメラ入力**の Media Foundation 実装（G.U.N.D.A.M. 用）。動画ファイル再生ではない |

**ただし `CameraCapture` は「MF SourceReader → フレーム取得 → `TextureManager` へ流す」経路を別スレッドで完成させている。** 入力を `MFCreateSourceReaderFromURL`（＝`SoundManager::LoadFile` と同じAPI）に差し替えれば、動画ファイル再生へ転用できる見込みがある。

### 6.2 実現手段の比較

| 案 | 内容 | 評価 |
|---|---|---|
| **A. 動画ファイル再生** | OBS で録画した動画を再生 | ✅ **推奨。** 見た目を完全に制御できる。**就活用のプレイ動画はどのみち作るので、素材が実質タダ**。`CameraCapture` の経路を転用 |
| B. 実際のゲームを自動操作 | 入力を自動生成して裏で動かす | ⭕ 容量ゼロ・常に最新。ただし**自機が被弾して死ぬ**ので見栄えが不安定。カメラ演出も制御できない |
| C. 入力リプレイ | 操作ログを記録・再生 | ❌ パーティクルや敵の徘徊に乱数が入るため厳密再現が難しい。`Seek` はあるが決定論の保証範囲外 |

**→ A を推奨。** ただし Phase 3 は任意なので、時間が無ければ**やらない判断でよい**。

⚠️ 動画を同梱すると配布ZIPが大きくなる。`Resources/` は FS直読みなので同梱忘れにも注意（`1_Sound.md` §8-6 と同じ）。

---

## 7. 落とし穴

### 7-1. Debug ビルドではタイトルを通らない
`Game.cpp:196` が Debug で `STAGEPLAY` 直行。**作業中は一時的に `TITLE` へ変えて確認すること**（戻し忘れに注意）。

### 7-2. TextRenderer でロゴを出すとボケる
`Framework.cpp:374` で **32px** 初期化。`scale 3.0` は 32px の3倍拡大。ロゴは `title.mesh` かスプライトで。

### 7-3. `TransitionType::Circle` は使えない
enum にはあるが実装が無い。使えるのは `Fade` と `Stripe` のみ。

### 7-4. `Game::GetPostEffect()->ResetEffects()` が各シーンの冒頭で呼ばれる
`TitleScene::Initialize()` の1行目。**タイトル用のポストエフェクトはこの後に設定する**こと。

### 7-5. Hub / StageSelect を消さない
導線から外すだけにする。`SceneFactory` の登録も残す。ステージが増えたときに戻せるようにしておく。

### 7-6. `AnyInputTriggered()` とメニューの併存
Phase 2 でメニューを置くと、「どのキーでも進む」と「上下で選ぶ」が競合する。
→ **Press Any Button でメニューを*出す*、の2段構え**にするのが素直（タイトル画面の定番でもある）。

---

## 8. 実装ステップ

```
Phase 1（最重要）
Step 1  Debug の開始シーンを一時的に TITLE へ         作業のため
Step 2  背景に Skybox を表示してゆっくり回す           StageEnvironment の cubemap を流用
Step 3  title.mesh を配置してロゴにする               既存アセット。回転/浮遊アニメを付ける
Step 4  Vignette + Dissolve で登場演出                既存 PostEffect
Step 5  "PRESS ANY BUTTON" の点滅表示
Step 6  遷移を Stripe に変更                          実装済み・未使用
Step 7  BGM とカーソル音                              → 1_Sound.md

Phase 2
Step 8  タイトルにメニューを実装                       スタート / 設定 / ゲーム終了
Step 9  Hub を作り直す                                各ステージ + チュートリアル + タイトルに戻る
        └ 「ゲーム終了」を削除し「タイトルに戻る」へ置換
Step 10 StageSelect を導線から外す                    コードと SceneFactory 登録は残す
Step 11 設定画面をタイトルから呼べるようにする          6_PauseOption.md の実装を共用

Phase 3（任意）
Step 12 動画再生の実装                                CameraCapture の MF 経路を転用
Step 13 10秒放置でデモ再生 / 入力で復帰                idleSeconds_ は既にある
```

**Step 1〜6 で評価軸の主要部分を満たせる。** ここまでを優先する。

---

## 9. 関連ファイル索引

| 役割 | パス |
|---|---|
| タイトルシーン | `DirectXGame/Game/Scene/TitleScene.cpp` / `.h`（`:43` に未使用の `kDemoTriggerSeconds`） |
| 開始シーンの分岐 | `DirectXGame/Game/Game.cpp:195-199`（Debug=STAGEPLAY / Release=TITLE） |
| ハブ | `DirectXGame/Game/Scene/HubScene.cpp`（2択のみ） |
| ステージ選択 | `DirectXGame/Game/Scene/StageSelectScene.cpp`（Stage1のみ） |
| 遷移の種類 | `DirectXGame/Game/Scene/Transition/BaseTransition.h:22-28`（Circle は未実装） |
| 遷移の実装 | `DirectXGame/Game/Scene/Transition/FadeTransition.*` / `StripeTransition.*` |
| 文字描画 | `TextRenderer`（初期化は `DirectXGame/GameEngine/Framework.cpp:374`、**MPLUS1p-Medium 32px / アトラス1024**） |
| ポストエフェクト | `DirectXGame/GameEngine/Graphics/OffscreenRendering/FilterEffect/`（15種） |
| 環境（Skybox流用元） | `DirectXGame/Game/Scene/StageEnvironment.h` / `.cpp` |
| 動画再生の転用候補 | `DirectXGame/GameEngine/CameraCapture.h` / `.cpp`（**Webカメラ入力**。MF→テクスチャ経路） |
| タイトルモデル（未使用） | `Resources/Models/Title/title.mesh` / `.mat` / `.dds` |
| フォント | `Resources/Fonts/`（8種。**ReggaeOne がロゴ向き**） |
| Cubemap | `Resources/Cubemaps/`（3枚） |
