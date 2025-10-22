#pragma once
#include <string>
#include <wrl.h>
#include <dxcapi.h>

class ResourceManager {

public:

	/// <summary>
	/// 指定されたHLSLファイルをDXCでコンパイルし、バイナリBlobを返す。
	/// </summary>
	/// <param name="fileName">シェーダーファイル名（例: L"Object3d.VS.hlsl"）</param>
	/// <param name="profile">シェーダープロファイル（例: L"vs_6_0", L"ps_6_0"）</param>
	/// <param name="dxcUtils">DXCユーティリティ（ファイル読み込み等を行う）</param>
	/// <param name="dxcCompiler">DXCコンパイラ（HLSLコンパイル本体）</param>
	/// <param name="includeHandler">include対応ハンドラ</param>
	/// <returns></returns>
	static Microsoft::WRL::ComPtr<IDxcBlob> CompileShader(
		const std::wstring& fileName,
		const wchar_t* profile,
		IDxcUtils* dxcUtils,
		IDxcCompiler3* dxcCompiler,
		IDxcIncludeHandler* includeHandler);

};

