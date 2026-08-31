@echo off
rem 提出用のクリーンなプロジェクトフォルダを作る。引数はそのまま package_project.py へ渡す。
rem 例: package_project.bat --zip
chcp 65001 > nul
cd /d "%~dp0..\.."
python tools\Python\package_project.py %*
echo.
pause
