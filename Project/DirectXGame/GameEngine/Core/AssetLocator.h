#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

/// <summary>
/// アセットのパスを、pack の中かファイルかに振り分けて読み込む。
/// シングルトンにする理由：アセットの読み込み元（pack かファイルか）をプロセス全体で 1 つに決めるため。
/// </summary>
class AssetLocator;

// =====================================================================
// AssetHandle — 1 アセットに対する読み出しハンドル
//
// FS 経路: ifstream を内部に保持し ReadAt で seek + read
// pack 経路: 共有 pack ファイルへの参照とエントリ内オフセットを保持（B.2 で実装）
// =====================================================================
class AssetHandle {
public:
	AssetHandle() = default;
	AssetHandle(const AssetHandle&) = delete;
	AssetHandle& operator=(const AssetHandle&) = delete;
	AssetHandle(AssetHandle&&) noexcept = default;
	AssetHandle& operator=(AssetHandle&&) noexcept = default;

	/// <summary>読み出せるハンドルか</summary>
	/// <returns>開けていれば true</returns>
	bool IsValid() const { return valid_; }
	/// <summary>アセットの大きさ（pack の圧縮エントリでは圧縮済みの大きさ）</summary>
	/// <returns>大きさ [byte]</returns>
	uint64_t GetSize() const { return size_; }
	/// <summary>Read の現在位置</summary>
	/// <returns>位置 [byte]</returns>
	uint64_t GetPosition() const { return position_; }

	/// <summary>指定オフセットから size バイトを読み出す。Read の位置は更新しない</summary>
	/// <param name="offset">アセット先頭からの位置 [byte]</param>
	/// <param name="dst">書き込む先</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <returns>読めたら true。無効なハンドル・範囲がアセットの外なら false</returns>
	bool ReadAt(uint64_t offset, void* dst, size_t size);

	/// <summary>現在位置から size バイトを読み、位置を進める（ifstream::read 相当）</summary>
	/// <param name="dst">書き込む先</param>
	/// <param name="size">読む大きさ [byte]</param>
	/// <returns>読めたら true（失敗した時は位置を進めない）</returns>
	bool Read(void* dst, size_t size);

	/// <summary>読み出し位置を移動する（ifstream::seekg 相当。範囲は確かめない）</summary>
	/// <param name="offset">アセット先頭からの位置 [byte]</param>
	void Seek(uint64_t offset) { position_ = offset; }

private:
	friend class AssetLocator;

	bool     valid_ = false;
	uint64_t size_ = 0;
	uint64_t position_ = 0;  // Read() の現在位置
	uint64_t baseOffset_ = 0;  // pack モード時の payload 開始位置（FS モードは 0）

	// FS / pack 共通の入力ストリーム
	// FS モード: 各アセットファイルへのストリーム
	// pack モード: pack ファイルへのストリーム（baseOffset_ 起点で読む）
	std::unique_ptr<std::ifstream> stream_;
};

// =====================================================================
// AssetLocator — アセットの場所を抽象化するシングルトン
//
// 開発時: InitializeFromFilesystem() でファイル直読み
// リリース時: InitializeFromPack() で .pack 経由（B.2 で実装）
// =====================================================================
class AssetLocator {
public:
	/// <summary>唯一のインスタンス</summary>
	/// <returns>インスタンス</returns>
	static AssetLocator* GetInstance();

	/// <summary>個別ファイル直読みモードで初期化する（pack の目次は捨てる）</summary>
	void InitializeFromFilesystem();

	/// <summary>.pack 経由モードで初期化する（ヘッダーと目次を読み込む）</summary>
	/// <param name="packPath">pack ファイルのパス</param>
	/// <returns>成功なら true。開けない・マジックやバージョンが違う時は false（モードは変えない）</returns>
	bool InitializeFromPack(const std::string& packPath);

	/// <summary>部分読み出し用のハンドルを開く（主要 API）</summary>
	/// <param name="path">アセットのパス</param>
	/// <returns>ハンドル（見つからない・空のファイル・未初期化なら IsValid が false）</returns>
	AssetHandle Open(const std::string& path);

	/// <summary>ファイル全体を一括で読み込む（小サイズ向け）。pack の圧縮エントリは圧縮されたまま返す</summary>
	/// <param name="path">アセットのパス</param>
	/// <returns>中身（見つからない・空・未初期化なら空）</returns>
	std::vector<uint8_t> LoadAll(const std::string& path);

	/// <summary>アセットがあるか</summary>
	/// <param name="path">アセットのパス</param>
	/// <returns>あれば true（未初期化なら false）</returns>
	bool Exists(const std::string& path) const;

	/// <summary>pack 内のエントリの位置を取る（DirectStorage 統合用）</summary>
	/// <param name="path">アセットのパス</param>
	/// <param name="outPackOffset">pack 上の開始位置 [byte] を書く先</param>
	/// <param name="outSize">解凍後の大きさ [byte] を書く先</param>
	/// <returns>見つかったら true。FS モードでは常に false</returns>
	bool GetPackEntryInfo(const std::string& path,
	                     uint64_t& outPackOffset, uint64_t& outSize) const;

	/// <summary>pack 内のエントリの位置と圧縮情報を取る（GDeflate 対応の拡張版）</summary>
	/// <param name="path">アセットのパス</param>
	/// <param name="outPackOffset">pack 上の開始位置 [byte] を書く先</param>
	/// <param name="outUncompressedSize">解凍後の大きさ [byte] を書く先</param>
	/// <param name="outCompressedSize">pack 上の実際の大きさ [byte] を書く先</param>
	/// <param name="outCompression">圧縮形式（0=NONE, 1=GDEFLATE）を書く先</param>
	/// <returns>見つかったら true。FS モードでは常に false</returns>
	bool GetPackEntryInfoEx(const std::string& path,
	                       uint64_t& outPackOffset,
	                       uint64_t& outUncompressedSize,
	                       uint64_t& outCompressedSize,
	                       uint8_t&  outCompression) const;

	/// <summary>現在のロードモードの名前</summary>
	/// <returns>"FS" / "Pack" / "Uninitialized"</returns>
	const char* GetModeName() const;
	/// <summary>pack 経由モードか</summary>
	/// <returns>pack モードなら true</returns>
	bool IsPackMode() const;

	/// <summary>
	/// 拡張子でアセットを列挙する（SceneEditor 用。拡張子は大文字小文字を区別しない）。
	/// FS モードは root 配下を再帰的にスキャン、pack モードは目次から root で始まる物を抜き出す
	/// </summary>
	/// <param name="ext">"." を含む拡張子（".mesh" 等）</param>
	/// <param name="root">スキャン起点（既定は "Resources"）</param>
	/// <returns>見つかったパスの一覧</returns>
	std::vector<std::string> ListByExtension(
		const std::string& ext,
		const std::string& root = "Resources") const;

private:
	AssetLocator() = default;
	~AssetLocator() = default;
	AssetLocator(const AssetLocator&) = delete;
	AssetLocator& operator=(const AssetLocator&) = delete;

	enum class Mode { Uninitialized, Filesystem, Pack };
	Mode mode_ = Mode::Uninitialized;

	// pack モード用: パスとそのオフセット情報
	struct PackEntry {
		uint64_t name_hash;
		uint64_t payload_offset;
		uint64_t uncompressed_size;
		uint64_t compressed_size;  // pack 上の実サイズ (圧縮なしなら uncompressed と同じ)
		uint8_t  compression;      // 0=NONE, 1=GDEFLATE
		std::string path;
	};
	std::string packPath_;
	std::vector<PackEntry> packIndex_;  // name_hash 昇順ソート済み

	/// <summary>パスのハッシュで目次を二分探索し、ハッシュが衝突した時はパス文字列で照合する</summary>
	/// <param name="path">アセットのパス</param>
	/// <param name="out">見つかったエントリのコピーを書く先</param>
	/// <returns>見つかったら true</returns>
	bool FindPackEntry(const std::string& path, PackEntry& out) const;
};
