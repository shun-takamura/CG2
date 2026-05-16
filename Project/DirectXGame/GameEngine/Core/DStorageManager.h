#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <dstorage.h>
#include <cstdint>
#include <string>

/// <summary>
/// DirectStorage 統合マネージャ。
///
/// 役割:
///   - IDStorageFactory / IDStorageQueue / Fence の生成・管理
///   - pack ファイル（IDStorageFile）の open
///   - メモリ / バッファ / テクスチャ subresource への転送リクエスト発行
///   - バッチ送出と完了待ち
///
/// 設計:
///   - Phase D.1 の時点では「同期待ちで 1 アセットを読む」用途
///   - 後で AssetLocator の pack モードと統合し、フェンスベースの非同期化（0-5）に乗せる
///   - 圧縮は Phase B.2/B.3 で GDeflate を CompressionFormat に追加する
/// </summary>
class DStorageManager {
public:
	static DStorageManager* GetInstance();

	bool Initialize(ID3D12Device* device);
	void Finalize();

	// pack ファイルを開く（UTF-8 相対パス）。dev/release で候補を順に試す。
	// 開いた IDStorageFile は内部保持され GetPackFile() で取得可。
	bool OpenPackFile(const std::string& path);
	IDStorageFile* GetPackFile() const { return packFile_.Get(); }

	// 転送先別の Enqueue API
	void EnqueueMemoryRead(IDStorageFile* file, uint64_t offset, uint32_t size, void* dst);
	void EnqueueBufferRead(IDStorageFile* file, uint64_t offset, uint32_t size,
	                       ID3D12Resource* dst, uint64_t dstOffset = 0);
	// width/height/depth は対象 subresource (mip) のサイズ。BC圧縮テクスチャでも
	// ピクセル単位で指定する（4の倍数でないと DStorage が拒否する）。depth=1 で 2D。
	void EnqueueTextureRead(IDStorageFile* file, uint64_t offset, uint32_t size,
	                        ID3D12Resource* dst, uint32_t subresource,
	                        uint32_t width, uint32_t height, uint32_t depth = 1);

	// DDS 風の連続レイアウト（mip 0 / mip 1 / ... が連続して並ぶ）を 1 リクエストで
	// 複数 subresource に転送する。
	void EnqueueMultipleSubresources(IDStorageFile* file, uint64_t offset, uint32_t size,
	                                 ID3D12Resource* dst, uint32_t firstSubresource);

	// バッチ送出（fence なし）
	void Submit();

	// バッチ送出 + 同期で完了まで待つ
	void SubmitAndWait();

	// バッチモード:
	//   BeginBatch() を呼ぶと以降の Submit / SubmitAndWait は no-op になり、
	//   Enqueue だけが蓄積される。EndBatchAndWait() でまとめて 1 回だけ Submit + Wait。
	//   シーン初期化全体を囲うことで NVMe の並列性と DStorage 内部スケジューラを活かす。
	void BeginBatch() { inBatch_ = true; }
	void EndBatchAndWait();
	bool IsInBatch() const { return inBatch_; }

	// 初期化済み かつ 明示的に無効化されていない (KPI 比較用に --no-dstorage で切れる)
	bool IsInitialized() const { return initialized_ && enabled_; }
	// CLI フラグ等で実体は初期化したまま経路だけ封じる
	void SetEnabled(bool e) { enabled_ = e; }

private:
	DStorageManager() = default;
	~DStorageManager();
	DStorageManager(const DStorageManager&) = delete;
	DStorageManager& operator=(const DStorageManager&) = delete;

	Microsoft::WRL::ComPtr<IDStorageFactory> factory_;
	Microsoft::WRL::ComPtr<IDStorageQueue>   queue_;
	Microsoft::WRL::ComPtr<ID3D12Fence>      fence_;
	uint64_t                                 fenceValue_ = 0;
	HANDLE                                   fenceEvent_ = nullptr;

	Microsoft::WRL::ComPtr<IDStorageFile>    packFile_;
	bool                                     initialized_ = false;
	bool                                     enabled_     = true;
	bool                                     inBatch_     = false;
};
