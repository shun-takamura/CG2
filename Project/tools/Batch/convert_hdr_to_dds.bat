@echo off
setlocal enabledelayedexpansion

REM ============================================================
REM HDR to DDS Cubemap Converter
REM 
REM Usage:
REM   convert_hdr_to_dds.bat         - Interactive mode (pause at end)
REM   convert_hdr_to_dds.bat --silent - Silent mode (for build events)
REM
REM Converts Assets/*.hdr to BC6H compressed Cubemap DDS files
REM Output goes to Resources/Cubemaps/
REM Skips files that are already converted
REM ============================================================

REM Detect mode
set INTERACTIVE_MODE=1
if "%~1"=="--silent" (
    set INTERACTIVE_MODE=0
)

echo.
echo ==========================================
echo   HDR to DDS Cubemap Conversion Started
echo ==========================================
echo.

REM Check required tools
if not exist "externals\cmft\cmftRelease.exe" (
    echo [ERROR] cmftRelease.exe not found at externals\cmft\
    goto :end_with_pause
)

if not exist "externals\texconv\Texconv.exe" (
    echo [ERROR] Texconv.exe not found at externals\texconv\
    goto :end_with_pause
)

if not exist "Assets" (
    echo [ERROR] Assets folder not found
    goto :end_with_pause
)

REM Create required folders
if not exist "Resources\Cubemaps" (
    mkdir "Resources\Cubemaps"
    echo [INFO] Created Resources\Cubemaps folder
)

if not exist "Temp" (
    mkdir "Temp"
    echo [INFO] Created Temp folder
)

REM Process HDR files
set PROCESSED_COUNT=0
set SKIPPED_COUNT=0

for %%f in (Assets\*.hdr) do (
    call :ProcessHDR "%%~nxf" "%%~nf"
)

echo.
echo ==========================================
echo   Conversion Completed
echo   Processed: !PROCESSED_COUNT!
echo   Skipped:   !SKIPPED_COUNT!
echo ==========================================
echo.

:end_with_pause
if %INTERACTIVE_MODE%==1 pause
exit /b 0


REM ============================================================
REM :ProcessHDR function
REM ============================================================
:ProcessHDR
set HDR_FILE=%~1
set BASE_NAME=%~2
set OUTPUT_DDS=Resources\Cubemaps\%BASE_NAME%.dds
set TEMP_DDS=Temp\%BASE_NAME%.dds

if exist "%OUTPUT_DDS%" (
    echo [SKIP] %BASE_NAME%.dds already exists
    set /a SKIPPED_COUNT+=1
    exit /b 0
)

echo.
echo [PROCESS] %HDR_FILE%

echo   [1/2] Converting to Cubemap with Mipmaps...
externals\cmft\cmftRelease.exe ^
    --input Assets\%HDR_FILE% ^
    --filter none ^
    --generateMipChain true ^
    --outputNum 1 ^
    --output0 Temp\%BASE_NAME% ^
    --output0params dds,rgba16f,cubemap ^
    --silent

if errorlevel 1 (
    echo [ERROR] cmft failed for %HDR_FILE%
    exit /b 1
)

if not exist "%TEMP_DDS%" (
    echo [ERROR] cmft did not produce output for %HDR_FILE%
    exit /b 1
)

echo   [2/2] Compressing to BC6H...
externals\texconv\Texconv.exe ^
    -f BC6H_UF16 ^
    -o Resources\Cubemaps\ ^
    -y ^
    -nologo ^
    "%TEMP_DDS%" > nul

if errorlevel 1 (
    echo [ERROR] Texconv failed for %HDR_FILE%
    exit /b 1
)

del "%TEMP_DDS%"

echo   [DONE] %BASE_NAME%.dds created
set /a PROCESSED_COUNT+=1

exit /b 0