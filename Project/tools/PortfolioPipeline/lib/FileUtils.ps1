# コピー・展開の共通処理。除外ルールはここ 1 か所に集める。

<#
.SYNOPSIS
    フォルダを消して空の状態で作り直す。
.DESCRIPTION
    指定パスを再帰削除するため、許可した作業ルート配下以外は拒否する。
.PARAMETER Path
    作り直すフォルダ。
#>
function Recreate-Directory {
    param([string]$Path)

    Assert-InsideAllowedRoot -Path $Path
    if (Test-Path $Path) {
        Remove-Item -Path $Path -Recurse -Force | Out-Null
    }
    New-Item -Path $Path -ItemType Directory -Force | Out-Null
}

# 削除を許可する作業ルート（PortfolioPipeline.ps1 が Generated と Intermediate を登録する）
$script:AllowedDeleteRoots = @()

<#
.SYNOPSIS
    削除してよい作業ルートを登録する。
#>
function Set-AllowedDeleteRoots {
    param([string[]]$Roots)
    $script:AllowedDeleteRoots = $Roots | ForEach-Object { [System.IO.Path]::GetFullPath($_).TrimEnd('\') }
}

<#
.SYNOPSIS
    パスが登録済み作業ルートの配下（ルート自身を含む）でなければ throw する。
#>
function Assert-InsideAllowedRoot {
    param([string]$Path)

    $full = [System.IO.Path]::GetFullPath($Path).TrimEnd('\')
    foreach ($root in $script:AllowedDeleteRoots) {
        if ($full -eq $root -or $full.StartsWith($root + '\', [System.StringComparison]::OrdinalIgnoreCase)) {
            return
        }
    }
    throw "作業ルートの外は削除できません: $full"
}

<#
.SYNOPSIS
    提出物に含めない項目かを判定する。
.PARAMETER RelativePath
    コピー元ルートからの相対パス。途中のフォルダ名も判定対象。
.RETURNS
    除外するなら $true。
#>
function Test-ExcludedPath {
    param([string]$RelativePath)

    foreach ($part in ($RelativePath -split '\\')) {
        # .git / .vscode / .vs など、ドットで始まる管理・エディタ用ファイル
        if ($part -match '^\..+') { return $true }
        # OS が作る補助ファイル
        if ($part -eq 'desktop.ini' -or $part -eq 'Thumbs.db' -or $part -eq '__MACOSX') { return $true }
        # デバッグ情報
        if ($part -like '*.pdb') { return $true }
    }
    return $false
}

<#
.SYNOPSIS
    除外ルールを守りながら、ファイル／フォルダを階層ごとコピーする。
.PARAMETER Source
    コピー元（ファイルまたはフォルダ）。
.PARAMETER Destination
    コピー先フォルダ（Source がファイルならコピー先パス）。
#>
function Copy-ItemWithExclude {
    param(
        [string]$Source,
        [string]$Destination
    )

    if (Test-Path -Path $Source -PathType Leaf) {
        $parent = Split-Path $Destination -Parent
        if ($parent -and -not (Test-Path $parent)) {
            New-Item -Path $parent -ItemType Directory -Force | Out-Null
        }
        Copy-Item -Path $Source -Destination $Destination -Force
        return
    }
    if (-not (Test-Path $Destination)) {
        New-Item -Path $Destination -ItemType Directory -Force | Out-Null
    }

    $sourceNormalized = (Get-Item -LiteralPath $Source).FullName.TrimEnd('\')
    $sourceLen = $sourceNormalized.Length

    # -Force で隠しファイルも列挙し、除外判定を通す
    $items = Get-ChildItem -LiteralPath $sourceNormalized -Recurse -Force
    foreach ($item in $items) {
        $relPath = $item.FullName.Substring($sourceLen).TrimStart('\')
        if ([string]::IsNullOrEmpty($relPath)) { continue }
        if (Test-ExcludedPath -RelativePath $relPath) { continue }

        $targetPath = Join-Path $Destination $relPath
        if ($item.PSIsContainer) {
            New-Item -Path $targetPath -ItemType Directory -Force | Out-Null
        } else {
            $parentDir = Split-Path $targetPath -Parent
            if (-not (Test-Path $parentDir)) {
                New-Item -Path $parentDir -ItemType Directory -Force | Out-Null
            }
            Copy-Item -LiteralPath $item.FullName -Destination $targetPath -Force
        }
    }
}

<#
.SYNOPSIS
    ZIP を一時フォルダに展開し、ラッパーフォルダ 1 段を剥がして配置する。
.PARAMETER ZipPath
    展開する ZIP。
.PARAMETER TempExtractDir
    展開用の一時フォルダ（作業ルート配下）。
.PARAMETER DestinationDir
    配置先フォルダ。
#>
function Expand-AndFlattenArchive {
    param(
        [string]$ZipPath,
        [string]$TempExtractDir,
        [string]$DestinationDir
    )

    Recreate-Directory -Path $TempExtractDir
    Expand-Archive -LiteralPath $ZipPath -DestinationPath $TempExtractDir -Force

    # 中身が「フォルダ 1 個だけ」なら、その中を実際のルートとみなす
    $contentRoot = $TempExtractDir
    $topItems = @(Get-ChildItem -LiteralPath $TempExtractDir -Force | Where-Object { -not (Test-ExcludedPath -RelativePath $_.Name) })
    if ($topItems.Count -eq 1 -and $topItems[0].PSIsContainer) {
        $contentRoot = $topItems[0].FullName
    }

    Copy-ItemWithExclude -Source $contentRoot -Destination $DestinationDir
    Remove-Item -Path $TempExtractDir -Recurse -Force | Out-Null
}
