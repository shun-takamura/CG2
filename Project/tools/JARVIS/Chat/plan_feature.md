# スマホ単独で設計まで完結する `/plan` 機能（相談メモ）

J.A.R.V.I.S. の `/prompt` は「投げっぱなしの実行」で、Claudeの返答や対話が見えない。
スマホ単独で「設計 → 仕様保存 → 実装」まで完結できるよう、対話系コマンドを追加する。

## 確認した claude CLI の能力（実機 `claude --help` で確認済み）
- `-p / --print` … 非対話で実行
- `--output-format json` … 返答テキストと `session_id` を構造化で取得
- `--permission-mode plan` … 読み取り専用（コードを編集しない）設計モード
- `-r / --resume <id>` … セッションを継続（往復の設計対話）

## 追加コマンド
| コマンド | claude CLI | 用途 |
|---|---|---|
| `/ask <質問>` | `-p --permission-mode plan`（単発） | 軽い相談。編集せず返答だけ返す |
| `/plan <指示>` | `-p --permission-mode plan` ＋ `--resume`（会話継続） | 設計対話。往復で詰める |
| `/plan_new` | （セッションIDを破棄） | 設計対話をリセットして新規開始 |
| `/plan_save <名前>` | `--resume --permission-mode acceptEdits` | 対話の結論を `Project/tools/JARVIS/Chat/<名前>.md` に保存 |

実装は既存 `/prompt` で行う：
```
/prompt instruction:"Project/tools/JARVIS/Chat/foo.md の通りに実装して"
```
`/prompt` はファイルを読めるので保存した仕様書を見て実装する（新コマンド不要）。

## 設計上の決定
- **安全**：`/ask`・`/plan` は `plan` モード＝コードを一切編集しない。書き込むのは `/plan_save`（仕様mdだけ）と `/prompt`（実装）のみ。
- **会話の記憶**：Botが「今の plan セッションID」を1つ保持。`/plan` のたびに `--resume`。`/plan_new` で破棄。session_id は `--output-format json` の応答から取得。
- **長い返答**：Discord 2000字制限を超えたら `.md` ファイルとして添付。
- **既存パターン踏襲**：`defer → asyncio.to_thread → followup`。
- **既存ツールは非改変**：`ai_edit.find_claude()` を参照するだけで `ai_edit.py` は変更しない。新ロジックは新モジュール `claude_chat.py` に分離。
- **保存先**：`Project/tools/JARVIS/Chat/`（設計メモ専用の新フォルダ。既存の `Project/tools/Python/` の配置はノータッチ）。

## 実装ファイル
- 新規：`Project/tools/Python/claude_chat.py`（ask / plan / save_plan）
- 追記：`Project/tools/Python/jarvis_bot.py`（/ask /plan /plan_new /plan_save と長文送信ヘルパ）
