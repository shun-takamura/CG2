#pragma once
#include <string>

class PathManager{

public:
    /// <summary>
    /// リソースディレクトリのパスを取得する関数
    /// </summary>
    /// <returns>リソースディレクトリのファイルパス</returns>
    static const std::wstring& GetResourceDir();

    /// <summary>
    /// シェーダーディレクトリのパスを取得する関数
    /// </summary>
    /// <returns>シェーダーディレクトリのファイルパス</returns>
    static const std::wstring& GetShaderDir();
};

