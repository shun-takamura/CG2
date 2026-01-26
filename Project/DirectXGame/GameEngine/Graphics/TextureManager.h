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



	// 既存のprivateセクションのTextureData構造体を修正:
	struct TextureData {
		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;  // 追加: 動的テクスチャ用
		uint32_t srvIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
		bool isDynamic = false;  // 追加: 動的テクスチャフラグ
	};

	// SRVインデックスの開始番号(0番はImGui)
	static uint32_t kSRVIndexTop;

	//// テクスチャ一枚分のデータ
	//struct TextureData{
	//	//std::string filePath; // 画像ファイルのパス
	//	DirectX::TexMetadata metadata; // 画像の幅や高さの情報
	//	Microsoft::WRL::ComPtr<ID3D12Resource> resource; // テクスチャリソース
	//	uint32_t srvIndex;
	//	D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU; // SRV作成時に必要なCPUハンドル
	//	D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU; // 描画コマンドに必要なGPUハンドル
	//};

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
	
	/// <summary>
    /// 動的テクスチャを作成（カメラ映像など毎フレーム更新するテクスチャ用）
    /// </summary>
    /// <param name="name">テクスチャの識別名</param>
    /// <param name="width">幅</param>
    /// <param name="height">高さ</param>
    /// <param name="format">フォーマット（デフォルトはRGBA）</param>
	void CreateDynamicTexture(const std::string& name, uint32_t width, uint32_t height,
		DXGI_FORMAT format = DXGI_FORMAT_B8G8R8A8_UNORM);

	/// <summary>
	/// 動的テクスチャを更新
	/// </summary>
	/// <param name="name">テクスチャの識別名</param>
	/// <param name="data">ピクセルデータ</param>
	/// <param name="dataSize">データサイズ</param>
	void UpdateDynamicTexture(const std::string& name, const uint8_t* data, size_t dataSize);

	/// <summary>
	/// テクスチャが存在するかチェック
	/// </summary>
	bool HasTexture(const std::string& filePath) const;

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

