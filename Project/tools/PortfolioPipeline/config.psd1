# PortfolioPipeline.ps1 の設定。作品を増やすときは works に要素を追加する。
#
# output（全体で共通）
#   zip_name               : 提出フォルダ名・ZIP 名（必須）
#   generated_dir          : 成果物の出力先。相対パスはこのファイルのフォルダ基準（必須）
#   project_folder_name    : ソースを置くフォルダ名（省略時「ソースコード」）
#   executable_folder_name : 実行ファイルを置くフォルダ名（省略時「実行ファイル」）
#
# works（作品ごと）
#   work_folder_name       : 作品フォルダ名（必須・重複不可）
#   ソースの取得元（どちらか一方だけ必須）
#     git_repository_url   : Git リポジトリ URL（--depth 1 で取得）
#     local_source_path    : ローカルフォルダ
#   google_drive_file_id   : 説明資料（PDF・Google スライド等）の Drive ファイル ID（任意）
#   実行ファイルの取得元（どれか 1 つ・任意）
#     google_drive_zip_id  : 実行ファイル ZIP の Drive ファイル ID
#     local_executable_path: ローカルの ZIP またはフォルダ
#     project_msbuild_target_path : 取得したソース内の .sln / .vcxproj（ソース基準の相対パス）
#       project_msbuild_configuration      : 省略時 Release
#       project_msbuild_platform           : 例 x64
#       project_build_copy_list            : 配置するファイル／フォルダの一覧（ソース基準）
#       project_build_output_relative_path : 一覧が無いときに配置する出力フォルダ（ソース基準）
#                                            中身が ZIP 1 個だけなら展開して配置する
#   test_executable_name   : 起動確認する EXE 名（省略時は自動検出）
#   smoke_test_required    : $true なら EXE が無いと失敗にする（省略時 $false）
@{
    output = @{
        zip_name               = 'LE3B_18_タカムラ_シュン_QR'
        generated_dir          = '.\Generated'
        project_folder_name    = 'ソースコード'
        executable_folder_name = '実行ファイル'
    }
    works = @(
        @{
            work_folder_name                   = '01_個人作品'
            git_repository_url                 = 'https://github.com/shun-takamura/CG2.git'
            google_drive_file_id               = ''
            project_msbuild_target_path        = 'Project\CG2_0_1.sln'
            project_msbuild_configuration      = 'Release'
            project_msbuild_platform           = 'x64'
            project_build_output_relative_path = 'Generated\Output\Release\Distribution'
            test_executable_name               = 'CG2_0_1.exe'
            smoke_test_required                = $true
        }
    )
}
