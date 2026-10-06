# 5. タイトルシーン

> **優先度 A** — 学校の評価軸で明示的に問われる項目。
> 評価軸は「**展示会で遠目に見て惹きつけられるか**」。つまり**止まっている時点で失格**。
> 現状は黒背景に "Title" という文字が1行出るだけ（65行）。
>
> 就活の観点でも、動画の**冒頭に映る画**なので投資対効果が高い。

最終更新: 2026-10-05

> **2026-10-05 時点の最新の計画は §10（水面と扉の開始演出・担当分け）。** §1〜§2 の「現状」は 2026-09-18 時点の記録で、その後 Phase 1 の大半と水面反射は実装済み。

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
| 水面・反射 | `DirectXGame/GameEngine/Graphics/Water/WaterSurface.*` / `WaterReflection.*`、`Resources/Shaders/Water/*`（詳細は `10_WaterReflection.md`） |

---

## 10. 水面と扉の開始演出（2026-10-04 確定）

### 10.1 現状（実装済み・2026-10-04）

| 項目 | 状態 |
|---|---|
| 背景 Skybox・3D ロゴ（`title.mesh`）・浮遊/首振り | ✅ |
| ロゴ登場（EaseOutBack）＋ヴィネット | ✅（起動時のラジアルブラーは 2026-10-05 に削除。毎回うるさいため） |
| Press Any Button → メニュー（スタート / ゲーム終了）、Stripe 遷移、BGM | ✅（A-2 で §10.2 の流れに置き換える。Press は ② の開始に、メニューは ⑤ に移り、⑥ の遷移は Stripe → **白フェード** に変わる） |
| 水面（y=0・水深0.1m）：平面リフレクション（ロゴ＋空）、水底の床の透過・影、フレネル | ✅ |
| 同心円のさざ波（ランダム間隔の波の束）＋ノイズの揺らぎ | ✅（中心＝周回中心） |
| カメラ：周回中心の周りを回る（`orbitAngle_`）。ロゴは中心の真上で常にカメラを向く | ✅ |
| デバッグカメラ対応・反射の参照元切替（Debug） | ✅ |
| 扉（`door_frame` / `door_leaf_L` / `door_leaf_R`）を周回中心に配置。反射・影に登録。開閉（奥へ）確認済み | ✅ 質感（A-1b）・色味の調整まで完了（§10.9） |
| 雲なしの昼の青空 `title_clear_sky.dds`。平行光源の向きを空の太陽に合わせた | ✅ |
| ⚠️ 扉の確認用にロゴを既定で非表示（`showLogo_ = false`）。Title Tuning → Door で切替 | **A-2（状態機械）で置き換える** |
| ⚠️ `Game.cpp` の Debug 開始シーンを一時的に `"TITLE"` に変更中 | **確認が終わったら `"STAGEPLAY"` に戻す** |

### 10.2 演出の流れ（確定）

| フェーズ | ロゴ | 扉 | 波 | カメラ |
|---|---|---|---|---|
| ① 待機 | 浮遊 | なし | **ごく小さな波がたまに出る** | 周回 |
| ② ロゴ消去（Press で開始） | 中心から大きな波が出て、揺らぎながらディゾルブで消える | — | **大きな波を数回**（ロゴ中心から） | 減速開始 |
| ③ 扉の線画 | — | ワイヤーフレーム（アウトライン）が**ノイズなしで下から**現れる（高さだけのディゾルブ） | 大きな波が続く | 周回が減速を始める |
| ④ 扉の形成 | — | 線画が出きったら、光の粒が集まりながら**下からノイズ付きディゾルブで実体化** | 大きな波が続く | **水面近くへ下がりながら広角・見上げの構図へ補間**（§10.7） |
| ⑤ 完成 | — | 完成 | **大きな波を止める**（①の静けさへ戻る） | 扉用の構図・正面で**固定** → メニュー表示 |
| ⑥ スタート | — | 扉が**奥へ両開き** → 中の光があふれる（§10.6） | — | 扉の中へ前進 → **白フェード**で `HUB` へ |

- 扉は**周回中心（ロゴがあった位置＝波紋の中心）**に立てる。カメラは中心を見たまま、扉の正面の角度で止まる。
- ① 〜 ⑤ の間は入力を受け付けない。メニューは ⑤ で出す（今の「Press 直後にメニュー」は置き換え）。
- ロゴは将来テクスチャ化する可能性あり（ディゾルブ等の処理はそのまま使えるように作る）。
- ① では今のロゴ登場（EaseOutBack）と「PRESS ANY BUTTON」の点滅をそのまま使う。**起動時のラジアルブラーは使わない（削除済み）**。

**決定事項（2026-10-05）**
- **スキップなし**。HUB などからタイトルに戻ってきたときも毎回 ①〜⑤ を見せる。
- ⑤ のメニューは「スタート（→ HUB）」「ゲーム終了」の2つだけ。**キャンセル（B / Esc）は無視**する（戻る先が無い。今の「キャンセルで PRESS ANY BUTTON に戻る」処理は消す）。
- **デモプレイは今回は入れない**。状態機械を書き直すときに `idleSeconds_` / `kDemoTriggerSeconds` を消す（§6 は保留扱い）。

### 10.3 担当分け

並行で作業するため、**チャットごとに触るファイルを分ける**。境界は §10.4 の API。

#### チャット W（水面・波の制御）— このチャット
- 担当ファイル：`WaterSurface.*` / `WaterReflection.*` / `Resources/Shaders/Water/*`
- `TitleScene` への変更は **ImGui の動作確認用ボタン**と、待機中の波の初期設定だけに留める（状態機械は書かない）
- [x] W-1 待機中の波の強さを外から切り替える（`SetAmbientRingScale`、時間をかけた補間つき）
- [x] W-2 任意のタイミングで大きな波を出す（`EmitRing`：CPU 側の固定長リングバッファを CB で渡し、シェーダで既存の波の束と合成）
- [x] W-3 連続で大きな波を出し続ける／止める（`StartRingBurst` / `StopRingBurst`。止めても出た波は自然に外へ抜けて消える）
- [x] W-4 Title Tuning に確認用ボタン（Emit / Burst Start / Burst Stop / 待機の強さ）
- [x] W-5 待機中の「ごく小さな波がたまに」のパラメータ決め（仮：振幅0.013/間隔6s/ばらつき0.7。実機で調整して確定）
- [x] ~~W-6~~ **不要（2026-10-06）**：ロゴは板ポリで頂点を揺らせず、揺らぎは Distortion エフェクトで作った（A-5）。（旧：ロゴの「揺らぎ」用に、ある地点・時刻の波の高さ/位相を CPU から取れる関数（`SampleRingPhase` 等）。ロゴ側の頂点の揺らしに使う場合のみ）

#### チャット A（アセット＋演出本体）— 別チャット
- 担当ファイル：`TitleScene.*`（状態機械・カメラ・ロゴ・扉）、Object3D のディゾルブ/ワイヤーフレーム関連、エフェクト JSON、アセット
- [x] A-1 **扉のモデル（必須）**：§10.5 の要件どおり。**アセット完了（2026-10-04）**。Claude が Blender スクリプトで生成（§10.5「作り方」）
  - [x] A-1a 形のプレビュー（Blender レンダリング画像）→ ユーザー確認（第1案で確定・2026-10-04）
  - [x] A-1b 大理石テクスチャ／法線マップ、金の質感（ボックス投影 UV＋FFT ノイズの継ぎ目無しタイル。cook 済み・両マテリアルとも shading=1。色味は §10.9 の調整で確定）
  - [x] A-1c glTF 出力 → `cook_assets.py` → エンジンで表示確認（正面の向き・原点・開閉）。PBR の確認は A-1b の後
- [x] A-2 フェーズの状態機械（①〜⑥）。入力は ⑤ まで無効、⑤ でメニュー。`showLogo_` / `showDoor_` の暫定切替を置き換える（2026-10-05 実装・ビルド未確認）
  - `TitleScene::Phase`（Idle / LogoVanish / DoorOutline / DoorForm / Ready / Enter）。入口の処理は `ChangePhase`、進行は `UpdatePhase`。各フェーズの長さ・波の強さは Title Tuning → Sequence で調整、「Restart Sequence」「Skip to Ready」あり
  - **仮演出（後のタスクで置き換える）**：③ 待つだけ（→ A-6 で線画に置き換え済み）／⑥ 扉を 85° 開いて黒フェードで HUB（→ A-9）。② のロゴ消去と ④ の扉の実体化はディゾルブ（A-4）に置き換え済み
  - [x] A-2a 扉の初期状態は非表示。④ の開始で表示し、反射・影に登録（`SetDoorVisible`）。ロゴは ② の終わりに反射から外す（`SetLogoVisible`）
  - [x] A-2b メニューのキャンセルを無視、デモプレイ用の `idleSeconds_` / `kDemoTriggerSeconds` を削除
- [x] A-3 カメラ：扉用の構図への補間と正面での固定（詳細は §10.7）
  - [x] A-3a Title Tuning → Camera Framing に FOV（`camera_->SetFovY` を毎フレーム反映）・距離・高さ・見る点のスライダーと「Framing Blend」（0=ロゴ用 / 1=扉用）。実機で値を決めたら §10.7 を更新する（2026-10-05・ビルド未確認）
  - [x] A-3b 構図を `CameraFraming`（FOV・距離・高さ・見る点の高さ）としてまとめ、ロゴ用→扉用を **④ の間（扉が出始めてから出きるまで）** に補間する
  - [x] A-3c **方式を変更（2026-10-05）**：カメラを扉の正面へ回り込ませるのではなく、③ の開始で「今の角速度から ease-out（2次）で ③〜④ の間に止まる角度」＝開始角＋角速度×時間/2 を求め、**扉の向き `doorYaw_` をその角度＋π に合わせて立てる**（扉は ③ まで存在しないので向きは自由）。最短回り・巻き戻りの問題が起きない
    - **罠（2026-10-05 修正）**：止まる角度によっては空の太陽が扉の真後ろになり、逆光で扉が黒く沈む（色味は「周回角 0 で停止・扉 yaw=π」で調整した）→ ③ の開始で平行光源の向きも停止角だけ Y 軸まわりに回す（`TitleScene::SetSunYaw`）。空・雲・水面の映り込みはそのまま
  - [x] A-3d ⑤ で周回を止めて固定。⑥ の前進はこの固定位置から始める
- [x] A-4 オブジェクト単位のディゾルブ（2026-10-05 実装・ビルド未確認、シェーダは compile_shaders.py で OK）。反射パスは PS 共通なので水面の映り込みも一緒に消える
  - [x] A-4a `Material` 末尾に拡張（128B → 176B、`static_assert` あり）：`dissolveEnable / Progress（見えている割合）/ EdgeWidth / NoiseScale / HeightMin / HeightMax / NoiseWeight / EdgeColor`。既定は無効。**ArcanaEngine 同期対象**（`Material.h` / `ModelInstance.cpp` / `AnimatedModelInstance.cpp` / Object3D の PS 3本 / `Dissolve.hlsli`）
  - [x] A-4b `Resources/Shaders/Object3D/Dissolve.hlsli`（`ApplyDissolve` / `ApplyDissolveEdge`）を `Object3d.PS`・`Object3dNoEnv.PS`（ロゴ）・`Object3dPBR.PS`（扉）が include。**マスクテクスチャは使わない**：ワールドの高さ（0..1）とワールド座標の 3D 値ノイズ（2オクターブ）を `NoiseWeight` で混ぜる → 部位をまたいでも映り込みでも模様がつながる。旧 2 本の PS の `Material` は PBR と同じ並びに揃えた
  - [x] A-4c 影は**ディゾルブ中は出さず、実体化しきった時点で落とす**（`DrawShadowCasters` で `doorDissolve_ >= 1`）。clip 付きの影 PSO は作らない
  - TitleScene：④ 扉は元の位置で下から実体化（`doorDissolveStyle_`、高さ範囲＝台座の底〜+3.8m）、② ロゴはノイズだけで消える（`logoDissolveStyle_`）。Title Tuning → Sequence で進み具合・縁の幅/色・ノイズを調整可
- [x] A-5 ロゴの消去：ディゾルブ（A-4）＋**揺らぎ**（2026-10-06 実装・ビルド未確認）。ロゴは頂点 4 つの板なので頂点では揺らせない（W-6 不要）→ 既存の Distortion エフェクトで、ロゴの中心から波紋の歪みを広げてディゾルブに重ねる
  - `Resources/Json/Effects/TitleLogoRipple.json`（Plane・ビルボード・歪みのみ、2 秒で 0.5→9 倍、強さ 0.6→0）を ② の開始に `EffectManager::Play`。エフェクトエディタで調整可
  - 波紋のノーマルマップ：`tools/Python/gen_ripple_normal.py` → `Assets/Textures/NormalMapTexture/TitleRippleNormal.png` → cook（フォルダ名で線形 BC7）
  - エンジン変更：エフェクトのプリミティブに `distortionStrengthAnim` / `distortionEndStrength`（寿命の間に歪みの強さを補間。一定だと寿命が切れた瞬間に歪みがパッと消える）。JSON 読み書き・エディタ UI あり。**ArcanaEngine 同期対象**
- [x] A-6 扉の線画（2026-10-05 実装・ビルド未確認）。**案 2（組み合わせ）に決定**：③ は特徴線のメッシュ、④ は扉のアウトライン（ポストエフェクト）
  - 線画：`gen_title_door.py --export-lines Assets/Models/TitleDoor` で、折れ目（30° 以上）・縁・大理石と金の境目を太さ 2.4cm の角柱にした `door_*_lines`（計 約1.5万三角形）を書き出す。扉本体のアセットは書き出さない。cook 済み
  - ③：線画を**高さだけ（ノイズなし）のディゾルブで下から**出す（`doorLineDissolveStyle_`）。ライティング無しの金白。水面の映り込みにも出る
  - ④：扉本体がノイズ付きディゾルブで実体化する間、`MaskedOutline`（深度から作った法線のアウトライン）で扉を縁取る。扉の部位は `SetObjectId(1)`＝colorFire の枠。点滅なし
  - ⑤：線画とアウトラインをそろって 0.8 秒で消す
  - **罠（2026-10-05 修正）**：ID パスが扉の形全体に ID を書くと、まだ実体化していない部分の背景（遠くの水面）の深度差を折れ目と誤判定し、地平線より下が板のように塗りつぶされた → ディゾルブ中は `WriteIDDissolve.PS`（`Object3DManager::GetIdDissolvePipelineState`）で消えている部分に ID を書かない。ルート定数は id＋ディゾルブ値の 8 個（既存の ID パスは先頭 1 個だけ使うので影響なし）。`Dissolve.hlsli` は `DissolveClip`（引数版）に分け、`DISSOLVE_NO_MATERIAL` で gMaterial なしでも include 可。**ArcanaEngine 同期対象**
  - エンジン変更：`Scene::HasExtraIdPassObjects` / `DrawExtraIdPassObjects`（シーンがメンバで持つ物を ID パスに描く仮想関数）。`Game.cpp` はこれが true でも ID パスを走らせる。**ArcanaEngine 同期対象**（`Scene.h/.cpp`）
  - ③ の長さ（仮 1.5 秒）・線とアウトラインの色・強さは Title Tuning → Sequence で調整して決める
- [ ] A-7 光の粒の収束（ディスラプター Step7 の収束パーティクルを流用。候補 `Resources/Json/Effects/DisruptorCharge.json`）＋下からディゾルブで実体化。**A-12 が前提**
- [x] A-8 扉を反射の対象に登録（`WaterReflection::AddTarget`）、影に参加（`DrawShadowCasters` の override に追加）。ディゾルブ実装時は表示切替と合わせて登録/解除すること
- [x] A-9 ⑥ 扉が開く → 中に入る → シーン遷移（2026-10-05 実装・ビルド未確認。値は Title Tuning → Enter (6) で調整）
  - タイムライン（2026-10-06 調整：カメラが動き出してから真っ白まで **1.5 秒**）：0s 扉が開き始める（1.3s ease-out で 85°）＋`OpenDoor` → 0.4s カメラ前進（1.6s ease-in、`enterFraming_`＝扉の中心の高さで水平に枠の手前 0.5m まで）＋`InDoor` → 1.1s 白フェード開始（0.8s）→ 1.9s 真っ白 → `HUB`
  - [x] A-9a 扉板を蝶番の軸で奥へ回転（ease-out、85°）
  - [x] A-9b **光の部屋**（光る板から変更）：`gen_title_door.py --export-light` の `door_light`＝開口部より片側 0.15m 広いアーチ形の筒（枠の奥の面から奥行き 2.5m、奥は閉じる、**面は内向き**）。ライティング無しの白。どこから覗いても背景が見えず、正面からは枠に隠れてはみ出さない。開いた扉板は天井から少し突き出すが、カメラが低いので枠の陰で見えない。⑥ の間だけ表示・反射に登録
  - [x] A-9c 光芒と後光 → **新規ポストエフェクト `LightShaftEffect`**（GPU Gems 3 の Volumetric Light Scattering）。光の部屋だけを ID パス（id=3、深度テストで見えている部分のみ）でマスクにし、各ピクセルから光源（扉の奥 0.7m・高さ 1.6m を画面 UV に投影）へ向かってマスクを減衰つきでサンプルして加算。扉板が遮った所は自然に筋の影になる。Bloom（画面全体の明るさ）だと昼の空や白い大理石まで光るので使わない。ラジアルブラーとエフェクト JSON は使わない
    - エンジン：`FilterEffect/LightShaftEffect.*`・`Shaders/PostEffect/Filters/LightShaft.PS.hlsl` を新規、`PostEffect` に登録（outline 用ルートシグネチャを共用）。ArcanaEngine.vcxproj(.filters) に追記済み。**ArcanaEngine 同期対象**
  - [x] A-9d 扉の奥にポイントライト（`AcquirePointLight`、開く量に合わせて強く）。`ResetSequence` / `Finalize` で `ReleasePointLight`
  - [x] ~~A-9e 周りを暗くする~~ → **入れない（2026-10-06 決定。今の暗さで十分）**。ライトシャフトを入れて見づらければ入れる。入れる場合、水面は空の cubemap を直接引くので `WaterSurface` に明るさ係数が要る
  - [x] A-9f `FadeTransition::SetNextFade(色, フェード, ホールド)`：次の1回だけ色と長さを変え、終わったら黒・既定（0.5s / 0.1s）に戻る
    - **罠（2026-10-05 修正）**：トランジションの板はシーンと一緒に描かれ（`SceneManager::Draw` の最後）、**その後でポストエフェクトが掛かる**。真っ白でもヴィネットの縁とライトシャフトが残り、HUB へ切り替わった瞬間に消えてパッと変わって見えた → 白フェードに合わせて両方を弱める（`TitleScene::GetPostEffectFade`）。根本対策（トランジションをポストエフェクトの後に描く）は全シーンに影響するので未実施
  - [x] A-9g 音：`Game.cpp` で `bgm_title_scene`（TitleBGM.mp3）/ `se_title_open_door`（OpenDoor.mp3）/ `se_title_in_door`（InDoor.mp3）を読み込み。**HUB は従来の `bgm_title` のまま**。BGM は ⑥ で 2 秒かけてフェードアウト（`SoundManager::Set2DSoundVolume` を追加、ArcanaEngine 同期対象）。クレジットは `Documents/Readme.md`
  - 未使用の素材：`Resources/Sounds/TitleScene/SE/WaterWave.mp3`（② の大きな波に合わせる候補）
- [x] A-11 昼の青空の cubemap（すずめ風）。`tools/BlenderPipeline/gen_title_sky.py` → `Resources/Cubemaps/title_clear_sky.dds`。光の向き `kSunLightDirection` を空の太陽に合わせ済み。映り込み用の IBL 版 `title_clear_sky_ibl.dds` も作成（§10.9）
- [x] A-14 ロゴを PNG（`Assets/Textures/Title/TitleRogo.png`、512×256）に差し替え（2026-10-05・ビルド未確認）
  - 板ポリ `Resources/Models/TitleLogo/logo_plane.mesh`（`tools/Python/gen_title_logo_plane.py` → `cook_assets.py`。幅 2 × 高さ 1、表が -Z＝カメラと同じ回転で正対）
  - 板はテクスチャを持たず、`TitleScene::ApplyLogoMaterial` が GPU 準備後に `ModelInstance::SetTextureFilePath` で差し替え（エンジン変更なし）。ライティング無し・α は ② のフェードに使用
  - ロゴは**影を落とさない**（シャドウパスは透明部分を捨てられず四角い影になる）。半透明なので**水面の後に描く**
  - 旧 `Resources/Models/Title/title.mesh` は未使用（削除はしていない）
- [x] A-12 TitleScene で EffectManager を動かす（2026-10-05・ビルド未確認。`Initialize` / `Finalize` で `StopAll`）：`Scene::UpdateGlobalEffects` / `DrawGlobalEffects` を呼ぶ（今は呼んでいないので、A-7 の光の粒・A-9b の後光が出ない）
- [x] A-15 マウスで水面に波を立てる（クラス内コンテスト向け・2026-10-06 作り直し・ビルド未確認）
  - **カーソルでなぞった所に波**（クリック不要）。マウスの位置から描画カメラの逆 VP でレイを作り水面との交点を取り、動かした距離に比例した波源を軌跡に沿って入れる。止まっている間は出さない。Debug はビューポート窓内の位置を画面解像度へ換算
  - 当初の「EmitRing（16 枠のリングバッファ）にクリック/ドラッグで波を出す」方式は、上限を超えると見えている波が上書きされてパッと消えたので廃止（クリック判定の `mouseClickMaxSeconds` もキーコンフィグから削除）
  - **GPU の波のシミュレーション `RippleSimulation`**（新規・エンジン）：周回中心まわり 40m 四方を 512² の高さマップ（R32G32_FLOAT＝今/1つ前の高さ、2 枚を交互）で持ち、コンピュートシェーダ `Resources/Shaders/Water/RippleSimulation.CS.hlsl` が固定刻み（60 回/s、1 フレーム最大 4 刻み）で 2 次元の波動方程式を解く。波源はガウス形のへこみ（1 フレーム 32 個まで）。範囲の縁で吸収。**波の数に上限なし・干渉する**
  - 水面の PS が高さマップ（t6）の傾きを法線に足す（遠くはちらつき防止で弱める）。待機中の波・大きな波（EmitRing / バースト）・ノイズは従来どおり計算式。映り込みも一緒に揺れる
  - `WaterSurface::Initialize` に `SRVManager*` を追加、`AddRippleImpulse` / `DispatchSimulation`（シーンの Draw の最初に呼ぶ）。**ArcanaEngine 同期対象**（RippleSimulation.* を ArcanaEngine.vcxproj に追記済み）
  - 調整：Title Tuning → Water → Ripple Simulation (GPU)（速さ・減衰・範囲・法線の強さ・Clear）、Mouse Ripple / Attract（波源の半径・強さ・間隔）
- [x] A-17 マウスで UI を操作（2026-10-06・ビルド未確認）
  - `Game/UI/UIPointer.h/.cpp`（新規、CG2_0_1.vcxproj に追記）：ゲーム画面のピクセル座標でのマウス位置・移動・左クリック。Debug はビューポート窓内を換算し、**ImGui パネル上のクリックは数えない**
  - `VerticalMenu`：`SetPosition` で配置をメンバに持ち `Draw()` と当たり判定で共有。カーソルを動かすと項目を選択、項目の上で左クリックで決定。タイトル・HUB・ポーズメニューで有効
  - タイトル ①：**ゲーム画面の上での左クリックも Press**（F8 は除外。DirectInput のマウスボタンは引き続き AnyInputTriggered から除外し、UIPointer 経由のクリックだけ数える）。`PickWaterPoint` も UIPointer を使う
  - 残：StagePlay のレティクルにも同じ換算処理があるので UIPointer に寄せる（別タスク）
- [x] A-16 デモモード（展示用の自動進行、2026-10-06・ビルド未確認）
  - `Game/Scene/AttractMode.h/.cpp`（シングルトン、CG2_0_1.vcxproj に追記）。**F8 で開始・終了**（`Game::Update`、Release でも有効）。画面表示は無し
  - タイトル ① ロゴの登場後 4 秒で Press → ⑤ メニューが出て 2 秒で「スタート」→ HUB で 2.5 秒後にカーソルを「タイトルに戻る」へ、0.8 秒後に決定 → タイトル…をループ。人の入力でも普通に進む。待ち時間は Title Tuning → Mouse Ripple / Attract
- [x] A-18 水底の床を石畳に差し替え（2026-10-06・ビルド未確認）
  - 素材は別チャット作成の Flagstone（`Resources/Textures/Terrain/Flagstone_BaseColor.dds` / `NormalMapTexture/Terrain/Flagstone_NormalMap.dds` / `MaskTexture/Terrain/Flagstone_Height.dds`、1 枚 3m、凹凸 -0.013〜+0.0068m）
  - 床は水面の PS が屈折レイの当たる位置で描いているので（Object3D ではない）、**水面の PS に床の法線マップ（t7）と視差 POM（t8）を追加**：`WaterSurface::SetFloorMaps(法線, ハイト, 深さ)`。接空間は T=+X / B=+Z / N=+Y。法線は床のライティング（日差し・影）に効く。**ArcanaEngine 同期対象**
  - 調整：Title Tuning → Water（Floor Normal Strength / Flip Y / Parallax Depth / Layers）。法線の緑の向きは実機で凹凸が逆に見えたら Flip Y
- [ ] A-10 （発表会に間に合えば）構造物：ボス戦アリーナのパーツをモジュール化して共用し、タイトル用に中心を囲むよう数個配置（Blender 併用レベルエディタで配置を別管理）

### 10.4 チャット間の API（チャット W が実装済み・2026-10-04 確定、チャット A が呼ぶ）

```cpp
// WaterSurface（DirectXGame/GameEngine/Graphics/Water/WaterSurface.h）
void Update(float deltaTime);                                     // 毎フレーム呼ぶ（TitleScene::Update で呼び済み）
void SetRippleCenter(const Vector3& center);                      // 待機中の波の中心（TitleScene が周回中心を毎フレーム設定済み）

void SetAmbientRingScale(float scale, float blendSeconds = 0.0f); // 待機中の波の振幅倍率（1=既定, 0=無し）。blend>0 で滑らかに補間
float GetAmbientRingScale() const;

void EmitRing(const Vector3& center, float amplitude, float wavelength,
              float packetLength = 3.0f, float maxRadius = 35.0f);   // 大きな波を1発（最大16個、超えたら最古を上書き）
void StartRingBurst(const Vector3& center, float interval,
                    float amplitude, float wavelength);             // 開始時に1発＋interval 秒ごと（間隔±20%/振幅0.7〜1.0/波長0.8〜1.0倍でばらつく）
void StopRingBurst();                                              // 新しく出すのをやめる（出た波は消えるまで残る）
bool IsRingBursting() const;
int  GetActiveEmittedRingCount() const;                            // まだ水面に残っている大きな波の数
```

- 単位：`amplitude` は高さ [m] 相当。待機中の波は `0.013`（TitleScene で設定）、大きな波は `0.10〜0.15`・波長 `2〜3m` が目安（Title Tuning → Water → Ring Control で試せる）。
- 速度と距離減衰は待機中の波と共通（`Params::ringSpeed` = 0.8m/s / `ringFalloff`）。
- 呼び出し例（チャット A の状態機械）：
  - ② 開始：`StartRingBurst(ロゴ中心, 1.2f, 0.12f, 2.5f)` ＋ `SetAmbientRingScale(0.0f, 0.5f)`（小さな波を引っ込める）
  - ⑤ 完成：`StopRingBurst()` ＋ `SetAmbientRingScale(1.0f, 1.5f)`（静けさへ戻す）
- 注意：16 枠なので、1.2 秒間隔の連続発射を約 19 秒以上続けると、まだ見えている古い波を上書きする（今回の演出の長さなら問題なし）。
- 扉の反射：`WaterReflection::AddTarget(door)`。ディゾルブを PS に入れれば反射にも自動で効く。

### 10.5 アセット

#### 必須：扉（チャット A）

**イメージ**：扉のデザイン＝原神のスタート画面の扉／サイズと開く演出＝パルテナの鏡のスタート演出／全体の絵＝すずめの戸締まり（水の中に扉が1枚立つ）。

| 要件 | 理由 |
|---|---|
| **枠と扉板を別メッシュ**：`door_frame` / `door_leaf_L` / `door_leaf_R` | ⑥ で扉板だけを回転させて開くため |
| **両開き・奥開き** | 扉板が光の中へ倒れていき「中へ招き入れる」絵になる。手前開きは扉板がカメラの進路と光を遮る |
| **扉板の原点を蝶番の軸に置く**（外側の縁 × **枠の奥側の面**） | 原点の回転だけで開けるため。奥側に置くと開いたときに枠のアーチと干渉しない |
| 枠の原点は**台座の底面の中央**、正面の向きを統一 | 周回中心にそのまま立て、カメラの停止角度を決めやすくするため |
| 開口部 **幅 1.8m × 高さ 3.2m**（アーチの頂点まで）、枠の幅 0.3m・奥行き 0.4m、全体 約 2.4m × 3.6m | プレイヤーモデルが翼ごとすり抜けられる大きさ（パルテナの扉のスケール） |
| 台座：高さ 0.15m の段を1つ | 水深 0.1m に対して上面が少し水から出る |
| 面の構成を素直に、数千ポリゴン以下。**彫りはジオメトリでなく法線マップで** | ワイヤーフレームの線がきれいに出るため |

**デザイン（2026-10-04 確定案。プレビューで確認中）**

| 部位 | 案 |
|---|---|
| シルエット | 上部はアーチ型。頂点を少し尖らせる（パルテナ寄り）、全体の形は原神の扉寄り |
| 大理石（本体） | 白、灰色の淡い筋。roughness 0.35 前後 |
| 金（装飾） | 枠の内側の縁取り・アーチ頂点の要石・鏡板の縁・中央の紋章（左右の扉板にまたがり、開くと割れる）・台座の四隅の金具。metallic 1 / roughness 0.3 |
| 金の量 | **全体の 15% 程度**に抑える（多いと派手になり、光の当たる部分が目立たない） |

- 部位ごとのマテリアルは submesh ごとの `.mat` で対応済み（`ModelInstance.cpp:91-96`）。IBL があるので金に空が映る。
- ⚠️ **PSO は submesh[0] のマテリアルで選ばれ、cook の PBR 自動切替は「法線マップの有無」だけで決まる** → 大理石・金の**両方に法線マップを付ける**（金は平らな法線マップでよい）。ファイル名に `NormalMap` を含める（線形圧縮のため）。

**作り方**：Claude が Blender（4.4）をスクリプトで動かして生成する。

```
tools/BlenderPipeline/gen_title_door.py（寸法・金の配置をパラメータで持つ）
  → Blender をバックグラウンドで実行 → プレビュー画像 / glTF 出力（Assets/Models/TitleDoor/）
  → tools/Python/cook_assets.py → Resources/Models/TitleDoor/（.mesh / .mat / .dds）
```

- 手彫りのレリーフは作れない。作れるのは段差の縁取り・鏡板・幾何学的な紋章まで。凝った紋章は後から法線マップだけ差し替える。
- 原点で書き出すこと（cook はノードのワールド変換を頂点へ焼き込む）。名前は ASCII のみ。

#### 既にあるもの（新しく作らなくてよい）
- ディゾルブのノイズ：`Textures/MaskTexture/noise0.dds` / `noise1.dds` / `VerticalGradation_*.dds`
- 光の粒：`Textures/Effect/sparkle_00.dds` / `sparkles_*.dds` / `Glow_*.dds` / `ster_*.dds`
- ロゴ：`title.mesh`（テクスチャ版にする場合は α 付き PNG、2:1 程度）
- 水面の揺らぎ：計算で作っているので素材不要

#### 必須（扉以外）
- **昼の青空の cubemap**（すずめ風。今は夜空）。金への映り込みもこれで決まる
- 光芒用の帯状グラデーションテクスチャ（既存で足りなければ）

#### 仕上げで欲しいもの（今は無くてよい）
- ~~石畳のテクスチャ~~ → A-18 で差し替え済み
- 構造物：すずめ風の骨組み・建物（A-10）
- 効果音：波、ディゾルブ、扉の形成、扉が開く音（→ `1_Sound.md`）

### 10.6 扉に入る演出（⑥・2026-10-04 確定）

パルテナの鏡のスタート演出のように、**扉が開くと中が光っていて、周りに光芒があり、カメラが中へ入るとホワイトアウト**する。

| 順 | 内容 | 実現方法 |
|---|---|---|
| 1 | 扉が奥へ両開き | 扉板を蝶番の軸で回転 |
| 2 | 中の光があふれる | 枠の奥（扉板の幅 0.9m より奥）に光る板。後光は `Glow_*.dds` の加算ビルボード |
| 3 | 光芒 | `RadialBlurEffect` を扉の中心に＋加算合成の板/円錐を数本 |
| 4 | 金が光る | 扉の奥にポイントライト（最大8個の枠を使う）。開く量に合わせて強く |
| 5 | 周りが暗くなる | ヴィネット強化、空・水面を暗く。パルテナの「暗い中に光の扉」に寄せる |
| 6 | カメラ前進 → ホワイトアウト | 白フェード（`FadeTransition` に色指定を追加）。行き先は `HUB` |

- 閉じた扉から光が漏れる演出は**入れない**（2026-10-05 決定）。光は ⑥ で扉が開いてから。

**落とし穴**
- **Bloom が無い**ので、昼の空の中では白い板が雲と同じ明るさにしかならない → 5 の「周りを暗く」は省略しない。
- **光る板はライトではない**（周りを照らさない）→ 金に光を映すのは 4 のポイントライトの役目。
- **カメラが光る板を通り抜けると板が消えて裏の水面が見える** → 板に届く前に白フェードが α=1 になるよう時間を逆算する（FadeIn → Hold でシーン切替 → FadeOut の既存構造で足りる）。
- 扉の裏には壁が無い。カメラは正面から入るので問題ないが、横から見せる構図は避ける。
- 行き先の `HUB` は今は黒背景＋文字メニュー。作り込みは別チャットで行う。

### 10.7 カメラの構図（2026-10-04 決定）

**ロゴの間は今の周回の構図。扉が出始めてから出きるまで（④）の間に、水面近くへ下がりながら扉用の構図（広角・見上げ）へ補間し、⑤ で固定する。**（2026-10-05 更新：すずめの戸締まりのキービジュアルに合わせて「水平」→「低い位置から見上げ」に変更）

#### 2つの構図
| 項目 | ロゴ用（①〜③、`logoFraming_`） | 扉用（⑤ 以降、`doorFraming_`・仮の値） |
|---|---|---|
| 視野角（縦） | 0.45 rad（約 26°） | **0.75 rad（約 43°）** |
| 距離 `radius` | 20m | **10m**（2026-10-06 実機で決定） |
| 高さ `height` | 1.5m | **0.5〜0.8m（水面すれすれ）** |
| 見る点 `aimHeight` | 0.55m（見下ろし） | **2.5〜3m（扉の上部）＝見上げ**（仮 0.65m / 2.75m）。地平線は画面の下から 1/3〜2/5、空が画面の大半 |
| `orbitAngle_` | 周回し続ける | ③〜④ で減速して停止。扉はその停止角の正面に立てる（A-3c） |

- 値は案。A-3a の FOV スライダーで実機を見て決め、決まったらここを更新する。
- **扉用を広角＋低い位置からの見上げにする理由**：すずめの戸締まりの構図（水面すれすれから広角で見上げ、地平線は画面の下寄り、空が広い、手前は水面と映り込み、建物は左右の端）に寄せるため。今の望遠＋見下ろしだと画面の上端が水平から約 9° しかなく、A-10 で背景の建物（例：50m 先・高さ 12m＝約 13.5°）を置くと空が全部埋まる。扉用の構図なら上端は約 21°。
- 広角は手前の水面と映り込みが大きく広がり、奥行きが出る効果もある。

#### 補間のしかた
- ④ の開始で補間を始め、④ の終わり（⑤）で終える（長さは ④ の演出時間。ease-in-out）。周回の減速（A-3c）は ③〜④ で行う。
- 補間するのは FOV・距離・高さ・見る点の高さの4つ（`orbitAngle_` の減速は A-3c で実装済み）。
- ~~`orbitAngle_` は「今の角度 → 扉の正面に最も近い角度」で補間する~~ → 2026-10-05 変更：カメラの停止角を先に決め、扉をそちらへ向ける（A-3c）。`orbitAngle_` は補間対象から外れる。
- ⑥ の前進（扉の中へ）は ⑤ の固定位置から始める。

#### 背景の構造物を置くとき（A-10）
- 画面の中央と上の方は空ける。左右の端に寄せる。扉の真後ろには置かない（扉が空に浮かんで見えるように）。
- 遠くの物は低め（地平線の少し上に連なる程度）。

### 10.8 次のチャット（演出の実装）への引き継ぎ

- 扉・空は**スクリプトで生成**している。形や色を変えるときは Blender で直接いじらず、スクリプトのパラメータを直して作り直す（.blend の手編集は消える）。
  - 扉：`tools/BlenderPipeline/gen_title_door.py`（`--save` / `--export Assets/Models/TitleDoor` / `--preview`）→ `python tools/Python/cook_assets.py`
  - 空：`tools/BlenderPipeline/gen_title_sky.py`（`--export Assets/title_clear_sky.hdr` / `--preview`）→ `convert_hdr_to_dds.py --silent`。**同名の DDS があると変換を飛ばす**ので、作り直すときは cmft → texconv を直接実行して上書きする
  - **Blender の GUI から書き出さない**（扉板の原点＝蝶番がずれる。`--export` は書き出し中だけ位置・回転を 0 に戻している）
- 扉の座標：枠の原点は台座の底面の中央、正面は +Z。蝶番（枠のローカル）は L=(0.9, 0, -0.2) / R=(-0.9, 0, -0.2)。開くときは L が `yaw - open`、R が `yaw + open` で奥（-Z）へ。`TitleScene::UpdateDoor()` 参照。
- **タイトル画面のアセット作業は完了（2026-10-04）**。詳細と残りの注意点は §10.9。
- 優先順の目安：~~A-2（状態機械）~~ → ~~A-3a/b（構図の補間）~~→ A-4/A-5（ディゾルブ・ロゴ消去）→ A-12 → A-6/A-7（線画・実体化）→ A-9（扉に入る）。

### 10.9 アセット作業のまとめ（2026-10-04 完了）

#### 作ったもの
| アセット | 中身 | 作り直し方 |
|---|---|---|
| 扉 | 枠＋左右の扉板（両開き・奥開き）。大理石＋金の PBR。UV はボックス投影（2m で1枚） | `gen_title_door.py --export Assets/Models/TitleDoor` → `cook_assets.py` |
| 背景の空 `title_clear_sky.dds` | 雲なしの昼の青空（青強め：チリ 0 / オゾン 4 / 彩度 1.5）。背景と水面が使う | `gen_title_sky.py --export Assets/title_clear_sky.hdr` → cmft → texconv で上書き（§10.8） |
| 映り込み用の空 `title_clear_sky_ibl.dds` | 地平線より下を石の暖色 (0.25, 0.21, 0.16) にした IBL 専用版。扉の金と大理石だけが使う | `gen_title_sky.py --ibl --export Assets/title_clear_sky_ibl.hdr` → 同上 |

#### 色味の調整で分かったこと（同じ問題を踏まないために）
| 症状 | 原因 | 対処 |
|---|---|---|
| 金が真っ黒、大理石が暗い | `TitleScene` が IBL の環境マップ（t1）を設定していなかった。PBR の金属は環境マップの色だけで色が決まる | `SetEnvironmentTexture` を設定（**`LoadTexture` も必要**。`SetEnvironmentTexture` はパスを覚えるだけ） |
| 大理石が青い | 拡散 IBL は cubemap の最粗 mip（空全体の平均＝濃い青）。PBR は拡散を π で割るので日差し 1.2 では負ける | 日差しを強く・暖色に、大理石の environmentCoefficient を下げる |
| 金が緑（オリーブ）っぽい | 金属の映り込み＝「空の青 × 金色」。PBR として正しい結果 | 空の映り込みを弱め、**遠景の雲だけを別の強さで映す**（下記のエンジン変更） |
| カメラより低い所の金が白っぽい | 反射が下向き→空の下半分（明るい水色で埋めてあった）を映す | IBL 専用 cubemap の地平線より下を石の暖色にする |
| 設定した材質値が効かない | マテリアルは遅延ロードで、`Initialize` 直後はまだ無い | `UpdateDoor()` で毎フレーム上書き（submesh の .mat 名 `_Gold` / `_Marble` で見分ける） |

#### 確定した値（`TitleScene.h`。Title Tuning → Door で調整可）
| 項目 | 値 |
|---|---|
| 日差しの強さ / 色 | 4.55 / (1.0, 0.96, 0.9) |
| 大理石の environmentCoefficient | 0.25 |
| 金：environmentCoefficient / metallic / roughness | 0.56 / 1.0 / 0.6 |
| 金の色 / 雲の映り込み | (255, 184, 66) / 1.0 |

#### エンジン側の変更（**ArcanaEngine と同期すること**）
- `Object3DManager`：ルートシグネチャ末尾に [13]=b7 / [14]=t5 ＋ サンプラ s4（`CloudSky.hlsli` と共用）。`SetCloudLayer()` / `BindCloud()`。雲が無いシーンはダミーを挿す。**ルートシグネチャを貼り直した後は BindFog / BindShadow と一緒に `BindCloud` が必須**（`AnimatedObject3DInstance` に追加済み）
- `Material.h`：`padding2` → `cloudReflection`（既定 0＝他のモデルは無影響。サイズ不変）。`ModelInstance` / `AnimatedModelInstance` で 0 初期化
- `Object3dPBR.PS.hlsl`：反射ベクトルで `CloudSky()` を引き、`cloudReflection` の強さで鏡面反射に足す（粗いほど弱く）
- `Object3DInstance`：`GetModelInstance()` を追加（submesh ごとの材質調整用）
- **雲を持つシーンは終了時に `object3DManager_->SetCloudLayer(nullptr)` 必須**（`CloudLayer` の寿命はシーン側。`TitleScene::Finalize` で実施済み）

#### 残っている注意点（演出のチャットへ）
- [ ] ルートシグネチャを変えたので、他のシーン（STAGEPLAY 等）の見た目が変わっていないか確認
- [x] ~~ロゴは BlinnPhong のまま。明るすぎないか確認~~ → ロゴは PNG 板＋ライティング無しにしたので不要（A-14）
- [ ] 台座の手前の上面の網目ノイズ（影のアクネか、水面と 5cm で深度が競合しているか未切り分け）
- [ ] `Game.cpp` の Debug 開始シーンが `"TITLE"` のまま（確認が終わったら `"STAGEPLAY"` に戻す）
- 形や色を変えるときは Blender で直接いじらず、スクリプトのパラメータを直して作り直す（§10.8）
