# config.psd1 の検証。開始前に止めるべき設定エラーをここで throw する。

<#
.SYNOPSIS
    config.psd1 の必須項目と取得元の指定を検証する。
.PARAMETER Config
    Import-PowerShellDataFile で読み込んだ設定。
.PARAMETER RootPath
    相対パスを解決する基準（PortfolioPipeline.ps1 のあるフォルダ）。
#>
function Assert-Config {
    param(
        [hashtable]$Config,
        [string]$RootPath
    )

    if ($null -eq $Config.output) {
        throw 'config.psd1 に output がありません。'
    }
    foreach ($field in @('zip_name', 'generated_dir')) {
        if ([string]::IsNullOrEmpty($Config.output.$field)) {
            throw "output の必須項目 $field が不足しています。"
        }
    }

    # 提出物を作るバッチなので、作品が 0 件の実行は設定ミスとして止める
    if ($null -eq $Config.works -or $Config.works.Count -eq 0) {
        throw 'works が空です。作品を 1 件以上設定してください。'
    }

    $names = @{}
    foreach ($work in $Config.works) {
        if ([string]::IsNullOrEmpty($work.work_folder_name)) {
            throw '作品フォルダ名 (work_folder_name) が不足しています。'
        }
        $name = $work.work_folder_name
        if ($names.ContainsKey($name)) {
            throw "work_folder_name '$name' が重複しています。"
        }
        $names[$name] = $true

        # ソースの取得元は Git かローカルのどちらか一方だけ
        $hasGit = -not [string]::IsNullOrEmpty($work.git_repository_url)
        $hasLocal = -not [string]::IsNullOrEmpty($work.local_source_path)
        $sourceCount = [int]$hasGit + [int]$hasLocal
        if ($sourceCount -eq 0) {
            throw "作品 '$name': 取得元 (git_repository_url / local_source_path) を指定してください。"
        }
        if ($sourceCount -gt 1) {
            throw "作品 '$name': Git とローカルパスはどちらか一方だけ指定してください。"
        }
        if ($hasLocal -and -not (Test-Path (Resolve-ConfigPath -Path $work.local_source_path -RootPath $RootPath))) {
            throw "作品 '$name': local_source_path が存在しません: $($work.local_source_path)"
        }

        # 実行ファイルの取得元も一方だけ（Drive の ZIP / ローカル / ビルド）
        $hasZip = -not [string]::IsNullOrEmpty($work.google_drive_zip_id)
        $hasLocalExe = -not [string]::IsNullOrEmpty($work.local_executable_path)
        $hasBuild = -not [string]::IsNullOrEmpty($work.project_msbuild_target_path)
        $exeCount = [int]$hasZip + [int]$hasLocalExe + [int]$hasBuild
        if ($exeCount -gt 1) {
            throw "作品 '$name': google_drive_zip_id / local_executable_path / project_msbuild_target_path はどれか 1 つだけ指定してください。"
        }
        if ($hasLocalExe -and -not (Test-Path (Resolve-ConfigPath -Path $work.local_executable_path -RootPath $RootPath))) {
            throw "作品 '$name': local_executable_path が存在しません: $($work.local_executable_path)"
        }
        if ($hasBuild) {
            $hasCopyList = $null -ne $work.project_build_copy_list -and $work.project_build_copy_list.Count -gt 0
            if (-not $hasCopyList -and [string]::IsNullOrEmpty($work.project_build_output_relative_path)) {
                throw "作品 '$name': ビルドする場合は project_build_copy_list か project_build_output_relative_path が必要です。"
            }
        }
    }
}

<#
.SYNOPSIS
    設定に書かれたパスを絶対パスにする。相対パスは RootPath 基準。
.RETURNS
    絶対パス。
#>
function Resolve-ConfigPath {
    param(
        [string]$Path,
        [string]$RootPath
    )
    if ([System.IO.Path]::IsPathRooted($Path)) {
        return $Path
    }
    return [System.IO.Path]::GetFullPath((Join-Path $RootPath $Path))
}
