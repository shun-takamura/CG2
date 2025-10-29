#include "ResourceManager.h"
#include "PathManager.h"
#include <cassert>
#include <iostream>
#include <format>

#pragma comment(lib, "dxcompiler.lib")

using Microsoft::WRL::ComPtr;

ComPtr<IDxcBlob> ResourceManager::CompileShader(
    const std::wstring& fileName,
    const wchar_t* profile,
    IDxcUtils* dxcUtils,
    IDxcCompiler3* dxcCompiler,
    IDxcIncludeHandler* includeHandler)
{

    std::wcout << L"[DEBUG] Full shader path: " << fileName << std::endl;

    // =========================
    // 1. パスを構築する
    // =========================
    // PathManagerを使用してシェーダーのディレクトリのパスを取得し、ファイル名と連結
    std::wstring filePath = PathManager::GetShaderDir() + fileName;
    std::wcout << std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile);

    // =========================
    // 2. HLSLファイルを読み込む
    // =========================
    OutputDebugStringW((L"Failed to load shader file: " + filePath + L"\n").c_str());

    ComPtr<IDxcBlobEncoding> shaderSource = nullptr;
    HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, &shaderSource);
    if (FAILED(hr) || shaderSource == nullptr) {
        std::wcerr << L"Failed to load shader file: " << filePath << std::endl;
        assert(false);
        return nullptr;
    }

    DxcBuffer shaderSourceBuffer = {};
    shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
    shaderSourceBuffer.Size = shaderSource->GetBufferSize();
    shaderSourceBuffer.Encoding = DXC_CP_UTF8;

    // ==========================
    // 3. コンパイル設定
    // ==========================
    LPCWSTR arguments[] = {
        filePath.c_str(),          // コンパイル対象のHLSLファイル名
        L"-E", L"main",            // エントリーポイント指定（通常main固定）
        L"-T", profile,            // シェーダープロファイル指定
        L"-Zi", L"-Qembed_debug",  // デバッグ情報を埋め込む
        L"-Od",                    // 最適化をオフにする（デバッグ用）
        L"-Zpr",                   // 行優先メモリレイアウト
        L"-I", L"Resources/Shaders"
    };

    ComPtr<IDxcResult> shaderResult = nullptr;
    hr = dxcCompiler->Compile(
        &shaderSourceBuffer,
        arguments,
        _countof(arguments),
        includeHandler,
        IID_PPV_ARGS(&shaderResult)
    );

    if (FAILED(hr) || shaderResult == nullptr) {
        std::wcerr << L"Shader compile failed (DXC invocation): " << filePath << std::endl;
        assert(false);
        return nullptr;
    }

    // =============================
    // 4. エラー・警告チェック
    // =============================
    ComPtr<IDxcBlobUtf8> shaderError = nullptr;
    shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
    if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
        OutputDebugStringA(shaderError->GetStringPointer());
        OutputDebugStringA("\n");
        //std::cerr << shaderError->GetStringPointer() << std::endl;
        //assert(false);
        return nullptr;
    }

    // =============================
    // 5. コンパイル結果を取得
    // =============================
    ComPtr<IDxcBlob> shaderBlob = nullptr;
    hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
    if (FAILED(hr) || shaderBlob == nullptr) {
        std::wcerr << L"Failed to retrieve compiled shader blob: " << filePath << std::endl;
        assert(false);
        return nullptr;
    }

    std::wcout << std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile);
    return shaderBlob;
}
