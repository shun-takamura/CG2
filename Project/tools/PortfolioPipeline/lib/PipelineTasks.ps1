# 作品ごとの処理（取得・ダウンロード・ビルド・配置・起動確認）。
# どの関数も「一時領域で作ってから成果物を置き換える」順序を守る。

<#
.SYNOPSIS
    処理の表示・例外捕捉・失敗理由の付加を共通化する。
.PARAMETER Message
    実行中に表示する内容。
.PARAMETER Action
    実行する処理。
.PARAMETER ErrorMessage
    失敗時に先頭へ付ける説明。
.RETURNS
    Action の戻り値。
#>
function Invoke-PipelineTask {
    param(
        [string]$Message,
        [scriptblock]$Action,
        [string]$ErrorMessage
    )
    Write-Host "  -> $Message" -NoNewline
    try {
        $result = & $Action
        Write-Host ' OK' -ForegroundColor Green
        return $result
    } catch {
        Write-Host ' 失敗' -ForegroundColor Red
        throw "$ErrorMessage 原因: $($_.Exception.Message)"
    }
}

<#
.SYNOPSIS
    外部コマンドを実行し、出力をログにも残す。終了コードを返す。
.PARAMETER FilePath
    実行するコマンド。
.PARAMETER Arguments
    引数。
.RETURNS
    終了コード。
#>
function Invoke-NativeCommand {
    param(
        [string]$FilePath,
        [string[]]$Arguments
    )
    # Windows PowerShell 5.1 では外部コマンドの stderr が例外扱いになるため、一時的に Continue にする
    $oldEAP = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        # Invoke-PipelineTask の見出し行と出力を分ける
        Write-Host ''
        & $FilePath @Arguments 2>&1 | ForEach-Object { Write-Host "     $_" }
        return $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldEAP
    }
}

<#
.SYNOPSIS
    Git からソースを浅く取得し、追跡情報を記録してから .git を取り除く。
.PARAMETER RepoUrl
    リポジトリ URL。
.PARAMETER CloneDestPath
    取得先（Intermediate 配下）。
.RETURNS
    取得元 URL・コミット ID・取得日時のハッシュテーブル。
#>
function Get-SourceFromGit {
    param(
        [string]$RepoUrl,
        [string]$CloneDestPath
    )

    Recreate-Directory -Path $CloneDestPath
    $exitCode = Invoke-NativeCommand -FilePath 'git' -Arguments @('clone', '--depth', '1', $RepoUrl, $CloneDestPath)
    if ($exitCode -ne 0) {
        throw "git clone が終了コード $exitCode で失敗しました。"
    }

    # .git を消す前に、どの版を取得したかを残す
    $commitId = (& git -C $CloneDestPath rev-parse HEAD).Trim()
    $info = [ordered]@{
        source     = 'git'
        url        = $RepoUrl
        commit     = $commitId
        fetched_at = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    }

    $gitDir = Join-Path $CloneDestPath '.git'
    if (Test-Path $gitDir) {
        Remove-Item -Path $gitDir -Recurse -Force | Out-Null
    }
    return $info
}

<#
.SYNOPSIS
    ローカルフォルダからソースを除外付きで取得する。
.PARAMETER LocalPath
    コピー元（絶対パス）。
.PARAMETER DestPath
    取得先（Intermediate 配下）。
.RETURNS
    取得元パスと取得日時のハッシュテーブル。
#>
function Get-SourceFromLocal {
    param(
        [string]$LocalPath,
        [string]$DestPath
    )
    Recreate-Directory -Path $DestPath
    Copy-ItemWithExclude -Source $LocalPath -Destination $DestPath
    return [ordered]@{
        source     = 'local'
        path       = $LocalPath
        fetched_at = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
    }
}

<#
.SYNOPSIS
    一時領域で揃えた中身で、成果物フォルダを置き換える。
.PARAMETER StagingPath
    取得済みの一時フォルダ。
.PARAMETER DestPath
    成果物フォルダ。
#>
function Publish-StagedFolder {
    param(
        [string]$StagingPath,
        [string]$DestPath
    )
    if (-not (Test-Path $StagingPath)) {
        throw "配置元がありません: $StagingPath"
    }
    Recreate-Directory -Path $DestPath
    Copy-ItemWithExclude -Source $StagingPath -Destination $DestPath
}

<#
.SYNOPSIS
    download_file.py で Google Drive のファイルを取得する。
.PARAMETER FileId
    Google Drive のファイル ID。
.PARAMETER OutputDir
    保存先フォルダ。
.PARAMETER PyScriptPath
    download_file.py のパス。
#>
function Invoke-DriveDownload {
    param(
        [string]$FileId,
        [string]$OutputDir,
        [string]$PyScriptPath
    )
    $exitCode = Invoke-NativeCommand -FilePath 'python' -Arguments @($PyScriptPath, $FileId, $OutputDir)
    if ($exitCode -ne 0) {
        throw "download_file.py が終了コード $exitCode で失敗しました。"
    }
}

<#
.SYNOPSIS
    説明資料（PDF・スライド等）を一時領域へ落としてから作品フォルダへ配置する。
.PARAMETER SpecId
    Google Drive のファイル ID。
.PARAMETER WorkPath
    成果物側の作品フォルダ。
.PARAMETER InterWorkPath
    Intermediate 側の作品フォルダ。
.PARAMETER PyScriptPath
    download_file.py のパス。
#>
function Download-ProgramSpec {
    param(
        [string]$SpecId,
        [string]$WorkPath,
        [string]$InterWorkPath,
        [string]$PyScriptPath
    )
    $tempSpecDir = Join-Path $InterWorkPath 'spec_download'
    Recreate-Directory -Path $tempSpecDir
    Invoke-DriveDownload -FileId $SpecId -OutputDir $tempSpecDir -PyScriptPath $PyScriptPath

    $files = @(Get-ChildItem -LiteralPath $tempSpecDir -File)
    if ($files.Count -eq 0) {
        throw '資料のダウンロード結果が空です。'
    }
    foreach ($file in $files) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $WorkPath $file.Name) -Force
    }
}

<#
.SYNOPSIS
    Google Drive の実行ファイル ZIP を取得・展開し、一時領域に用意する。
.DESCRIPTION
    成果物フォルダへの置き換えは起動確認の後に Publish-StagedFolder で行う。
.PARAMETER ZipId
    Google Drive のファイル ID。
.PARAMETER StagingPath
    実行ファイルを用意する一時フォルダ（Intermediate 配下）。
.PARAMETER InterWorkPath
    Intermediate 側の作品フォルダ。
.PARAMETER PyScriptPath
    download_file.py のパス。
#>
function Deploy-ExecutableFromZip {
    param(
        [string]$ZipId,
        [string]$StagingPath,
        [string]$InterWorkPath,
        [string]$PyScriptPath
    )
    $tempZipDir = Join-Path $InterWorkPath 'zip_download'
    Recreate-Directory -Path $tempZipDir
    Invoke-DriveDownload -FileId $ZipId -OutputDir $tempZipDir -PyScriptPath $PyScriptPath

    $zipFiles = @(Get-ChildItem -LiteralPath $tempZipDir -Filter '*.zip' -File)
    if ($zipFiles.Count -ne 1) {
        throw "ZIP ファイルは 1 件である必要があります。検出数: $($zipFiles.Count)"
    }

    Recreate-Directory -Path $StagingPath
    Expand-AndFlattenArchive -ZipPath $zipFiles[0].FullName `
        -TempExtractDir (Join-Path $InterWorkPath 'zip_extract') `
        -DestinationDir $StagingPath
}

<#
.SYNOPSIS
    ローカルの実行ファイル（ZIP またはフォルダ）を一時領域に用意する。
.PARAMETER LocalPath
    ZIP またはフォルダの絶対パス。
.PARAMETER StagingPath
    実行ファイルを用意する一時フォルダ（Intermediate 配下）。
.PARAMETER InterWorkPath
    Intermediate 側の作品フォルダ。
#>
function Deploy-ExecutableFromLocal {
    param(
        [string]$LocalPath,
        [string]$StagingPath,
        [string]$InterWorkPath
    )
    if (-not (Test-Path $LocalPath)) {
        throw "指定されたローカルパスが見つかりません: $LocalPath"
    }

    Recreate-Directory -Path $StagingPath
    if ((Test-Path $LocalPath -PathType Leaf) -and $LocalPath -like '*.zip') {
        Expand-AndFlattenArchive -ZipPath $LocalPath `
            -TempExtractDir (Join-Path $InterWorkPath 'zip_extract') `
            -DestinationDir $StagingPath
    } else {
        Copy-ItemWithExclude -Source $LocalPath -Destination $StagingPath
    }
}

<#
.SYNOPSIS
    Intermediate 上のソースを MSBuild でビルドし、必要な成果物だけを一時領域に用意する。
.PARAMETER Work
    作品設定。
.PARAMETER InterSourcePath
    Intermediate 側の取得済みソース。
.PARAMETER InterWorkPath
    Intermediate 側の作品フォルダ（ログと一時ファイルの置き場）。
.PARAMETER StagingPath
    実行ファイルを用意する一時フォルダ（Intermediate 配下）。
#>
function Invoke-MSBuildAndDeploy {
    param(
        [hashtable]$Work,
        [string]$InterSourcePath,
        [string]$InterWorkPath,
        [string]$StagingPath
    )

    $msbuildPath = Find-MSBuildPath
    $msbuildTarget = Join-Path $InterSourcePath $Work.project_msbuild_target_path
    if (-not (Test-Path $msbuildTarget)) {
        throw "ビルド対象が見つかりません: $msbuildTarget"
    }
    $configName = if ($Work.project_msbuild_configuration) { $Work.project_msbuild_configuration } else { 'Release' }

    # ログは Intermediate に残し、失敗時の調査に使う
    $logFilePath = Join-Path $InterWorkPath 'msbuild.log'
    $msbuildArgs = @(
        "`"$msbuildTarget`"",
        '/t:Build',
        "/p:Configuration=$configName",
        '/v:normal',
        '/nr:false',
        '/m',
        '/fl',
        "/flp:LogFile=`"$logFilePath`";Verbosity=normal"
    )
    if ($Work.project_msbuild_platform) {
        $msbuildArgs += "/p:Platform=`"$($Work.project_msbuild_platform)`""
    }
    & $msbuildPath $msbuildArgs > $null
    if ($LASTEXITCODE -ne 0) {
        throw "MSBuild が終了コード $LASTEXITCODE で失敗しました。ログ: $logFilePath"
    }

    $stagingExec = $StagingPath
    Recreate-Directory -Path $stagingExec
    $copyList = $Work.project_build_copy_list
    if ($copyList -and $copyList.Count -gt 0) {
        # 一覧があれば、それだけを配置する
        foreach ($relItem in $copyList) {
            if ([string]::IsNullOrEmpty($relItem)) { continue }
            $resolvedSource = Join-Path $InterSourcePath $relItem
            if (-not (Test-Path $resolvedSource)) {
                throw "コピー対象が見つかりません: $resolvedSource"
            }
            Copy-ItemWithExclude -Source $resolvedSource `
                -Destination (Join-Path $stagingExec (Split-Path $resolvedSource -Leaf))
        }
    } else {
        $outputDir = Join-Path $InterSourcePath $Work.project_build_output_relative_path
        if (-not (Test-Path $outputDir)) {
            throw "出力フォルダが見つかりません: $outputDir"
        }
        # 出力が配布用 ZIP 1 個だけなら展開し、そうでなければフォルダごと配置する
        $outputZips = @(Get-ChildItem -LiteralPath $outputDir -Filter '*.zip' -File)
        $outputOthers = @(Get-ChildItem -LiteralPath $outputDir -Force | Where-Object { $_.Extension -ne '.zip' })
        if ($outputZips.Count -eq 1 -and $outputOthers.Count -eq 0) {
            Expand-AndFlattenArchive -ZipPath $outputZips[0].FullName `
                -TempExtractDir (Join-Path $InterWorkPath 'zip_extract') `
                -DestinationDir $stagingExec
        } elseif ($outputZips.Count -gt 1 -and $outputOthers.Count -eq 0) {
            throw "出力フォルダに ZIP が複数あります（$($outputZips.Count) 件）: $outputDir"
        } else {
            Copy-ItemWithExclude -Source $outputDir -Destination $stagingExec
        }
    }
}

<#
.SYNOPSIS
    EXE を短時間起動し、起動直後に異常終了しないかを確かめる簡易テスト。
.DESCRIPTION
    画面操作や機能の正しさは確認しない。起動したままなら finally で必ず終了させる。
    起動した作品がログ等を書き出しても提出物に混ざらないよう、ExecSourceDir を
    RunDir（Intermediate 配下）へ複製し、複製側で起動する。
.PARAMETER ExecSourceDir
    起動確認する実行ファイル一式のフォルダ。存在しなければ EXE なし扱い。
.PARAMETER RunDir
    起動に使う複製先（Intermediate 配下）。
.PARAMETER TestExecutableName
    起動する EXE 名。省略時はフォルダ内の EXE を自動検出。
.PARAMETER WaitSeconds
    起動後に待つ秒数。
.PARAMETER Required
    $true なら EXE が無いことを失敗とする。
#>
function Test-ExecutableSmoke {
    param(
        [string]$ExecSourceDir,
        [string]$RunDir,
        [string]$TestExecutableName,
        [int]$WaitSeconds = 3,
        [bool]$Required = $false
    )

    $sourceExePath = $null
    if (-not [string]::IsNullOrEmpty($ExecSourceDir) -and (Test-Path $ExecSourceDir)) {
        if ($TestExecutableName) {
            $sourceExePath = Join-Path $ExecSourceDir $TestExecutableName
        } else {
            $exeFiles = @(Get-ChildItem -LiteralPath $ExecSourceDir -Filter '*.exe' -File)
            if ($exeFiles.Count -gt 0) { $sourceExePath = $exeFiles[0].FullName }
        }
    }
    if ($null -eq $sourceExePath -or -not (Test-Path $sourceExePath)) {
        if ($Required) {
            throw "起動確認の対象 EXE が見つかりません: $ExecSourceDir"
        }
        Write-Host ' （EXE なし・起動確認は任意のため省略）' -NoNewline -ForegroundColor Yellow
        return
    }

    # 複製側で起動する（作品が書き出すログ等は RunDir にだけ残る）
    Recreate-Directory -Path $RunDir
    Copy-ItemWithExclude -Source $ExecSourceDir -Destination $RunDir
    $exePath = Join-Path $RunDir $sourceExePath.Substring($ExecSourceDir.TrimEnd('\').Length).TrimStart('\')

    $process = $null
    try {
        if (Get-Command Unblock-File -ErrorAction SilentlyContinue) {
            Unblock-File -Path $exePath -ErrorAction SilentlyContinue
        }
        $process = Start-Process -FilePath $exePath `
            -WorkingDirectory (Split-Path $exePath -Parent) -PassThru -WindowStyle Minimized `
            -ErrorAction Stop
        Start-Sleep -Seconds $WaitSeconds
        if ($process.HasExited -and $process.ExitCode -ne 0) {
            throw "起動後に終了コード $($process.ExitCode) でクラッシュしました。"
        }
    } finally {
        if ($null -ne $process -and -not $process.HasExited) {
            $process.Kill()
            $process.WaitForExit()
        }
    }
}
