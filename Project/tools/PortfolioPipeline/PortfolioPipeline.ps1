<#
.SYNOPSIS
    ポートフォリオ提出物の収集・整理パイプライン。
.DESCRIPTION
    環境確認 → 設定検証 → 作品ごとに「取得 → ソース配置 → 資料ダウンロード → 実行ファイルの用意（ビルド等）
    → 起動確認 → 実行ファイル配置」→ 提出用 ZIP 作成、の順に処理する。
    途中のファイルは Intermediate、提出するものだけを Generated に置く。
    1 作品でも失敗したら全体を停止する（提出物の一式が欠けたまま ZIP を作らないため）。
.PARAMETER TargetWork
    指定した work_folder_name の作品だけを処理する。
.PARAMETER SkipDownload
    Google Drive からの取得を省略する（開発中の切り分け用）。
.PARAMETER SkipBuild
    MSBuild を省略する（開発中の切り分け用）。
.PARAMETER SkipSmokeTest
    起動確認を省略する（開発中の切り分け用）。
.PARAMETER KeepIntermediate
    成功時も Intermediate を残す。失敗時は常に残る。
.PARAMETER ConfigPath
    設定ファイル。省略時はこのスクリプトと同じフォルダの config.psd1。
.PARAMETER NoPause
    終了時に Enter 入力を待たない（コンソールから直接実行するとき用）。
.EXAMPLE
    .\PortfolioPipeline.ps1 -TargetWork 01_個人作品 -SkipBuild
#>
param(
    [string]$TargetWork = $null,
    [switch]$SkipDownload,
    [switch]$SkipBuild,
    [switch]$SkipSmokeTest,
    [switch]$KeepIntermediate,
    [string]$ConfigPath = '',
    [switch]$NoPause
)

$ErrorActionPreference = 'Stop'
$requirementsFileName = 'requirements.txt'
$configFileName = 'config.psd1'
$defaultProjectFolder = 'ソースコード'
$defaultExecutableFolder = '実行ファイル'
$smokeWaitSeconds = 3
$exitSuccess = 0
$exitFailure = 1

$libDir = Join-Path $PSScriptRoot 'lib'
. (Join-Path $libDir 'ConfigAssert.ps1')
. (Join-Path $libDir 'FileUtils.ps1')
. (Join-Path $libDir 'BuildUtils.ps1')
. (Join-Path $libDir 'PipelineTasks.ps1')
$pyDownloadScript = Join-Path $libDir 'download_file.py'

<#
.SYNOPSIS
    結果を読めるよう Enter を待ってから終了する。
#>
function Exit-Pipeline {
    param([int]$Code)
    try { Stop-Transcript | Out-Null } catch { }
    if (-not $NoPause) {
        Read-Host '終了するには Enter キーを押してください...' | Out-Null
    }
    exit $Code
}

# 実行ログ（取得したコミットや失敗理由を後から追えるように残す）
$logDir = Join-Path $PSScriptRoot 'Logs'
New-Item -Path $logDir -ItemType Directory -Force | Out-Null
$logPath = Join-Path $logDir ('pipeline_{0}.log' -f (Get-Date -Format 'yyyyMMdd_HHmmss'))
Start-Transcript -Path $logPath | Out-Null

Write-Host '--- 環境確認を開始します ---'

# Step 1: Python
Write-Host 'Step 1: Python の確認中...'
try {
    $pythonVersion = python --version
    if ($LASTEXITCODE -ne 0) { throw "終了コード $LASTEXITCODE" }
    Write-Host "OK: $pythonVersion"
} catch {
    Write-Host '失敗: Python が見つかりません' -ForegroundColor Red
    Exit-Pipeline $exitFailure
}

# Step 2: Git
Write-Host 'Step 2: Git の確認中...'
try {
    $gitVersion = git --version
    if ($LASTEXITCODE -ne 0) { throw "終了コード $LASTEXITCODE" }
    Write-Host "OK: $gitVersion"
} catch {
    Write-Host '失敗: Git が見つかりません' -ForegroundColor Red
    Exit-Pipeline $exitFailure
}

# Step 3: 依存ライブラリ
Write-Host 'Step 3: 依存ライブラリの確認中...'
$importCheck = 'import googleapiclient, google_auth_oauthlib, google_auth_httplib2'
try {
    $reqFile = Join-Path $PSScriptRoot $requirementsFileName
    if (-not (Test-Path $reqFile)) {
        throw "$requirementsFileName が見つかりません。"
    }

    # import 失敗のメッセージは一時的に隠す
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = 'SilentlyContinue'
    python -c $importCheck 2>$null
    $installed = ($LASTEXITCODE -eq 0)
    $ErrorActionPreference = $oldEAP

    if (-not $installed) {
        Write-Host '未導入: インストールを開始します'
        $ErrorActionPreference = 'Continue'
        python -m pip install -r $reqFile --upgrade --no-warn-script-location 2>&1 | Out-Null
        $pipExitCode = $LASTEXITCODE
        $ErrorActionPreference = $oldEAP
        if ($pipExitCode -ne 0) {
            throw "pip install が終了コード $pipExitCode で失敗しました。"
        }

        # 「完了」表示だけでなく、実際に読み込めるかで導入成功を判定する
        $ErrorActionPreference = 'SilentlyContinue'
        python -c $importCheck 2>$null
        $installedAfter = ($LASTEXITCODE -eq 0)
        $ErrorActionPreference = $oldEAP
        if (-not $installedAfter) {
            throw 'インストール後もライブラリを読み込めません。'
        }
        Write-Host 'インストール完了'
    } else {
        Write-Host '導入済み'
    }
} catch {
    Write-Host "失敗: $($_.Exception.Message)" -ForegroundColor Red
    Exit-Pipeline $exitFailure
}

Write-Host '--- 環境確認と準備が完了しました ---'

$pipelineSucceeded = $false
$intermediatePrepared = $false
$intermediateDir = Join-Path $PSScriptRoot 'Intermediate'
try {
    # Step 4: 設定の読み込みと検証（不正な設定は作業前に止める）
    Write-Host 'Step 4: 設定の確認中...'
    if ([string]::IsNullOrEmpty($ConfigPath)) {
        $ConfigPath = Join-Path $PSScriptRoot $configFileName
    }
    if (-not (Test-Path $ConfigPath)) {
        throw "$ConfigPath が見つかりません。"
    }
    $config = Import-PowerShellDataFile -Path $ConfigPath
    Assert-Config -Config $config -RootPath $PSScriptRoot

    $zipName = $config.output.zip_name
    $generatedDir = Resolve-ConfigPath -Path $config.output.generated_dir -RootPath $PSScriptRoot
    $workDir = Join-Path $generatedDir $zipName
    $projFolder = if ([string]::IsNullOrEmpty($config.output.project_folder_name)) { $defaultProjectFolder } else { $config.output.project_folder_name }
    $execFolder = if ([string]::IsNullOrEmpty($config.output.executable_folder_name)) { $defaultExecutableFolder } else { $config.output.executable_folder_name }

    # 出力先がスクリプト自身やドライブ直下だと、作り直しで関係ないものを消してしまう
    $scriptRootFull = [System.IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\')
    $generatedFull = [System.IO.Path]::GetFullPath($generatedDir).TrimEnd('\')
    if ($generatedFull -eq $scriptRootFull -or $generatedFull -eq [System.IO.Path]::GetPathRoot($generatedFull).TrimEnd('\')) {
        throw "generated_dir に指定できない場所です: $generatedFull"
    }
    Set-AllowedDeleteRoots -Roots @($generatedDir, $intermediateDir)

    # 処理する作品を選ぶ
    $selectedWorks = @()
    foreach ($work in $config.works) {
        if (-not [string]::IsNullOrEmpty($TargetWork) -and $work.work_folder_name -ne $TargetWork) { continue }
        $selectedWorks += $work
    }
    if (-not [string]::IsNullOrEmpty($TargetWork) -and $selectedWorks.Count -eq 0) {
        throw "対象作品 '$TargetWork' が設定にありません。"
    }
    Write-Host "OK: 対象 $($selectedWorks.Count) 作品"

    # Step 5: フォルダの準備。Intermediate は毎回作り直し、成果物は作品ごとに置き換える
    Write-Host 'Step 5: 作業フォルダの準備中...'
    try {
        Recreate-Directory -Path $intermediateDir
        $intermediatePrepared = $true
        if (-not (Test-Path $workDir)) {
            New-Item -Path $workDir -ItemType Directory -Force | Out-Null
        }
    } catch {
        throw "収集用フォルダの準備に失敗しました。原因: $($_.Exception.Message)"
    }
    Write-Host "OK: $workDir"

    # Step 6: 作品ごとの処理
    $workIndex = 1
    foreach ($work in $selectedWorks) {
        $name = $work.work_folder_name
        Write-Host "Step 6-${workIndex}: $name"

        $workPath = Join-Path $workDir $name
        New-Item -Path $workPath -ItemType Directory -Force | Out-Null
        # Intermediate 側は ASCII 名にして、ビルドツールが日本語パスで落ちるのを避ける
        $interWorkPath = Join-Path $intermediateDir ('work_{0:00}' -f $workIndex)
        $interSourcePath = Join-Path $interWorkPath 'Source'
        # 実行ファイルはここに用意し、起動確認の後で成果物へ置き換える
        $execStagingPath = Join-Path $interWorkPath 'exec_staging'
        $execStaged = $false
        New-Item -Path $interWorkPath -ItemType Directory -Force | Out-Null

        # 取得（成果物はまだ触らない）
        if (-not [string]::IsNullOrEmpty($work.git_repository_url)) {
            $sourceInfo = Invoke-PipelineTask -Message "Git から取得: $($work.git_repository_url)" `
                -ErrorMessage "作品 '$name' の取得に失敗しました。" `
                -Action { Get-SourceFromGit -RepoUrl $work.git_repository_url -CloneDestPath $interSourcePath }
        } else {
            $localPath = Resolve-ConfigPath -Path $work.local_source_path -RootPath $PSScriptRoot
            $sourceInfo = Invoke-PipelineTask -Message "ローカルから取得: $localPath" `
                -ErrorMessage "作品 '$name' の取得に失敗しました。" `
                -Action { Get-SourceFromLocal -LocalPath $localPath -DestPath $interSourcePath }
        }
        foreach ($key in $sourceInfo.Keys) {
            Write-Host "     $key = $($sourceInfo[$key])"
        }

        # 取得が済んでから、ソースの成果物を置き換える（ビルドで出るファイルが混ざる前に）
        $destProjPath = Join-Path $workPath $projFolder
        Invoke-PipelineTask -Message "ソースを配置: $projFolder" `
            -ErrorMessage "作品 '$name' のソース配置に失敗しました。" `
            -Action { Publish-StagedFolder -StagingPath $interSourcePath -DestPath $destProjPath }

        # Google Drive（ネットワークを使うので -SkipDownload で省略できる）
        if (-not $SkipDownload) {
            if (-not [string]::IsNullOrEmpty($work.google_drive_file_id)) {
                Invoke-PipelineTask -Message '説明資料をダウンロード' `
                    -ErrorMessage "作品 '$name' の資料ダウンロードに失敗しました。" `
                    -Action { Download-ProgramSpec -SpecId $work.google_drive_file_id -WorkPath $workPath `
                        -InterWorkPath $interWorkPath -PyScriptPath $pyDownloadScript }
            }
            if (-not [string]::IsNullOrEmpty($work.google_drive_zip_id)) {
                Invoke-PipelineTask -Message '実行ファイル ZIP をダウンロード・展開' `
                    -ErrorMessage "作品 '$name' の実行ファイルの取得に失敗しました。" `
                    -Action { Deploy-ExecutableFromZip -ZipId $work.google_drive_zip_id -StagingPath $execStagingPath `
                        -InterWorkPath $interWorkPath -PyScriptPath $pyDownloadScript }
                $execStaged = $true
            }
        } elseif ($work.google_drive_file_id -or $work.google_drive_zip_id) {
            Write-Host '  -> Google Drive の取得を省略（-SkipDownload）' -ForegroundColor Yellow
        }

        # ローカルの実行ファイル
        if (-not [string]::IsNullOrEmpty($work.local_executable_path)) {
            $localExePath = Resolve-ConfigPath -Path $work.local_executable_path -RootPath $PSScriptRoot
            Invoke-PipelineTask -Message "実行ファイルを用意: $localExePath" `
                -ErrorMessage "作品 '$name' の実行ファイルの用意に失敗しました。" `
                -Action { Deploy-ExecutableFromLocal -LocalPath $localExePath -StagingPath $execStagingPath `
                    -InterWorkPath $interWorkPath }
            $execStaged = $true
        }

        # ビルド（Intermediate のソースで行い、必要な成果物だけを用意する）
        if (-not [string]::IsNullOrEmpty($work.project_msbuild_target_path)) {
            if (-not $SkipBuild) {
                Invoke-PipelineTask -Message "MSBuild でビルド: $($work.project_msbuild_target_path)" `
                    -ErrorMessage "作品 '$name' のビルドに失敗しました。" `
                    -Action { Invoke-MSBuildAndDeploy -Work $work -InterSourcePath $interSourcePath `
                        -InterWorkPath $interWorkPath -StagingPath $execStagingPath }
                $execStaged = $true
            } else {
                Write-Host '  -> ビルドを省略（-SkipBuild）' -ForegroundColor Yellow
            }
        }

        # 起動確認。今回用意した実行ファイルを優先し、無ければ前回配置済みのものを確かめる。
        # どちらも Intermediate の複製で起動するので、提出物に作品のログ等は混ざらない
        $destExecPath = Join-Path $workPath $execFolder
        if (-not $SkipSmokeTest) {
            $required = [bool]$work.smoke_test_required
            $smokeSource = if ($execStaged) { $execStagingPath } else { $destExecPath }
            Invoke-PipelineTask -Message '起動確認' `
                -ErrorMessage "作品 '$name' の起動確認に失敗しました。" `
                -Action { Test-ExecutableSmoke -ExecSourceDir $smokeSource -RunDir (Join-Path $interWorkPath 'smoke_run') `
                    -TestExecutableName $work.test_executable_name -WaitSeconds $smokeWaitSeconds -Required $required }
        } else {
            Write-Host '  -> 起動確認を省略（-SkipSmokeTest）' -ForegroundColor Yellow
        }

        # 起動確認を通ってから、実行ファイルの成果物を置き換える
        if ($execStaged) {
            Invoke-PipelineTask -Message "実行ファイルを配置: $execFolder" `
                -ErrorMessage "作品 '$name' の実行ファイル配置に失敗しました。" `
                -Action { Publish-StagedFolder -StagingPath $execStagingPath -DestPath $destExecPath }
        }

        $workIndex++
    }

    # Step 7: 提出用 ZIP
    Write-Host 'Step 7: 提出用 ZIP の作成中...'
    $zipPath = Join-Path $generatedDir "$zipName.zip"
    Invoke-PipelineTask -Message "ZIP を作成: $zipPath" `
        -ErrorMessage '提出用 ZIP の作成に失敗しました。' `
        -Action {
            if (Test-Path $zipPath) { Remove-Item -Path $zipPath -Force }
            Add-Type -AssemblyName System.IO.Compression.FileSystem
            [System.IO.Compression.ZipFile]::CreateFromDirectory($workDir, $zipPath,
                [System.IO.Compression.CompressionLevel]::Optimal, $true)
        }

    $pipelineSucceeded = $true
} catch {
    Write-Host "失敗: $($_.Exception.Message)" -ForegroundColor Red
    if ($intermediatePrepared) {
        Write-Host "調査用: Intermediate を残しています -> $intermediateDir" -ForegroundColor Yellow
    }
} finally {
    # 成功時だけ中間成果物を掃除する。失敗時は調査のため残す
    if ($pipelineSucceeded -and -not $KeepIntermediate -and (Test-Path $intermediateDir)) {
        Remove-Item -Path $intermediateDir -Recurse -Force -ErrorAction SilentlyContinue | Out-Null
    }
}

Write-Host "ログ: $logPath"
if ($pipelineSucceeded) {
    Write-Host '--- パイプラインが完了しました ---' -ForegroundColor Green
    Exit-Pipeline $exitSuccess
}
Exit-Pipeline $exitFailure
