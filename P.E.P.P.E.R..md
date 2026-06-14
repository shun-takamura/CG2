# 🤖 P.E.P.P.E.R. 実装計画書

**P**roactive **E**valuation & **P**rofiling for **P**erformance-**E**fficiency **R**ecommendation
~ エンジンのボトルネックを自前計測で暴き、最適化を提案するプロファイラ ~

---

## 0. 位置づけ（J.A.R.V.I.S. ファミリー）

司令塔 **J.A.R.V.I.S.** の下にぶら下がる4ツールの1つ。

```
S.U.N.D.A.Y. P.E.P.P.E.R. G.U.N.D.A.M. H.A.P.P.Y.
(自動検証)   (性能計測)   (手動操作)   (配布ランチャー)
```

| ツール | 役割 |
|---|---|
| **P.E.P.P.E.R.** | CPU/GPUの区間時間を計測 → 最遅区間を特定 → 最適化提案 |

**このファイルの範囲**：PEPPER本体（C++プロファイラ ＋ profile.log ＋ Python解析 ＋ ImGui表示）。
**第0層（エンジン共通基盤）は `S.U.N.D.A.Y..md` で定義済み・実装済み**。PEPPERはそれを共用する：
- **ログ基盤（SessionLogger）**：1セッション1フォルダ `Logs/YYYY-MM-DD_HHMMSS/`、カテゴリ別ファイル、6段階レベル。→ PEPPERは **`Profile` カテゴリ（profile.log）を1つ足すだけ**。
- **レベル制**：profile.log は高頻度なので Debug/PEPPERビルドでのみ出力（Release は出さない）。

**前提（合意済み）**：C++/Python。リポジトリはpublic。計測は「見るだけ」でゲーム挙動（乱数・dt）に触れないため、**SUNDAYの決定論リプレイと両立**する。

---

## 1. 目的と合格基準

**目的**：エンジンの各処理区間（Update/Draw/各マネージャー/シェーダ等）のCPU時間・GPU時間を継続計測し、ボトルネックを特定。最遅区間を Python/Claude に渡して**具体的な最適化を提案**する（将来は修正→PRまで自動化）。

**合格基準**：プロファイラを有効にして起動すると、毎フレーム各計測区間のCPU/GPU時間が `profile.log` に出る。Pythonが**最遅区間TopN**を抽出し、該当コード（`file:line`）を提示して最適化候補を出せる。ImGuiで区間別時間が**リアルタイム**に見える。

**スコープ**：v1は**計測・可視化・最遅区間抽出まで**。Claude連携の最適化提案は半自動（人が確認）、自動修正→PRは将来拡張。

---

## 2. 全体構成

```
[ゲーム実行(Debug/PEPPERビルド)]
   │  PEPPER_SCOPE("名前") / PEPPER_GPU_SCOPE(...) で区間を囲む
   ▼
Profiler(シングルトン) … CPU=QueryPerformanceCounter / GPU=D3D12タイムスタンプクエリ
   │  フレーム終端で集計
   ├──► profile.log（SessionLogger の Profile カテゴリ）
   └──► ImGui リアルタイム表示（区間別バー/テーブル/フレームグラフ）
            │
            ▼（オフライン）
   Python が profile.log を読み最遅区間TopN抽出 → file:line 提示 → Claudeが最適化提案
```

---

## 3. C++プロファイラ

### 3.1 計測の単位「区間（Section）」
- 1区間 = 名前付きのコード範囲。`__FILE__` / `__LINE__` を一緒に記録し、後で該当箇所へ飛べるようにする。
- **ネスト対応（階層プロファイル）**：区間はスタックで親子関係を持ち、`Update > SceneUpdate > Collision` のように木で集計する。
- 計測対象の自然な境界：`Framework::Update` / `Framework::Draw`、`SceneManager::Update/Draw`、各マネージャー（Sprite/Object3D/Particle/Skybox/Text/Line…）の `Update`/`Draw`、重い処理（Skinning、衝突判定、パーティクル更新など）。

### 3.2 CPU計測（QueryPerformanceCounter）
- `QueryPerformanceFrequency` で tick→ms 変換係数を起動時に1回取得。
- **RAIIスコープ**で計測：
  ```cpp
  { PEPPER_SCOPE("SceneManager::Update");  /* ... */ }   // ブロックを抜けた瞬間に区間時間確定
  ```
  - マクロ `PEPPER_SCOPE(name)` は内部で `ProfileScope` 一時オブジェクトを作り、コンストラクタで `QueryPerformanceCounter` 開始・デストラクタで終了し Profiler へ加算。`__FILE__`/`__LINE__` も渡す。
  - **Release では空マクロに展開**してオーバーヘッドゼロ（`#ifdef` ガード）。
- 同名区間がフレーム内で複数回呼ばれる場合は**回数と合計時間**を集計（例：弾100発の更新）。

### 3.3 GPU計測（D3D12タイムスタンプクエリ）
- `ID3D12QueryHeap`（`D3D12_QUERY_HEAP_TYPE_TIMESTAMP`）と readback 用バッファを `DirectXCore` 側に用意。
- 区間の前後で `commandList->EndQuery(heap, TIMESTAMP, idx)` を積む（begin用/end用の2スロット）。
  ```cpp
  { PEPPER_GPU_SCOPE(commandList, "Object3D::Draw"); /* Draw コマンド */ }
  ```
- フレーム末に `ResolveQueryData` → readback バッファへコピー。
- **GPUは非同期**なので、その場で待たず **1〜2フレーム遅延でリードバック**（フェンス待ちを避ける）。
- `commandQueue->GetTimestampFrequency()` で tick→ms 変換。
- ⚠️ タイムスタンプは同一キュー内で有効。コピーキュー/コンピュートキューを跨ぐ計測は別管理。

### 3.4 オーバーヘッドと安全性
- QPCは軽量。GPUクエリはリードバック遅延で隠す。計測が**フレーム時間を歪めない**ことを優先。
- Profiler は**メインスレッド前提**（区間スタックの順序保証のため）。
- 計測は乱数・dt・ゲーム状態に一切触れない → **リプレイ決定論に影響しない**。

---

## 4. profile.log（フォーマット）

- SessionLogger の **`Profile` カテゴリ**を新設（`profile.log`）。レベルは高頻度ゆえ `Trace`（Debug/PEPPERビルドのみ）。
- 1フレーム複数行（区間ごと）。1行1レコード（人間もPythonも読める）。
  ```
  frame=1234 section=SceneManager::Update depth=1 calls=1 cpu_ms=1.234 gpu_ms=0.000 file=SceneManager.cpp line=47
  frame=1234 section=Object3DManager::Draw depth=2 calls=1 cpu_ms=0.210 gpu_ms=2.880 file=Object3DManager.cpp line=88
  ```
- `depth` で階層、`calls` で同名集約回数、`cpu_ms`/`gpu_ms` で内訳。
- フレーム区切りが分かるよう、行頭の `frame=` を必ず付ける（SessionLoggerの時刻prefixも付く）。

---

## 5. Python解析 ＋ 最適化提案

- `profile.log` を読み込み（tail も可）、区間別に **平均 / 最大 / 95%tile / 累積 / 呼び出し回数** を集計。
- **最遅区間TopN**を抽出し、`file:line` 付きで提示。CPU優位／GPU優位を区別。
- フレーム時間予算（例：16.6ms@60fps / 4.16ms@240fps）に対する各区間の割合を出す。
- **Claude連携**：最遅区間のコードを読ませ、`Project/CLAUDE.md` のパフォ指針（描画コール削減・定数バッファまとめ・SRVバッチ化・ヒープ確保削減・SIMD・`std::move`・コンパイル時計算 等）に沿った**最適化候補**を出させる。
  - v1：提案まで（人が採否を判断）。将来：`/prompt` 経由で「修正→ビルド→PR」まで自動化（SUNDAYの自動修正トラックと共通）。

---

## 6. ImGui リアルタイム表示

- `USE_IMGUI` ガード。`ImGuiManager` に PEPPER ウィンドウを追加。
- 表示内容：
  - 区間別テーブル（名前 / CPU ms / GPU ms / calls / 親子インデント）
  - フレーム時間の折れ線グラフ（直近Nフレーム）
  - CPU/GPU の内訳バー、予算超過区間のハイライト
- ライブ計測なので「今どこが重いか」を即確認 → profile.log と合わせてオフライン解析。

---

## 7. 実装順（依存関係）

```
1. profile.log カテゴリ追加（SessionLogger に Profile を1つ足す）
2. CPUプロファイラ：ProfileScope(RAII) + PEPPER_SCOPE マクロ + Profiler シングルトン
   → Framework / SceneManager / 主要マネージャーに計装。フレーム末に profile.log へ
3. ネスト/階層集計（区間スタック、depth・calls 集約）
4. GPUタイムスタンプクエリ（QueryHeap + Resolve + 1〜2フレーム遅延リードバック）
5. ImGui リアルタイム表示
6. Python：profile.log → 最遅区間TopN抽出（file:line 提示）
7. Claude連携：最遅区間のコードを読んで最適化提案（将来：修正→PR）
```

各ステップは「ビルド通過」を関門に、小さくコミットして進める。Release で完全無効化（マクロ空展開）を常に確認する。

---

## 8. 共通設計メモ

- **第0層共用**：ログ基盤（SessionLogger / レベル制 / 1セッション1フォルダ）をそのまま使う。profile.log は他カテゴリ（state/event/input…）と同じセッションフォルダに並ぶので、SUNDAYの異常ログと**時刻で突き合わせ**できる（「カクついた瞬間に何が重かったか」）。
- **既存KPIログとの関係**：`Framework` の起動時KPI（loadTime/VRAM/RAM/CPU、1回出力）は"起動時スナップショット"。PEPPERは"毎フレーム継続計測"で役割が違う。両立。
- **ビルド構成**：Debug/Development では計測ON、Release ではマクロ空展開で完全OFF。必要なら専用の `PEPPER` 構成 or `USE_PEPPER` マクロを切る。
- **決定論との両立**：計測はゲーム状態に触れないので、SUNDAYの記録/リプレイと同時に走らせても再現性を壊さない。
- **他ツールとの関係**：profile.log とログ基盤は H.A.P.P.Y.（クラッシュ報告）/ SUNDAY（異常検知）と同じ土台。最適化提案の自動化はSUNDAYの自動修正トラックと共通の `/prompt` 経路を使う。

---

## 9. 実装着手メモ（2026-06-14 設計確定）

別PCで**エフェクトエディタのゲームからの独立**（`GameEngine/Graphics/Effect/` 一帯＋Effect系ImGuiウィンドウ）が並行進行中。**主戦場が分離しているので並行可能**と確認済。実当たりするのは ①ImGuiManager（PEPPERのImGui表示を後回しで回避）②vcxproj/filters（新規ファイル追記で必ず当たるが手で解消）の2点のみ。コア計測部は別領域。

### 確定事項
- **ブランチ**: master から `Engine_PEPPER_00` を新規に切る。
- **有効化**: 専用マクロ **`USE_PEPPER`**。`CG2_0_1.vcxproj` の **Debug / Development 構成のみ** `PreprocessorDefinitions` に `USE_PEPPER` を追加、**Release には入れない**。`PEPPER_SCOPE`/`PEPPER_GPU_SCOPE` は `#ifdef USE_PEPPER` で本体、未定義時は `((void)0)` に空展開（オーバーヘッドゼロ）。SUNDAY の `_DEBUG` とは独立に制御。
- **配置**: 新規フォルダ **`GameEngine/Profiling/`** に `Profiler.{h,cpp}` ＋ マクロヘッダ（`PEPPER_SCOPE`/`PEPPER_GPU_SCOPE` 定義。`__FILE__`/`__LINE__` をトークン連結で一意名に）。`CG2_0_1.vcxproj` と `.filters` に `<ClCompile>`/`<ClInclude>` 追記（→ユーザーへ報告）。
- **SessionLogger 連携**: `SessionLogger::Category` に **`Profile`** を1個追加（`Session` の後、`kCount` の前）→ `profile.log`。レベルは `Trace`（高頻度。`USE_PEPPER` ビルドでのみ書き出し）。`SessionLogger.h` の enum へ1行＋`SessionLogger.cpp` のファイル名テーブルへ1行。**これがエフェクトエディタ作業との唯一の共有ファイル接点（1行追加なので競合ほぼ無し）**。
- **計装対象（v1）**: `Framework::Update/Draw` → `SceneManager::Update/Draw` → 主要マネージャー（Sprite / Object3D / Particle / Skybox / Text / Line）の `Update/Draw`。**Effect系は計装しない**（エフェクトエディタと棲み分け。独立完了後に追加検討）。各所は `PEPPER_SCOPE("名前")` を関数先頭に1行入れるだけ＝差分が局所的でmerge衝突しにくい。

### 実装順（ImGuiを後ろへ組み替え）
計画書 §7 の `1→2→3→4→5→6→7` を、衝突回避のため **`1→2→3→4→6→(エフェクトエディタmerge後)5→7`** に変更：
1. `profile.log` カテゴリ追加（SessionLogger に `Profile`）
2. CPUプロファイラ（`ProfileScope` RAII ＋ `PEPPER_SCOPE` マクロ ＋ `Profiler` シングルトン）→ Framework/SceneManager/主要マネージャーに計装、フレーム末に profile.log へ
3. ネスト/階層集計（区間スタック、`depth`・`calls` 集約）
4. GPUタイムスタンプクエリ（`DirectXCore` に QueryHeap ＋ Resolve ＋ 1〜2フレーム遅延リードバック）
6. Python：profile.log → 最遅区間TopN抽出（`file:line` 提示）。`tools/Python/` に新規（vcxproj不要）
   — ここまでで profile.log 計測＋オフライン解析が完成。**ImGuiManager に一切触らずに走り切れる**
5. ImGui リアルタイム表示（**エフェクトエディタ独立が master に merge されてから着手**）
7. Claude連携（最遅区間のコードを読んで最適化提案。将来：修正→PR）

各ステップ「ビルド通過」を関門に小さくコミット。Release で空展開＝完全OFFを毎回確認。

### 新規チャットでの始め方
1. このPCで master を最新化（エフェクトエディタが merge されていれば pull）→ `Engine_PEPPER_00` を切る。
2. ステップ1（SessionLogger に `Profile` カテゴリ）から着手。新規 `Profiler.{h,cpp}` 作成時は配置・命名を最終確認のうえ vcxproj/filters 追記を報告（Project/CLAUDE.md ルール）。
