"""J.A.R.V.I.S. Step4: Discord Bot

.env のトークンで Bot を起動し、スラッシュコマンドを受けて自宅PCを駆動する。
まずは /ping → pong! の疎通確認から（動作確認済み）。
ここから /prompt・/refactor・/test_run を足して、Botを
「ai_edit → git_pipeline → test_run」を順に呼ぶオーケストレーターに育てる。
"""

import asyncio
import os
from pathlib import Path

import discord
from discord import app_commands
from dotenv import load_dotenv

import test_run  # 同フォルダの「ゲーム起動＆スクショ」モジュール

# このファイルは tools/Python にあるので、3つ上がリポジトリ直下(CG2)
REPO_DIR = Path(__file__).resolve().parents[3]
# 金庫(.env)を開けて中身を環境変数として読み込む
load_dotenv(REPO_DIR / ".env")

# .env からトークンを取り出す。無ければここで KeyError で落ちる（=設定ミスにすぐ気づける）
TOKEN = os.environ["DISCORD_TOKEN"]
# テストサーバーのID。あれば「そのサーバー限定で即時登録」＝コマンドがすぐ出る
# 無ければ全サーバー共通登録になり、反映に数分かかることがある（任意設定）
GUILD_ID = os.environ.get("DISCORD_GUILD_ID")

# Botが受け取りたい情報の意思表示。スラッシュコマンドだけなら既定でOK（特権インテント不要）
intents = discord.Intents.default()

# 最小構成：素の Client と、スラッシュコマンドを束ねる CommandTree
client = discord.Client(intents=intents)
tree = app_commands.CommandTree(client)


# /ping コマンドを定義。スマホで /ping を選ぶとこの関数が走る
@tree.command(name="ping", description="疎通確認。pong! を返す")
async def ping(interaction: discord.Interaction):
    # 重い処理が無いので3秒以内に即レスして良い
    await interaction.response.send_message("pong!")


# /test_run コマンド：ゲームを起動してキー入力し、前後のスクショを返す
@tree.command(name="test_run", description="ゲームを起動・キー入力してスクショを返す")
@app_commands.describe(
    keys="押すキーをカンマ区切りで（例: q,e,space）。省略時は q,e,space",
    wait_sec="起動から撮影までの待機秒数（省略時は5）",
)
async def test_run_cmd(interaction: discord.Interaction, keys: str = "q,e,space", wait_sec: int = 5):
    # 起動+待機+撮影で3秒を超えるので、まず defer で「考え中…」を出して時間を稼ぐ
    await interaction.response.defer()
    try:
        # "q,e,space" → ["q", "e", "space"]（空白と空要素は捨てる）
        key_list = [k.strip() for k in keys.split(",") if k.strip()]
        # run_test は同期処理（time.sleepでBotが固まる）なので別スレッドに逃がす
        result = await asyncio.to_thread(test_run.run_test, key_list, wait_sec)
        # 撮った2枚を添付して @メンション付きで返信
        files = [
            discord.File(result["before"], filename="before.png"),
            discord.File(result["after"], filename="after.png"),
        ]
        await interaction.followup.send(
            f"{interaction.user.mention} test_run 完了！ 押したキー: {', '.join(result['keys'])}",
            files=files,
        )
    except Exception as e:
        # ウィンドウが見つからない等はここで捕まえてスマホに通知
        await interaction.followup.send(f"{interaction.user.mention} test_run 失敗: {e}")


# Botがログインしてオンラインになった瞬間に1度だけ呼ばれる
@client.event
async def on_ready():
    if GUILD_ID:
        # テストサーバー限定で登録（即座にコマンドが反映される）
        guild = discord.Object(id=int(GUILD_ID))
        tree.copy_global_to(guild=guild)
        await tree.sync(guild=guild)
    else:
        # 全サーバー共通で登録（反映までDiscord側で時間がかかることがある）
        await tree.sync()
    print(f"起動完了: {client.user} としてログインしました")


# トークンでDiscordに接続してBotを動かし続ける（Ctrl+Cで停止）
client.run(TOKEN)
