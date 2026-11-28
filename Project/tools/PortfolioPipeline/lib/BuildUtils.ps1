# ビルド環境の検出。

<#
.SYNOPSIS
    MSBuild.exe の場所を探す。vswhere を優先し、無ければ代表的な配置先を順に確認する。
.RETURNS
    MSBuild.exe のフルパス。見つからなければ throw。
#>
function Find-MSBuildPath {
    $msbuildPath = $null

    $vswherePath = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (Test-Path $vswherePath) {
        $vsInstallPath = & $vswherePath -latest -products * `
            -requires Microsoft.Component.MSBuild `
            -property installationPath
        if (-not [string]::IsNullOrEmpty($vsInstallPath)) {
            $msbuildPath = Join-Path $vsInstallPath 'MSBuild\Current\Bin\MSBuild.exe'
        }
    }

    if ($null -eq $msbuildPath -or -not (Test-Path $msbuildPath)) {
        $defaultPaths = @(
            'C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe',
            'C:\Program Files\Microsoft Visual Studio\2022\Professional\MSBuild\Current\Bin\MSBuild.exe',
            'C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe',
            'C:\Program Files (x86)\Microsoft Visual Studio\2019\Community\MSBuild\Current\Bin\MSBuild.exe'
        )
        foreach ($path in $defaultPaths) {
            if (Test-Path $path) {
                $msbuildPath = $path
                break
            }
        }
    }

    if ($null -eq $msbuildPath -or -not (Test-Path $msbuildPath)) {
        throw 'MSBuild.exe が見つかりませんでした。Visual Studio（C++ によるデスクトップ開発）をインストールしてください。'
    }
    return $msbuildPath
}
