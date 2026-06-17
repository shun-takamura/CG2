#pragma once
#include <string>
#include "DirectXTex.h"
#include <wrl.h>
#include <d3d12.h>
#include <mutex>
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
		// CPU/GPU 二段ロードの状態。Unloaded → CPUReady → GPUReady と遷移する。
		enum class LoadState { Unloaded, CPUReady, GPUReady };
		LoadState loadState = LoadState::Unloaded;

		DirectX::TexMetadata metadata;
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		Microsoft::WRL::ComPtr<ID3D12Resource> uploadBuffer;  // 追加: 動的テクスチャ用
		uint32_t srvIndex;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandleCPU;
		D3D12_GPU_DESCRIPTOR_HANDLE srvHandleGPU;
		bool isDynamic = false;  // 追加: 動的テクスチャフラグ

		// CPU フェーズで作成し GPU フェーズで消費するデコード済み画像。
		// GPU アップロード後は解放してメモリを返す。
		DirectX::ScratchImage cpuImage;
		bool isLinear = false;  // 線形読み込み（マスク等）なら true

		// DirectStorage 経路（pack モード時のみ）。CPU メモリを経由せず VRAM へ直接ロードする。
		bool     useDirectStorage = false;
		uint64_t dsPayloadOffset = 0;     // pack ファイル先頭からの DDS payload 開始オフセット
		uint32_t dsPayloadSize = 0;       // pack 上の実バイト数 (圧縮ありなら圧縮済みサイズ)
		uint32_t dsUncompressedSize = 0;  // 解凍後 payload サイズ (DDS header を除いた残り)
		bool     dsCompressed = false;    // GDeflate 圧縮されているか
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

	// textureDatas への並行アクセス保護（バックグラウンドスレッドの CPU フェーズと
	// メインスレッドのクエリ・GPU フェーズが競合しないようにする）
	mutable std::mutex textureDatasMutex_;

	// シングルトンでインスタンスを一つだけ所有
	//static TextureManager* instance;

	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(TextureManager&) = default;
	TextureManager& operator = (TextureManager&) = default;

	/*SpriteManager* spriteManager_;
	DirectXCore* dxCore_;*/
	SpriteManager* spriteManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

public:

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore, SRVManager* srvManager);

	// 同期ロード（CPU + GPU を一気に行う既存 API）
	void LoadTexture(const std::string& filePath);

	/// <summary>
	/// SRGB変換せずに線形値として読み込む（マスク・ノーマルマップ用）
	/// </summary>
	void LoadTextureLinear(const std::string& filePath);

	/// <summary>
	/// CPU フェーズ: ファイルを読んで ScratchImage を作るところまで。
	/// バックグラウンドスレッドから呼んで OK（GPU API は触らない）。
	/// </summary>
	void LoadTextureCPU(const std::string& filePath, bool linear = false);

	/// <summary>
	/// GPU フェーズ: ID3D12Resource 作成 / アップロード / SRV 作成。
	/// メインスレッド専用。事前に LoadTextureCPU を呼んでおくこと。
	/// </summary>
	void LoadTextureGPU(const std::string& filePath);

	// 非同期ロードの状態クエリ
	bool IsCPUReady(const std::string& filePath) const;
	bool IsGPUReady(const std::string& filePath) const;

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

	/// <summary>
	/// 単色テクスチャを生成（ファイル不要）
	/// </summary>
	/// <param name="name">テクスチャ名（キー）</param>
	/// <param name="r">赤 (0-255)</param>
	/// <param name="g">緑 (0-255)</param>
	/// <param name="b">青 (0-255)</param>
	/// <param name="a">アルファ (0-255)</param>
	void CreateSolidColorTexture(const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

	// シングルトンインスタンスの取得
	static TextureManager* GetInstance();

	// 読み込み済みテクスチャ数（P.E.P.P.E.R. 計測用）
	size_t GetLoadedTextureCount() const { return textureDatas.size(); }

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