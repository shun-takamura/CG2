@echo off
chcp 65001 >nul
setlocal
cd /d "%~dp0"

echo ==========================================
echo   mergiraf セットアップ
echo ==========================================
echo.
echo このリポジトリで .cpp / .h の自動マージを有効にします。
echo クローン後に一度だけ実行してください。
echo.

REM --- 1) git の存在確認 ---
where git >nul 2>&1
if errorlevel 1 (
    echo [エラー] git が見つかりません。
    echo          Git for Windows をインストールしてから再実行してください。
    echo.
    pause
    exit /b 1
)

REM --- 2) mergiraf.exe を展開（初回のみ / 展開後 約75MB） ---
if exist "mergiraf.exe" (
    echo [1/2] mergiraf.exe は展開済みです。
) else (
    echo [1/2] mergiraf.exe を展開しています... 少し時間がかかります
    "%SystemRoot%\System32\tar.exe" -xf "mergiraf_x86_64-pc-windows-gnu.zip"
)

if not exist "mergiraf.exe" (
    echo.
    echo [エラー] mergiraf.exe の展開に失敗しました。
    echo          mergiraf_x86_64-pc-windows-gnu.zip が同じフォルダにあるか確認してください。
    echo.
    pause
    exit /b 1
)

REM --- 3) このリポジトリに merge driver を登録 ---
REM     .git/config はバージョン管理されないため、各自1回の登録が必要
set "MG=%~dp0mergiraf.exe"
set "MG=%MG:\=/%"

git config --local merge.mergiraf.name mergiraf
if errorlevel 1 (
    echo.
    echo [エラー] git config に失敗しました。
    echo          このファイルがリポジトリの中にあるか確認してください。
    echo.
    pause
    exit /b 1
)
git config --local merge.mergiraf.driver "'%MG%' merge --git %%O %%A %%B -s %%S -x %%X -y %%Y -p %%P -l %%L"
git config --local merge.conflictStyle diff3

echo [2/2] merge driver を登録しました。
echo.
echo ==========================================
echo   セットアップ完了
echo ==========================================
echo.
echo 以降 pull / merge / rebase のときに .cpp / .h が
echo 構文を理解した形で自動マージされます。
echo 例）二人が同じクラスに別々の関数を追加 -^> 衝突せず両方とも残る
echo.
echo 普段の操作は今までどおりで、特別なコマンドは不要です。
echo このウィンドウは閉じて構いません。
echo.
pause
exit /b 0
