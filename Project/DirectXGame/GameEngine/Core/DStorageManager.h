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
/// シングルトンにする理由：DirectStorage のファクトリとキューはプロセスに 1 つだけ作るため。
/// </summary>
class DStorageManager {
public:
	/// <summary>唯一のインスタンス</summary>
	/// <returns>インスタンス</returns>
	static DStorageManager* GetInstance();

	/// <summary>
	/// Factory・Queue・Fence を作る（ステージングバッファは 256MB に広げる）。初期化済みなら何もしない
	/// </summary>
	/// <param name="device">D3D12 デバイス</param>
	/// <returns>成功（または初期化済み）なら true、どれかの作成に失敗したら false</returns>
	bool Initialize(ID3D12Device* device);
	/// <summary>pack ファイル・Fence・Queue・Factory を解放する（未初期化なら何もしない）</summary>
	void Finalize();

	/// <summary>pack ファイルを開いて内部に持つ（以後 GetPackFile で取れる）</summary>
	/// <param name="path">pack ファイルのパス（UTF-8、相対パス可）</param>
	/// <returns>開けたら true。未初期化・ファイルが無い・開けなかったら false</returns>
	bool OpenPackFile(const std::string& path);
	/// <summary>開いている pack ファイル</summary>
	/// <returns>pack ファイル（開いていなければ nullptr）</returns>
	IDStorageFile* GetPackFile() const { return packFile_.Get(); }

	/// <summary>ファイルの一部を CPU メモリへ読む要求を積む（未初期化・引数が空なら何もしない）</summary>
	/// <param name="file">読むファイル</param>
	/// <param name="offset">ファイル上の開始位置 [byte]</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <param name="dst">書き込む先（完了まで生かしておく）</param>
	void EnqueueMemoryRead(IDStorageFile* file, uint64_t offset, uint32_t size, void* dst);
	/// <summary>ファイルの一部を GPU バッファへ読む要求を積む（未初期化・引数が空なら何もしない）</summary>
	/// <param name="file">読むファイル</param>
	/// <param name="offset">ファイル上の開始位置 [byte]</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <param name="dst">書き込む先のバッファ</param>
	/// <param name="dstOffset">バッファ上の書き込み位置 [byte]</param>
	void EnqueueBufferRead(IDStorageFile* file, uint64_t offset, uint32_t size,
	                       ID3D12Resource* dst, uint64_t dstOffset = 0);
	/// <summary>ファイルの一部をテクスチャの 1 つの subresource 全体へ読む要求を積む（未初期化・引数が空なら何もしない）</summary>
	/// <param name="file">読むファイル</param>
	/// <param name="offset">ファイル上の開始位置 [byte]</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <param name="dst">書き込む先のテクスチャ</param>
	/// <param name="subresource">subresource の番号</param>
	/// <param name="width">subresource（mip）の幅 [px]。BC 圧縮でもピクセル単位（4 の倍数でないと DStorage が拒否する）</param>
	/// <param name="height">subresource（mip）の高さ [px]</param>
	/// <param name="depth">奥行き（1 で 2D）</param>
	void EnqueueTextureRead(IDStorageFile* file, uint64_t offset, uint32_t size,
	                        ID3D12Resource* dst, uint32_t subresource,
	                        uint32_t width, uint32_t height, uint32_t depth = 1);

	/// <summary>
	/// DDS 風の連続レイアウト（mip 0 / mip 1 / ... が連続して並ぶ）を 1 リクエストで
	/// 複数 subresource に転送する要求を積む（未初期化・引数が空なら何もしない）
	/// </summary>
	/// <param name="file">読むファイル</param>
	/// <param name="offset">ファイル上の開始位置 [byte]</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <param name="dst">書き込む先のテクスチャ</param>
	/// <param name="firstSubresource">最初に書く subresource の番号</param>
	void EnqueueMultipleSubresources(IDStorageFile* file, uint64_t offset, uint32_t size,
	                                 ID3D12Resource* dst, uint32_t firstSubresource);

	/// <summary>GDeflate 圧縮された範囲を解凍しつつ複数 subresource に転送する要求を積む（未初期化・引数が空なら何もしない）</summary>
	/// <param name="file">読むファイル</param>
	/// <param name="srcOffset">ファイル上の開始位置 [byte]</param>
	/// <param name="srcSize">pack 上の圧縮済みサイズ [byte]</param>
	/// <param name="uncompressedSize">解凍後のサイズ [byte]</param>
	/// <param name="dst">書き込む先のテクスチャ</param>
	/// <param name="firstSubresource">最初に書く subresource の番号</param>
	void EnqueueMultipleSubresourcesCompressed(
		IDStorageFile* file, uint64_t srcOffset, uint32_t srcSize,
		uint32_t uncompressedSize,
		ID3D12Resource* dst, uint32_t firstSubresource);

	/// <summary>積んだ要求を送る（fence なし、完了は待たない）。バッチ中は何もしない</summary>
	void Submit();

	/// <summary>積んだ要求を fence 付きで送り、完了までブロックして待つ。バッチ中は何もしない</summary>
	void SubmitAndWait();

	/// <summary>
	/// 待たずに送る：ここまでの Enqueue の後に fence の Signal を積んで送出し、その fence 値を返す。
	/// 終わったかは IsFenceComplete で見る（裏での先読み・ストリーミング用）。バッチ中は送出だけ EndBatchAndWait に任せる
	/// </summary>
	/// <returns>完了を調べる fence 値（未初期化なら 0）</returns>
	uint64_t SubmitWithFence();
	/// <summary>SubmitWithFence で送った要求が終わったか</summary>
	/// <param name="fenceValue">SubmitWithFence の戻り値</param>
	/// <returns>終わっていれば true（未初期化・fenceValue が 0 の時も true）</returns>
	bool IsFenceComplete(uint64_t fenceValue) const;

	/// <summary>
	/// バッチモードを始める。以降の Submit / SubmitAndWait は no-op になり Enqueue だけが蓄積される。
	/// シーン初期化全体を囲うことで NVMe の並列性と DStorage 内部スケジューラを活かす
	/// </summary>
	void BeginBatch() { inBatch_ = true; }
	/// <summary>バッチモードを終え、溜めた要求をまとめて 1 回だけ送って完了まで待つ</summary>
	void EndBatchAndWait();
	/// <summary>バッチモード中か</summary>
	/// <returns>バッチ中なら true</returns>
	bool IsInBatch() const { return inBatch_; }

	/// <summary>環境が GDeflate を GPU で解凍できるか調べる（KPI / 診断用）</summary>
	/// <returns>対応状況のフラグ（未初期化なら NONE）</returns>
	DSTORAGE_COMPRESSION_SUPPORT QueryGDeflateSupport() const;

	/// <summary>DirectStorage の経路が使えるか</summary>
	/// <returns>初期化済み かつ 明示的に無効化されていなければ true（--no-dstorage で切れる）</returns>
	bool IsInitialized() const { return initialized_ && enabled_; }
	/// <summary>実体は初期化したまま経路だけ有効・無効にする（CLI フラグ等から）</summary>
	/// <param name="e">使うなら true</param>
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
