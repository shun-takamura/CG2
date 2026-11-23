#pragma once
#include <string>
#include "DirectXTex.h"
#include <wrl.h>
#include <d3d12.h>
#include <functional>
#include <mutex>
#include <unordered_map>
#include "SpriteManager.h"
#include "DirectXCore.h"
#include"SRVManager.h"
#include<cassert>

/// <summary>
/// テクスチャの読み込み・GPU への転送・SRV の割り当てを行い、パスで引けるようにする。
/// シングルトンにする理由：テクスチャ（と SRV）をパスごとに 1 つだけ持ち、どの描画クラス・シーンからも同じ物を引くため。
/// </summary>
class TextureManager
{
private:

	// 既存のprivateセクションのTextureData構造体を修正:
	struct TextureData {
		// CPU/GPU 二段ロードの状態。Unloaded → CPUReady → GPUReady と遷移する。
		// GPUPending は DirectStorage へ送ったが転送の完了を待っていない（BeginTextureGPUAsync → FinishTextureGPU の間）
		enum class LoadState { Unloaded, CPUReady, GPUPending, GPUReady };
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

	// テクスチャデータの配列。読み込み済みのテクスチャ枚数のカウントが可能
	std::unordered_map<std::string, TextureData>textureDatas;

	// textureDatas への並行アクセス保護（バックグラウンドスレッドの CPU フェーズと
	// メインスレッドのクエリ・GPU フェーズが競合しないようにする）
	mutable std::mutex textureDatasMutex_;

	TextureManager() = default;
	~TextureManager() = default;
	TextureManager(const TextureManager&) = delete;
	TextureManager& operator=(const TextureManager&) = delete;

	SpriteManager* spriteManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;

	// LoadTexture / LoadTextureLinear が呼ばれたら知らせる先（シーンの読み込みの記録用。空なら何もしない）
	std::function<void(const std::string& filePath, bool linear)> loadObserver_;

	/// <summary>
	/// DirectStorage 経路の GPU フェーズ。COMMON 状態でリソースを作り、pack から VRAM への転送を登録して SRV を作る（GPUPending にする）
	/// </summary>
	/// <param name="data">CPU フェーズで DirectStorage 経路と判定されたテクスチャの情報</param>
	/// <param name="filePath">テクスチャのパス（ログ用）</param>
	void CreateDirectStorageTexture(TextureData& data, const std::string& filePath);
	/// <summary>DirectStorage の転送が終わった後の仕上げ。COMMON → GENERIC_READ のバリアを積んで GPUReady にする</summary>
	/// <param name="data">転送が終わったテクスチャの情報</param>
	void FinishDirectStorageTexture(TextureData& data);

public:

	/// <summary>使う各マネージャーを受け取る</summary>
	/// <param name="spriteManager">テクスチャリソースの作成と転送に使う SpriteManager</param>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	/// <param name="srvManager">SRV を確保する SRVManager</param>
	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore, SRVManager* srvManager);

	/// <summary>同期ロード（CPU + GPU を一気に行う既存 API。sRGB として読む）。読み込み済みなら何もしない</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	void LoadTexture(const std::string& filePath);

	/// <summary>
	/// SRGB変換せずに線形値として読み込む（マスク・ノーマルマップ用）。読み込み済みなら何もしない
	/// </summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	void LoadTextureLinear(const std::string& filePath);

	/// <summary>
	/// CPU フェーズ: ファイルを読んで ScratchImage を作るところまで。
	/// バックグラウンドスレッドから呼んで OK（GPU API は触らない）。既に登録があれば何もしない
	/// </summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <param name="linear">true なら sRGB 変換せず線形値として読む</param>
	void LoadTextureCPU(const std::string& filePath, bool linear = false);

	/// <summary>
	/// GPU フェーズ: ID3D12Resource 作成 / アップロード / SRV 作成。
	/// メインスレッド専用。事前に LoadTextureCPU を呼んでおくこと。
	/// </summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	void LoadTextureGPU(const std::string& filePath);

	/// <summary>
	/// GPU フェーズを待たずに始める（メインスレッド専用。事前に LoadTextureCPU）。
	/// DirectStorage 経路なら転送を登録して true を返す：呼び出し側が DStorageManager::SubmitWithFence で送り、
	/// fence が終わってから FinishTextureGPU を呼ぶ。それ以外の経路はその場で GPUReady まで進めて false
	/// </summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>DirectStorage の転送を登録したら true。CPUReady でない・その場で仕上げた場合は false</returns>
	bool BeginTextureGPUAsync(const std::string& filePath);
	/// <summary>BeginTextureGPUAsync の転送が終わった後の仕上げ（バリアを積んで GPUReady に）。メインスレッド専用</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	void FinishTextureGPU(const std::string& filePath);

	/// <summary>CPU フェーズが終わっているかを返す（スレッドセーフ）</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>CPUReady 以降なら true</returns>
	bool IsCPUReady(const std::string& filePath) const;
	/// <summary>GPU フェーズまで終わって使えるかを返す（スレッドセーフ）</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>GPUReady なら true</returns>
	bool IsGPUReady(const std::string& filePath) const;

	/// <summary>LoadTexture / LoadTextureLinear が呼ばれるたびに知らせる（キャッシュに当たっても）。空の関数で外す</summary>
	/// <param name="observer">パスと線形読み込みかを受け取る関数</param>
	void SetLoadObserver(std::function<void(const std::string& filePath, bool linear)> observer) { loadObserver_ = std::move(observer); }

	/// <summary>
	/// 動的テクスチャを作成（カメラ映像など毎フレーム更新するテクスチャ用）
	/// </summary>
	/// <param name="name">テクスチャの識別名</param>
	/// <param name="width">幅</param>
	/// <param name="height">高さ</param>
	/// <param name="format">フォーマット（デフォルトはBGRA）</param>
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
	/// テクスチャが登録されているかチェック（読み込み途中の物も含む）
	/// </summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>登録があれば true</returns>
	bool HasTexture(const std::string& filePath) const;

	/// <summary>
	/// 1x1 の単色テクスチャ（sRGB）を生成（ファイル不要）。同じ名前があれば何もしない
	/// </summary>
	/// <param name="name">テクスチャ名（キー）</param>
	/// <param name="r">赤 (0-255)</param>
	/// <param name="g">緑 (0-255)</param>
	/// <param name="b">青 (0-255)</param>
	/// <param name="a">アルファ (0-255)</param>
	void CreateSolidColorTexture(const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255);

	/// <summary>
	/// よそが作った SRV（RenderTexture など）を名前で引けるように登録する。モデルのテクスチャとして貼るとき用。
	/// 同じ名前は上書きする（作り直した RenderTexture を登録し直す）。SRV と resource の寿命は呼び出し側が持つ
	/// （登録している間 resource の参照は1つ持つ）
	/// </summary>
	/// <param name="name">引くときの名前（キー）</param>
	/// <param name="resource">テクスチャのリソース（メタデータはここから作る）</param>
	/// <param name="srvIndex">呼び出し側が作った SRV のスロット番号</param>
	void RegisterExternalTexture(const std::string& name, ID3D12Resource* resource, uint32_t srvIndex);
	/// <summary>RegisterExternalTexture の登録を外す（SRV を解放する前に呼ぶ）</summary>
	/// <param name="name">登録した名前</param>
	void UnregisterExternalTexture(const std::string& name) { textureDatas.erase(name); }

	/// <summary>シングルトンインスタンスを返す</summary>
	/// <returns>TextureManager のインスタンス</returns>
	static TextureManager* GetInstance();

	/// <summary>登録済みテクスチャ数を返す（P.E.P.P.E.R. 計測用）</summary>
	/// <returns>登録数（読み込み途中・外部登録を含む）</returns>
	size_t GetLoadedTextureCount() const { return textureDatas.size(); }

	/// <summary>メタデータを返す。未登録なら assert で止まる</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>メタデータ（未登録なら空のダミー）</returns>
	const DirectX::TexMetadata& GetMetaData(const std::string& filePath);

	/// <summary>SRV インデックスを返す。未登録なら assert で止まる</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>SRV インデックス（未登録なら 0）</returns>
	uint32_t GetSrvIndex(const std::string& filePath);

	/// <summary>SRV の GPU ハンドルを返す。未登録なら登録済みのキーをログに出して assert で止まる</summary>
	/// <param name="filePath">テクスチャのパス（登録キー）</param>
	/// <returns>GPU ハンドル（未登録なら空のハンドル）</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandleGPU(const std::string& filePath);

	/// <summary>登録している全テクスチャを破棄する</summary>
	void Finalize();

};