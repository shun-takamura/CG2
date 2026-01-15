#pragma once
#include <string>
#include "DirectXTex.h"
#include <wrl.h>
#include <d3d12.h>
#include <unordered_map>
#include "SpriteManager.h"
#include "DirectXCore.h"
#include"SRVManager.h"
#include<cassert>

class TextureManager
{
private:

	// SRVインデックスの開始番号(0番はImGui)
	static uint32_t kSRVIndexTop;

	// テクスチャ一枚分のデータ
	struct TextureData{
		//std::string filePath; // 画像ファイルのパス
		DirectX::TexMetadata metadata; // 画像の幅や高さの情報
		Microsoft::WRL::ComPtr<ID3D12Resource> resource; // テクスチャリソース
		uint32_t srvIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU; // SRV作成時に必要なCPUハンドル
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU; // 描画コマンドに必要なGPUハンドル
	};

	// テクスチャデータの配列。読み込み済みのテクスチャ枚数のカウントが可能
	//std::vector<TextureData> textureDatas;
	std::unordered_map<std::string, TextureData>textureDatas;

	// シングルトンでインスタンスを一つだけ所有
	static TextureManager* instance;

	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = default;
	TextureManager& operator = (TextureManager&) = default;

	/*SpriteManager* spriteManager_;
	DirectXCore* dxCore_;*/
	SpriteManager* spriteManager_ = nullptr;
	DirectXCore * dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

public:

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,SRVManager*srvManager);

	void LoadTexture(const std::string& filePath);
	
	// シングルトンインスタンスの取得
	static TextureManager* GetInstance();

	// SRVインデックスの開始番号
	//uint32_t GetTextureIndexByFilePath(const std::string& filePath);

	// テクスチャ番号からGPUハンドルを取得
	//D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(uint32_t textureIndex);

	// メタデータを取得
	//const DirectX::TexMetadata& GetMetaData(uint32_t textureIndex);

	// メタデータの取得
	const DirectX::TexMetadata& GetMetaData(const std::string& filePath);

	// SRVインデックスの取得
	uint32_t GetSrvIndex(const std::string& filePath);

	// GPUハンドルの取得
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filePath);

	// 終了
	void Finalize();

};

