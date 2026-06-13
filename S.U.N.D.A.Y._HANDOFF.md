# 🤖 S.U.N.D.A.Y. 引継ぎ資料（作業継続用）

最終更新: 2026-06-13。設計の全体像は `S.U.N.D.A.Y..md`、不変の方針はメモリ（`MEMORY.md` / `project_sunday.md`）。この資料は「いま何が動いていて、次に何をするか」をまとめた作業継続用。

---

## 0. これは何か

**S.U.N.D.A.Y.** = 無人でゲームを自動操作し、クラッシュ等の異常を検知 → 再現性確認 → ローカルLLM(Ollama)で原因究明 → GitHub Issue起票、までやる自律検証システム。`J.A.R.V.I.S.` ファミリーの一員。STG（レール式シューティング）が対象ゲーム。

**実装順**: 第0層C++(1.ログ基盤→2.イベント計装→3.中央乱数→4.記録/リプレイ→5.クラッシュダンプ) → Python(6.ランダム入力+異常検知→7.再現性判定→8.原因究明+Issue起票→9.JARVIS統合)。

---

## 1. 完了済み（すべて動作確認済み）

### 第0層 C++（エンジン側）
- **1. ログ基盤**: `GameEngine/Utility/SessionLogger.{h,cpp}`。`Logs/YYYY-MM-DD_HHMMSS/` に6カテゴリ(input/state/event/gfx/error/session)・6レベル。`_DEBUG`→TRACE全開/Release→Criticalのみ。`Framework::Initialize` 先頭で初期化、`Finalize` で閉じる。
- **2. イベント計装(event.log)**: `SCENE_CHANGE`=`SceneManager`、`GAMEOVER`=`StagePlayScene`(player IsDead付近)、`MOVE_BLOCKED`=`StagePlayScene` のクリップ押し戻し(軸ごと立ち上がりエッジのみ)。state.logも `StagePlayScene::Update` 末尾で毎フレーム `frame= x= y= z= hp= scene=` を出力。
- **3. 中央乱数**: `GameEngine/Utility/RandomGenerator.{h,cpp}`(mt19937シングルトン)。`Framework` で `--seed N`>random_device でシード決定→session.logに記録。ゲーム挙動の乱数は `WanderInScreenCommand.h` のみ→置換済。
- **4. 記録/リプレイ**: `GameEngine/Core/ReplaySystem.{h,cpp}`。Record=`Framework::Update`末尾で `RecordFrame`(dt+生入力をinput.logへ)。Replay=`--replay <dir>` で input.log を全行パース→各入力デバイスの `ApplyReplay` で注入＋`DirectXCore::OverrideDeltaTime`。`DirectXCore::UpdateFixFPS` はreplay時dtを上書きせずペーシングのみ。seedはsession.logから復元。
- **5. クラッシュダンプ**: `GameEngine/Utility/CrashHandler.{h,cpp}`。`SetUnhandledExceptionFilter`(C2712回避&全スレッド&無人向き)。`MiniDumpWriteDump`で `crash.dmp`、**8aで `crash_stack.txt`(シンボル付きスタック)も出力**(dbghelp StackWalk64+SymFromAddr+SymGetLineFromAddr64、flush付き)。`main.cpp`で`Install()`、SessionLoggerが`SetDumpDir`通知。

### Python（SUNDAY本体、`Project/tools/Python/`）
- **6+7. `sunday.py`**: 起動→起動待ちゲート(state.logのframeがREADY_FRAMES=30到達まで検知しない、STARTUP_TIMEOUT=60s)→**仮想Xboxコントローラ(vgamepad)** でランダム操作(左スティック移動重め+A/B/X/Y/LB/RT、Title/HubはAで前進)→event/state.log追尾→異常検知(CRASH/HANG/COORD_NaN/COORD_OOR/STUCK、STUCKは5sクールダウン)。CRASH時に `reproduce_crash`(同セッションを`--replay`で3回起動し再現側にcrash.dmpが出るか)→`report_reproduce`がREPRODUCIBLE/FLAKY/NOT_REPRODUCED判定→REPRODUCIBLEなら `crash_analyze.analyze_crash` を呼ぶ。結果は各セッションの `anomalies.jsonl`。
- **8a+8b. `crash_analyze.py`**: crash_stack.txtをパース→#0(落ちた行)のソースだけ抜粋(±12行)+落ちた行を引用+例外コードの意味+「null/UAF/範囲外から断定」「if(ptr)でnull弾いてるならUAF」プロンプトで Ollama(`qwen2.5-coder:14b`, localhost:11434) に原因究明させる。プロンプト調整で実バグ(下記)を正しく当てられることを確認済。材料生成は `_build_material()` に切り出し Haikuと共有。
- **8b-2. Haikuダブルチェック(`crash_analyze.double_check`)**: 同じ材料＋Ollamaの分析を Claude CLI(`claude.exe -p --model haiku`, `find_claude()`流用, `--permission-mode`無し)に渡し独立判定。1行目 `VERDICT: AGREE|DISAGREE|OLLAMA_LOCATION_WRONG` を正規表現でパース(取れねば`UNKNOWN`)。`save_double_check`が`anomalies.jsonl`に`type=DOUBLE_CHECK`(verdict+ollama+haiku)で記録。
- **8c. `github_report.py`(新規)**: REST API(urllib)で `shun-takamura/CG2` のIssueを操作(`gh`不使用)。署名 `<!-- SUNDAY-SIG: CRASH|<シーン> -->` でOpen Issue検索→あればコメント追記/無ければ新規(重複防止)。本文=種別/シーン/座標/seed+再現コマンド(`--replay <dir> --seed N`)+crash_stack要点+Ollama分析+Haiku verdict+dmp/logパス。`GITHUB_TOKEN`(.env, fine-grained PAT: Issues=Read and write)無ければ黙ってスキップ。`report_crash(finding, analysis, check)`。**実起票まで動作確認済(2026-06-13)**。`python github_report.py Logs\<日時>` でdry-run、`--post`で実起票。
- **接続(`sunday.py` main)**: REPRODUCIBLE時に analyze_crash → save_analysis → double_check → save_double_check → `github_report.report_crash`。

---

## 2. 実行方法・前提

- **Debugビルド必須**(crash_stack.txtとTRACEログのため、`.pdb`も)。新規ファイル追加済みなので**リビルド**。
- **Ollama serve 起動 + `qwen2.5-coder:14b` pull済み**。
- **`pip install vgamepad` + ViGEmBus ドライバ**(初回だけ登録でエラーが出るが2回目以降OK)。
- 起動: `Project/tools/Python/` で `python sunday.py`(Ctrl+Cで停止)。
- 再生確認: ゲーム起動引数 `--replay Logs/<日時>`(VS: プロジェクト>プロパティ>デバッグ>コマンド引数)。デバッガ下(F5)だとクラッシュ捕捉フィルタが走らない→`Ctrl+F5` かSUNDAY経由で。
- exeパス: `Generated/Output/Debug/CG2_0_1.exe`(`test_run.GAME_EXE`)。CWD=`Project/`。

---

## 3. 次のタスク（ここから再開）

**ステップ8・9とも実装完了。全工程（C++計装→Python検知→再現→原因究明→ダブルチェック→Issue起票→Discord遠隔操作）が一通り繋がった。残りは実セッションでの通しソーク検証と運用調整。**

### 実装済（要・実セッション通し検証）
- **9: JARVIS統合(2026-06-13実装)**: `jarvis_bot.py` に `/sunday_start` `/sunday_stop` 追加。`sunday.run(stop_event, on_issue)` を **daemonスレッド**で回す。`sunday.py` は `main()`→`run()` に分離、`run_session`/`reproduce_crash` に `stop_event` を通し稼働中セッション/再生も速やかに畳める(クラッシュ解析中=Ollama/Haikuのみ即停止不可→現処理後に停止)。Issue起票時 `on_issue(issue_no, finding)` → `asyncio.run_coroutine_threadsafe` でDiscordへ「Issue #N」通知(起動チャンネル宛)。同時稼働は1つ(`_sunday_thread`/`_sunday_stop` で管理)。`/sunday_stop` は `stop_event.set()`＋60s join、解析中なら「現処理後に停止」と返す。

### 次にやること
- **実セッション通しソーク検証**: 自宅PCで `python jarvis_bot.py` → スマホDiscordで `/sunday_start` → 温存バグ(下記)でクラッシュ→再現→Ollama+Haiku→Issue起票→Discord通知、までの一気通貫を実機確認。複数クラッシュで同シーンが**同一Issueにコメント集約**されるか、`/sunday_stop` の停止応答も確認。
- 運用調整候補: STUCK実効化(画面内オフセットをstate.logへ／要C++小改修・要相談)、ソーク中のGPU検証コスト、FLAKY/NOT_REPRODUCEDの扱い、起票対象を広げるか。

---

## 4. 既知の事項・注意

- **温存中の実バグ(直さない)**: ImGui「Highlight一覧」パネル `ImGuiManager.cpp:214` が `scene->GetHighlights()` の**ダングリング(use-after-free)生ポインタ**を参照しAV(0xC0000005)。`if(e)`はnullしか弾けない。対象敵を highlight(StagePlayScene.cpp:937)→破棄でRemoveHighlight漏れ。**HAPPYのクラッシュレポートのテストケースとして意図的に残す**。SUNDAYの検知→再現→原因究明はこれで実証済。
- **STUCK検知の限界**: state.logはワールド座標(GetTranslate)を見るが、レール式STGは前進で座標が常に動く→STUCKがほぼ発火しない。実効化には `playerInputOffset_`(画面内オフセット)をstate.logに足すC++小改修が必要(要相談)。
- **GPU-Based Validationのクラッシュは根治済**: 正体はImGuiフォントのクロスキュー問題。`ImGuiManager.cpp` を新`InitInfo`版Init+`CommandQueue=dxCore_->GetCommandQueue()`(getter追加済)に変更、`RendererHasTextures`はoffで旧挙動維持。
- **可変フレームレート化済**: `DirectXCore.h` `useFixedFrameRate_=false`(VSyncで上限=モニタ)。フレーム依存はFade/StripeTransitionの`+=1/60`を`dt`化して対応済。dt列を記録しているのでリプレイ決定論は維持。
- **14bの限界**: qwen2.5-coder:14bはスタック追跡が不安定でプロンプト調整が要った。Haikuダブルチェックはこの精度backstop。

---

## 5. 並行作業（衝突注意）

- **学校でPEPPER実装中**(`P.E.P.P.E.R..md` 計画書あり)。profile.logはSessionLoggerに`Profile`カテゴリ追加で乗る。
- **エンジンのライブラリ化リファクタが別途進行**。守るべき契約: ①ログ形式 ②`--replay`/`--seed` 引数 ③`ReplaySystem::RecordFrame`(Update末尾) ④exe出力パス。SUNDAYのエンジン部品(SessionLogger/RandomGenerator/ReplaySystem/CrashHandler)は汎用なのでエンジンlib側に入れる。SUNDAYの残りはPythonなので衝突最小。ブランチ分離推奨。

---

## 6. 作業の流儀（守ること）

- **コマンドは代理実行せず解説する**(ユーザーが打つ)。ビルド検証もユーザー依頼。
- **Worktreeを使わずリポジトリルートで直接編集**。
- **C++新規ファイルは事前相談 → 作成後 `CG2_0_1.vcxproj` と `.filters` に `<ClCompile>`/`<ClInclude>` 追記し報告**。Python新規(tools/Python)はvcxproj不要。
- コメントは日本語。命名はPascalCase/メンバ末尾`_`。
- 返信は簡潔・日本語。
