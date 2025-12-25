#pragma once
#include <string>
#include "DirectXTex.h"
#include <wrl.h>
#include <d3d12.h>
#include <vector>

#include "DirectXCore.h"

class TextureManager
{
private:

	// テクスチャ一枚分のデータ
	struct TextureData{
		std::string filePath; // 画像ファイルのパス
		DirectX::TexMetadata metadata; // 画像の幅や高さの情報
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; // テクスチャリソース
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU; // SRV作成時に必要なCPUハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU; // 描画コマンドに必要なGPUハンドル
	};

	// テクスチャデータの配列。読み込み済みのテクスチャ枚数のカウントが可能
	std::vector<TextureData> textureDatas;

	// シングルトンでインスタンスを一つだけ所有
	static TextureManager* instance;

	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = default;
	TextureManager& operator = (TextureManager&) = default;

public:

	void Initialize();

	void LoadTexture(const std::string& filePath);
	
	// シングルトンインスタンスの取得
	static TextureManager* GetInstance();

	// 終了
	void Finalize();

};

