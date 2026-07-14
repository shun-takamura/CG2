# 🎮 G.U.N.D.A.M. 実装計画書（確定版）

**G**amepad-driven **U**ser **N**etwork **D**irect **A**ctive **M**anipulator
~ スマホから自宅PCの自作エンジンを直接操作し、input-to-photon 遅延を自前計測する有人遠隔操作システム ~

**確定日: 2026-07-14**（Geminiラフ案を実コードと突き合わせて再設計した確定版）

---

## 0. 位置づけ（J.A.R.V.I.S. ファミリー）

司令塔 **J.A.R.V.I.S.** の下にぶら下がる4ツールの1つ。本書は G.U.N.D.A.M. 本体と、その相棒である **P.E.P.P.E.R. の Live/Net 拡張**（`P.E.P.P.E.R..md §10`）を規定する。

```
S.U.N.D.A.Y. P.E.P.P.E.R. G.U.N.D.A.M. H.A.P.P.Y.
(自動検証)   (性能計測)   (手動操作)   (配布ランチャー)
```

| ツール | 役割 |
|---|---|
| **G.U.N.D.A.M.** | スマホの仮想コックピットから自作エンジンを遠隔直接操作。入力→表示のグラス・トゥ・グラス遅延を自前計測 |

---

## 1. 設計の背骨（レビュー指摘への回答）

> 指摘: 「Discord 既存 API に依存した構成は、革新性とプログラム実務の深掘りが足りない」

**構造的回答:**

1. **操作＆計測のフロントを Discord から自作 Web コックピット（Tailscale + FastAPI/WebSocket）へ移す。** サードパーティAPI非依存。
2. **OS任せの入力エミュ（SendInput/pydirectinput）をやめ、自作エンジンのフレームループに直接ネットワーク入力を差し込む。** これにより「受信(T2)」「表示(T3)」をエンジン内部で刻め、**input-to-photon 遅延を自前で分解計測**できる。中間APIを叩くのではなく、自分のレンダーループを計装する点が唯一性。
3. **Discord（＝J.A.R.V.I.S.）は非同期devops（ビルド/リファクタ/SUNDAY起動）に役割限定**して残す。責務分離。

**ポートフォリオの語り:** 「ゲーム業界ミドルウェア無しで、自作エンジン向けの低遅延リモートプレイ＋グラス・トゥ・グラス遅延テレメトリを自作した。自分のレンダーループを計装して input-to-photon をmsで分解した」。

---

## 2. 実コードとの突き合わせ（是正した5点）

| # | Gemini ラフ案 | 実コードの現状 | 確定判断 |
|---|---|---|---|
| ① | `pydirectinput`/`SendInput` でOS注入 | `Project/tools/Python/sunday.py` は既に **`vgamepad`(ViGEmBus 仮想XInput)** を使用。フォーカス非依存・アナログ・`gp.*` バインド一致 | pydirectinput は**不採用**（退化）。予備経路は vgamepad を流用 |
| ② | T1→T2→T3→T4 を各所計測 | T2/T3（受信/表示）を測る口が**存在しない**。OS注入では原理的に不可 | **エンジンにネットワーク入力口を新設**し、フレームループ内で T2/T3 を刻む（本設計の目玉） |
| ③ | 素朴に $T_4-T_1$ 等で遅延算出 | — | **クロックドメイン跨ぎの引き算は不正**。同一時計内でしか引かない方式に是正（§6） |
| ④ | FPS/エンコード/帯域を新規サンプリング | **OBS WebSocket が `live_control.py` で統合済**（`obsws_python`）。`GetStats` が render/encode/skippedframes/bitrate を返す | 新規実装せず **OBS `GetStats` を再利用**（本物の配信数値になる） |
| ⑤ | P.E.P.P.E.R. が RTT も測る | 実 P.E.P.P.E.R. は純粋な区間CPU/GPUプロファイラ（`GameEngine/Profiling/Profiler.h`, `profile.log`, `ImGUIManager/PepperWindow.h`） | RTT系は別ツールにせず **P.E.P.P.E.R. の "Live/Net モード拡張"** として同じ `Profiler` ＋新カテゴリで統合（`P.E.P.P.E.R..md §10`） |

**最重要は②。** `Framework::Update`（`GameEngine/Framework.cpp` の Replay 分岐, 概ね L522-533）には既に **`ReplaySystem` が「ハードを読まずデバイスへ入力を注入する」フック**が存在する。ネットワーク入力はこの機構の“ライブ版”として差し込めば、既存の `InputAction` 経路をそのまま通り、決定論も壊さない。

---

## 3. 全体アーキテクチャ

```
[スマホ Safari / 仮想コックピット index.html]
   │  WebSocket（Tailscale VPN 内、wss不要）
   │  入力パケット {seq, t1=performance.now(), buttons, axes}
   ▼
[自宅PC: pepper_server.py  (FastAPI / uvicorn)]
   ├─(A 本命) ループバック UDP → エンジンの Net 入力口へ
   │         エンジンから {seq,t2,t3,cpu_ms,gpu_ms} を回収
   ├─(B 予備) vgamepad(ViGEmBus) で XInput 注入（エンジン改修不要の安全網）
   ├─ OBS WebSocket GetStats（fps/encode_ms/skipped/bitrate）← 既存流用
   └─ アドバイザ判定 → WS でスマホへ push（診断文＋メーター値）
   ▲
   │  WS でメーター/診断を返信、echo で t4 を確定
[スマホ] RTT・FPS・オーバーヘッド・アドバイザをリアルタイム描画
```

エンジン側:
```
[Net受信スレッド(WinSock2)] --SPSC lock-free ring--> [ゲームスレッド]
  UDPパケット到着                      Framework::Update 冒頭で drain
                                        ├ t2 を刻む（QueryPerformanceCounter）
                                        ├ InputManager へ注入（ReplaySystem 同経路）
                                        └ そのフレームに seq をタグ
                                      Draw → Present 完了時に t3（＋PEPPER の GPU タイムスタンプ）
                                        └ net.log 出力 ＋ サーバへ {seq,t2,t3,...} 返送
```

---

## 4. 2系統の入力パス

S.U.N.D.A.Y..md §6 の「二刀流」を具体化。

- **Path A（本命・計測可能）**: スマホWS → FastAPI → **ループバックUDP** → エンジン `NetworkInputSource`。`InputManager`/`InputActionMap` を直接叩くため **t2（受信）/t3（表示）を刻める**＝真の input-to-photon 分解が取れる唯一の経路。
- **Path B（予備・改修ゼロ）**: スマホWS → FastAPI → **vgamepad**。エンジン無改修で即動く安全網。測れるのは**ネットワークRTTとWSエコーまで**（t2/t3不可）。擬似テストモードのデフォルトにも使う。

サーバは `mode = A | B` を保持。エンジンの Net 口が生きていれば自動で A、無ければ B にフォールバック（§9 の自動再接続と同居）。

---

## 5. 通信プロトコル

WS区間はテキストJSON（スマホ実装が楽）、**PC内ループバックは固定長バイナリ**（低レイヤの見せ場）。JSONは自作方針（`dev_constraints`）とも整合。

**スマホ → サーバ (WS JSON)**
```json
{ "t":"in", "seq":1042, "t1":19233.7, "btn":10, "lx":0.0, "ly":-1.0, "lt":0.0, "rt":1.0 }
```
`btn` はボタンビット（A/B/X/Y/LB/RB…）、`lx/ly` 左スティック[-1,1]、`lt/rt` トリガー[0,1]。

**サーバ → エンジン (loopback UDP, 24B 固定)**
```
u32 magic('GNDM') | u32 seq | u16 btnBits | i16 lx | i16 ly | u8 lt | u8 rt | u64 t1_ns_server_recv
```

**エンジン → サーバ (loopback UDP, 復路)**
```
u32 magic | u32 seq | u64 t2_qpc_ns | u64 t3_present_qpc_ns | f32 cpu_ms | f32 gpu_ms | u16 present_index
```

**サーバ → スマホ (WS JSON, メーター/診断)**
```json
{ "t":"metrics", "seq":1042, "rtt_net_ms":38.2, "engine_ms":11.4,
  "fps":142, "encode_ms":3.1, "skipped_pct":0.4, "bitrate_mbps":11.8,
  "advice_level":"WARNING", "advice":"4G特有のジッターを検知…" }
```

**サーバ → スマホ (WS JSON, echo)** — スマホが t4 を確定するための即時返し
```json
{ "t":"echo", "seq":1042, "t1":19233.7 }
```

---

## 6. 遅延計測の数学（クロックドメイン是正）

スマホ時計（t1,t4）と PC 時計（t2,t3）は同期していない。**同一クロック内でしか引き算しない**。

- **ネットワーク往復**: `RTT_net = t4 − t1`（**スマホ単一時計**。サーバが即 echo、echo に seq/t1 を同梱）
- **エンジン内部**: `engine_ms = t3 − t2`（**PC 単一 QPC**。受信→表示）
- **グラス・トゥ・グラス（推定）**: `≈ RTT_net/2 + engine_ms + encode_ms(OBS) + スマホ描画`
  - 片道は往復の半分として推定（上り下り対称の仮定を明示）。**跨ぎ減算はしない。**

この「クロックドメインを跨がない」制約こそがレビューで効く深掘り点。NTP同期に頼らず正しい数字を出す。

---

## 7. エンジン改修点（既存構造への結線）

すべて `EngineCore.vcxproj`（`vcxproj_layout`: GameEngine 配下＝EngineCore）に載る。**`USE_GUNDAM` マクロでガード**（Debug/Development のみ、Release 無効。`USE_PEPPER`/`_DEBUG` とは独立）。

1. **新規 `GameEngine/Net/NetworkInputSource.{h,cpp}`** — WinSock2 UDP 受信スレッド ＋ SPSC lock-free ring。`Framework::Update` 冒頭（Replay 分岐の隣）に「NetworkInput モード」を追加し、`ReplaySystem` と同じく `InputManager` へ状態注入。
2. **t2/t3 計装** — 注入時に seq ＋ QPC を保持。`Framework::Run` の Draw→Present 直後（`PEPPER_END_FRAME` 付近）で t3 を確定し、**P.E.P.P.E.R. の既存 GPU タイムスタンプ resolve に相乗り**して gpu_ms を得る。
3. **`SessionLogger` に `Net` カテゴリ1行追加**（`SessionLogger.h` の `Profile` の隣、`kCount` の前）→ `net.log`。profile.log / state.log / net.log が同一セッションフォルダに並び、**時刻で突合**できる（「カクついた瞬間の入力遅延と重い区間」を一括分析）。
4. **決定論との両立** — 計測は乱数/dt に触れない。ライブ操作中は `ReplaySystem` の Record を止める（記録と排他）。

> 新規ファイル/フォルダ作成・vcxproj 追記は Project/CLAUDE.md ルールに従い、着手時にユーザーへ配置・命名を確認し、追記内容を報告する。

---

## 8. スマホ側 `index.html`（1ファイル完結）

- **UI**: Tailwind＋SFダークUI（ネオンブルー）。左=仮想パッド（D-PAD / A・B・X・Y / マクロトリガー）、右=RTTメーター（緑/黄/赤）/ FPSオーバーヘッド率 / アドバイザパネル（タイピング演出）。
- **送信**: タップ即 `performance.now()` を t1 に詰めて WS 送信。
- **チャート**: RTT/FPS の時系列（Chart.js 相当は自前軽量描画でも可）。
- **配置**: HTML/CSS/JS を1枚に内包（スマホ配信容易化）。

---

## 9. 堅牢化（自動再接続・擬似テストモード）

- **自動再接続**: クライアント・サーバ双方に指数バックオフ再接続。WS 切断でメーターは「切断中」表示、復帰で自動再開。
- **擬似テストモード（標準搭載）**: エンジン未起動でも、サーバ側が fps/encode/RTT に**ゆらぎノイズを合成**して配信。UI・メーター・アドバイザを単体で検証可能（Path B の注入 off でも画が動く）。

---

## 10. アドバイザ判定ロジック

サーバが engine_ms（Path A）/ RTT_net / jitter / OBS skipped_pct を見て診断を push。

| 条件 | レベル | メッセージ（例） |
|---|---|---|
| RTT<30ms かつ GPU余裕 | `EXCELLENT` | 超低遅延を検知。G.U.N.D.A.M. によるフレーム単位の精密テストを推奨。 |
| 30≤RTT<100ms かつ ジッター高 | `WARNING` | 4Gモバイル回線特有のジッターを検知。S.U.N.D.A.Y. による無人自動デバッグへの切替を推奨。 |
| RTT≥100ms または skipped 多 | `CRITICAL` | 深刻な回線遅延を検知。ビットレート半減 or 配信 720p 化で追従性を回復。 |

S.U.N.D.A.Y. への切替提案がファミリーを繋ぐ導線になる。

---

## 11. 実装ステップ（鉄壁Git・ビルド関門を踏襲）

```
S1  pepper_server.py: FastAPI+WS 疎通 & 擬似テストモード（エンジン無しで完動）
S2  index.html: コックピット描画＋メーター（S1のダミー値で見た目確定）
S3  Path B: vgamepad 注入で実操作（sunday.py の InputDriver 流用）
S4  OBS GetStats 統合（encode_ms/skipped/bitrate をメーターへ）
S5  エンジン: NetworkInputSource（UDP受信→InputManager注入, USE_GUNDAM）
S6  t2/t3 計装＋net.log＋復路送信（PEPPER の GPU 計測に相乗り）
S7  アドバイザ判定（engine_ms/RTT/jitter/skipped → EXCELLENT/WARNING/CRITICAL＋SUNDAY切替提案）
```

S1–S4 はエンジン無改修で価値が出る（Discord脱却の見た目が即完成）。**S5–S6 が目玉**で、締切（12月末）とボス戦実装の兼ね合いで着手時期を決める。

---

## 12. 確定した設計判断（§9 の3点）

1. **輸送層 = UDP**。遅延計測が主目的でロスは seq で検知でき、TCP の Nagle/再送で ms が濁るのを避ける。
2. **S1–S4 先行**、S5–S6 は締切とボス戦の優先度を見て着手。
3. **ドキュメント整備**: 本書 `G.U.N.D.A.M..md` ＝確定版。`P.E.P.P.E.R..md §10` に Live/Net 拡張を追記済み。

---

## 13. 依存・秘密情報

- 追加 Python 依存: `fastapi` / `uvicorn`（既存: `vgamepad` / `obsws_python` / `python-dotenv`）。
- 秘密情報（`OBS_WS_PASSWORD` 等）は自宅PCの `.env` のみ。配布物に含めない（S.U.N.D.A.Y..md §7 と共通）。
- ループバック UDP は 127.0.0.1 固定ポート。外部公開しない（外との経路は Tailscale の WS のみ）。

---

関連: `S.U.N.D.A.Y..md`（第0層基盤・二刀流）/ `P.E.P.P.E.R..md`（§10 Live/Net 拡張）/ `J.A.R.V.I.S..md`（司令塔）
