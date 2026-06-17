# P.E.P.P.E.R. 引き継ぎメモ（別PCで続きから）

最終更新: 2026-06-17（学校行き前の中断点） / ブランチ: `Engine_PEPPER_00`

このファイルは git に乗るので、別PC（学校）で `pull` すれば中断点から再開できる。
設計の全体像は同ディレクトリの `P.E.P.P.E.R..md`（§9に確定事項）を参照。

---

## いま立っている場所（要点）

PEPPER（自前プロファイラ）は **計測〜オフライン解析まで動作する状態**。実測で
「約229fps・VSync律速で定常ボトルネックなし」と判明済み。

- **コミット済（ただし push 未）**: Step1〜4・6・7
  - 区間別 CPU/GPU 時間、階層(depth)、DrawCall カウント、1秒ウィンドウ集計で `profile.log` 出力
  - GPU=D3D12 タイムスタンプ（EndDraw の既存 GPU 完了待ちに相乗り）
  - `GpuWait/Present` を分離計測（= Framework::Draw の大半は CPU が VSync を待つ遊び時間だと判明）
  - Python 解析 `Project/tools/Python/analyze_profile.py`（最遅区間TopN＋無駄あぶり出し＋`--json`）
- **未コミット・未ビルド検証（= 段階1+2、下記）**: メモリ/VRAM/オブジェクト数の継続計測
- **次にやる = 段階3: ImGui リアルタイム表示（= 計画書 Step5 本体）**

---

## 別PCで再開する手順

1. **このPC（自宅）側で先にやること**（未実施なら学校に行く前に）:
   ```
   git add -A
   git commit -m "PEPPER: メモリ/VRAM/オブジェクト数の継続計測(段階1+2) ※未ビルド検証"
   git push -u origin Engine_PEPPER_00
   ```
   ※ `Engine_PEPPER_00` はまだ origin に push されていない。`-u` で初回 push。

2. **学校PC側**:
   ```
   git fetch origin
   git switch Engine_PEPPER_00      # 無ければ git switch -c Engine_PEPPER_00 origin/Engine_PEPPER_00
   git pull
   ```

3. **最初のビルド確認**（段階1+2 はまだビルドしていない！）:
   ```
   msbuild Project\CG2_0_1.sln /p:Configuration=Debug /p:Platform=x64 /m
   ```
   - ビルドが通ったらゲームを少し動かし、解析:
     ```
     python Project\tools\Python\analyze_profile.py
     ```
   - 「メモリ/リソース（ゲージ）」欄に `RAM_WS_MB / VRAM_MB / Obj3D_Count / Tex_Count …` が出れば段階1+2 成功 → コミット。

---

## 段階1+2（未コミット分）の中身

**段階1: メモリ/VRAM 継続計測**
- `Profiler` に「ゲージ」概念を追加（`SetGauge(name, value)`。合計でなく現在値。ウィンドウごとに cur/min/max/avg）。
- 新規 `GameEngine/Profiling/MemoryProbe.{h,cpp}`（vcxproj/filters 追記済）: RAM(WS/Private)＋VRAM を **1秒に1回 OS 問い合わせ**、ゲージは毎フレーム更新。
- `PepperMacros.h` に `PEPPER_GAUGE` / `PEPPER_SAMPLE_MEMORY`。`Framework::Run` ループで毎フレーム `PEPPER_SAMPLE_MEMORY()`。

**段階2: オブジェクト/リソース数カウント**
- `TextureManager::GetLoadedTextureCount()` / `ModelManager::GetModelCount()` を追加。
- `Scene::ReportProfileGauges()` がシーン内オブジェクト数(Obj3D/Anim/Sprite/Prim)＋リソース数(Tex/Model)をゲージ報告。`SceneManager::Update` から毎フレーム呼ぶ。
- `PEPPER_GAUGE` は USE_PEPPER 未定義時に引数を評価しない空展開 → Release はカウント取得すら走らない。

**未コミットのファイル一覧**:
- 新規: `Profiling/MemoryProbe.{h,cpp}`
- 変更: `Profiler.{h,cpp}` `PepperMacros.h` `Framework.cpp` `Scene.{h,cpp}` `Game/Scene/SceneManager.cpp` `Graphics/TextureManager.h` `Graphics/Object3D/ModelManager.h` `EngineCore.vcxproj(.filters)` `tools/Python/analyze_profile.py`

---

## 次の作業 = 段階3: ImGui リアルタイム表示（計画書 Step5）

Unity プロファイラ参考。`ImGuiManager` に PEPPER ウィンドウを追加し、`USE_IMGUI` ガード。
表示したいもの（計測層はすでに全部ある）:
- 区間別テーブル（名前 / CPU ms / GPU ms / calls / 親子インデント=depth）
- フレーム時間の折れ線（直近Nフレーム）
- DrawCall 数、メモリ(RAM/VRAM)、オブジェクト/リソース数（ゲージ）
- 予算超過のハイライト

実装メモ:
- ライブ表示は `Profiler` から「直近ウィンドウの集計」か「直近フレーム値」を読む getter が要る（今は profile.log へ書くだけで、外から読む API が無い）。まず `Profiler` に最新集計を保持＆公開する getter を足すところから。
- `USE_IMGUI` と `USE_PEPPER` の二重ガードに注意（Release/ImGui無効時に壊れないこと）。
- `ImGuiManager` は EngineCore 所属。PEPPER ウィンドウ追加で merge 衝突懸念は無し（並行作業は解除済み）。

---

## 重要な恒久メモ
- エンジン汎用コードは `EngineCore.vcxproj` に集約。**PEPPER 新規ファイルは EngineCore に追加**（`CG2_0_1.vcxproj` ではない）。追記したら報告するルール。
- `USE_PEPPER` は `Common.props`（EngineCore と CG2_0_1 が共有）の Debug/Development のみ。Release 未定義＝空展開でオーバーヘッドゼロ。
- ログ出力先は `Project/Logs/<セッション>/profile.log`。
