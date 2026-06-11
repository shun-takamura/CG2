# 🤖 S.U.N.D.A.Y. 実装計画書

**S**imulated **U**nattended **N**avigation **D**ebugger & **A**nomaly **Y**ielder
~ 人間が休む間、無人でゲームを遊び倒してバグを暴く自律検証システム ~

---

## 0. 位置づけ（J.A.R.V.I.S. ファミリー）

司令塔 **J.A.R.V.I.S.**（Just A Remote Visual-Studio Infrastructure System）の下に、4つの専門ツールがぶら下がる。

```
                  [ スマホ (Discord) ]
                          │
              【司令塔：J.A.R.V.I.S.】
                          │
   ┌──────────┬───────────┬──────────┐
   ▼          ▼           ▼          ▼
S.U.N.D.A.Y. P.E.P.P.E.R. G.U.N.D.A.M. H.A.P.P.Y.
(自動検証)   (性能計測)   (手動操作)   (配布ランチャー)
```

| ツール | フルネーム | 役割 |
|---|---|---|
| **S.U.N.D.A.Y.** | Simulated Unattended Navigation Debugger & Anomaly Yielder | 自動プレイでバグ検知・再現・起票 |
| **P.E.P.P.E.R.** | Proactive Evaluation & Profiling for Performance-Efficiency Recommendation | ボトルネック特定・最適化提案 |
| **H.A.P.P.Y.** | Hardened Asset Provisioning & Protection Yield-launcher | 環境チェック・更新・整合性・配布 |
| **G.U.N.D.A.M.** | Gamepad-driven User Network Direct Active Manipulator | 手動リモート操作・マクロ |

**このファイルの範囲**：SUNDAY本体 ＋ 全ツールが共用する**第0層（エンジン共通基盤）**。基盤はSUNDAYが最も多く必要とするため、ここで定義し最初に作る。PEPPER/HAPPY/GUNDAMは各自の `名前.md` を作り、本書の第0層を参照・共用する。

**前提（合意済み）**：当面は**完全C++/Python**で作る（C#移行は将来の別トラック）。レポート先は開発者向け=GitHub Issues、エンドユーザー向け(HAPPY)=Googleフォーム。リポジトリはpublic。

---

## 1. 目的と合格基準

**目的**：移動時間などに自宅PCで無人稼働し、ゲームを自動操作してクラッシュ・スタック等の異常を検知。再現性を確認し、開発者へGitHub Issueとして報告する。

**合格基準**：SUNDAYを起動すると、ゲームを自動操作し、異常を検知したら直前の入力ログから再現を試み、**再現すればGitHubにIssueが自動で立つ**。

**スコープ**：v1は**レポートまで**。自動修正（Claude/Ollama連携）は将来拡張。

---

## 2. 全体構成

```
J.A.R.V.I.S.(Bot) ── /sunday_start ──► SUNDAY(Python・自宅PC常駐)
                                          │
        ┌─────────────────────────────────┤
        ▼                                 ▼
  ゲーム.exe を起動・自動入力        エンジンのログをリアルタイム追尾(tail)
  (pydirectinput / リプレイ)              │
                                          ▼
                                  異常検知 → 再現判定 → GitHub Issue起票
```

SUNDAYはJ.A.R.V.I.S.から起動される別Pythonスクリプト（既存の `ai_edit`/`git_pipeline` と同様、Botが親=オーケストレーター）。

---

## 3. 第0層：エンジン共通基盤（C++）

全ツールの土台。SUNDAYはこのうちプロファイラ以外をすべて使う。

### 3.1 ログ基盤
- **形式**：1行1レコード（JSONにせず、人間もAIも読みやすいテキスト）。
  例：`<時刻> <カテゴリ> <レベル> key=val key=val ...`
- **カテゴリ別ファイル**：
  - `input.log` … 時刻 / アクション名 / 押下・解放
  - `state.log` … 時刻 / フレーム / 座標(x,y,z) / HP / シーン
  - `event.log` … 時刻 / 種別 / 詳細（後述のゲームイベント）
  - `gfx.log` … DirectX警告・デバイスロスト等
  - `error.log` … 一般エラー
  - （`profile.log` はP.E.P.P.E.R.で追加）
- **1セッション1フォルダ**：`Logs/YYYY-MM-DD_HHMMSS/` 配下に各ファイル。クラッシュとログを紐付けやすくする。
- **レベル制**：`CRITICAL / ERROR / WARN / INFO / DEBUG / TRACE`。カテゴリごとに「出力する最低レベル」を設定。
  - Debug/SUNDAYビルド → TRACE（全開）
  - Release/配布版 → CRITICALのみ（パフォーマンス優先）
- **現状**：`DirectXGame/Debug` にVS出力ウィンドウ向けの最低限ログあり。**ファイル出力＋カテゴリ分割＋レベル**を新規構築。

### 3.2 ゲームイベント計装（event.log）
- `DEATH` / `GAMEOVER` / `SCENE_CHANGE` … セッション制御に使う。
- **`MOVE_BLOCKED`** … 移動が衝突で阻止された瞬間に出力。**SUNDAYのスタック判定の核心**（壁＝正常 と 謎の固まり＝バグ を区別する材料）。

### 3.3 決定論・リプレイ
- **中央乱数**：ゲームに関わる乱数を**1つの `RandomGenerator`（std::mt19937）に集約**。コードは常にGeneratorを呼ぶ（シードファイルを直接読まない）。
  - 起動時にシード生成 → **ログに記録**。
  - **シード注入口**（起動引数 or 設定）でリプレイ時は同じシードから開始。
  - ⚠️ `std::random_device`・現在時刻・GPU乱数などシード外の源をゲーム挙動に使わない（GPU乱数は見た目専用に限定）。
- **時間源の差し替え**：通常=実時計（delta time）、リプレイ=**記録したdt列**を順に供給。これでデルタタイムのまま再現性を確保。
- **入力記録/再生**：`input.log` がそのまま記録。リプレイは **`input.log` + dt列をエンジンが読んで再生**（外部pydirectinputでなく**エンジン内再生**＝決定論を保つ。G.U.N.D.A.M.のマクロ再生にも共用）。
- **データ版数（将来）**：外部チューニングデータを使い始めたら、セッションが使ったデータのハッシュをログ記録し、再生時に同一データを保証する。

### 3.4 クラッシュダンプ
- `SEH(__try/__except)` + `MiniDumpWriteDump` でクラッシュ時に `.dmp` 生成。`.pdb`（シンボル）を保管し原因箇所を辿れるようにする。**H.A.P.P.Y. のクラッシュ報告とも共用**。

---

## 4. S.U.N.D.A.Y. 本体（Python）

### 4.1 起動・制御
- J.A.R.V.I.S.に `/sunday_start` `/sunday_stop` を追加。SUNDAYは別スクリプト、Botがオーケストレート。

### 4.2 入力戦略
- **キーコンフィグのゲーム関連アクションのみ**からランダム選択（移動中心の重み付け可）。将来のポーズ/メニュー追加に対応できる設計。
- `pydirectinput` で送出（自動プレイ）。
- 1セッション約2分（現STGの長さ）。**死亡/ゲームオーバー（event.log）検知でexeを再起動**して最初から。

### 4.3 異常検知（ログをリアルタイム追尾）
- **ハード異常（無条件でバグ確定）**：
  - プロセス異常終了・クラッシュ（`.dmp`生成）
  - ハング（N秒 `state.log` が更新されない）
  - 座標がNaN／範囲外／床下（閾値判定）
- **ソフト異常（候補・要確認）**：
  - 入力「移動」ありなのに座標がN秒変化せず、**かつ `MOVE_BLOCKED` イベントが無い** → スタック候補。
  - （`MOVE_BLOCKED` があれば壁＝正常として無視）

### 4.4 再現性判定
- 異常検知時、**直前の入力 + dt列 + シード**を取り出し、エンジンの**リプレイモードでM回再生**。
- 毎回再現 → 再現性ありバグ（高優先）／たまに → フレーク／再現せず → 環境・タイミング依存として情報付きで記録。

### 4.5 レポート（GitHub Issues）
- **署名**（異常種別＋発生シーン＋座標等）で既存Issueを検索 → 同一あれば**コメント追記**、無ければ**新規起票**（重複防止）。
- 内容：発生条件（シーン・座標・直前入力）／何が起きたか／**再現手順（シード＋入力ログ添付）**／`.dmp`／（可能なら）原因箇所の手がかり。
- **GitHub PAT を `.env`（自宅PCのみ・配布しない）**。
- Discordには「Issue #N 起票」の**通知のみ**（重要情報が流れるのを防ぐ）。

### 4.6 自動修正（将来拡張）
- v1はレポートまで。将来は `/prompt`（Claude/Ollama）へ流して「修正→ビルド→PR」まで自動化。

---

## 5. 実装順（依存関係）

```
[第0層・エンジンC++]
 1. ログ基盤（input/state/event + レベル + 1セッション1フォルダ）
 2. ゲームイベント計装（DEATH / GAMEOVER / SCENE_CHANGE / MOVE_BLOCKED）
 3. 中央RandomGenerator + シード記録/注入
 4. 時間源差し替え + 入力記録/リプレイ（エンジン内再生）
 5. クラッシュダンプ（SEH + MiniDump）
[SUNDAY・Python]
 6. ランダム入力 → ログ追尾 → 異常検知
 7. リプレイによる再現性判定
 8. GitHub Issue起票（署名・重複防止）
 9. J.A.R.V.I.S.統合（/sunday_start・/sunday_stop）
```

各ステップは「ビルド通過」を関門に、小さくコミットして進める（既存の鉄壁Git運用を踏襲）。

---

## 6. 他ツールとの関係（各自のファイルで詳細化）

- **P.E.P.P.E.R..md**：自前プロファイラ（CPU=QueryPerformanceCounter / GPU=D3D12タイムスタンプクエリ）→ `profile.log`（区間・時間・`__FILE__/__LINE__`）→ Pythonが最遅区間抽出 → Claudeがコードを読んで最適化提案。ImGuiでリアルタイムGUI表示も。第0層のログ基盤を共用。
- **H.A.P.P.Y..md**：C++単体exe（静的CRT）。環境チェック（DX12/必須DLL）・整合性チェック（SHA-256 manifest照合）・GitHub Releasesからの更新と修復・クラッシュ報告（Googleフォーム→スプレッドシート）。第0層のクラッシュダンプを共用。配布は全部入りパッケージ。
- **G.U.N.D.A.M..md**：手動リモート操作。(a) **Tailscale + FastAPI Webコントローラ**でリアルタイム操作（遅延許容）、(b) **マクロ**＝入力ログ由来プリセットのエンジン内再生 ＋ `/test_run` 風の簡易送信（二刀流）。第0層のリプレイ機構を共用。

---

## 7. 共通の設計メモ
- 報告の使い分け：開発者向け(SUNDAY/PEPPER)=GitHub Issues、エンドユーザー(HAPPY)=Googleフォーム。
- 秘密情報（DISCORD_TOKEN / GitHub PAT / OBS_WS_PASSWORD）は自宅PCの `.env` のみ。配布物には絶対に含めない。
- C#移行・ホットリロード・PIX深掘り・GPU乱数のゲーム利用・アンチチートは、いずれも**将来の別トラック**として本計画から切り離す。
