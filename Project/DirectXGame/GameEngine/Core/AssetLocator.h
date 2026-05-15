#pragma once
#include <cstdint>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

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

	bool IsValid() const { return valid_; }
	uint64_t GetSize() const { return size_; }
	uint64_t GetPosition() const { return position_; }

	// 指定オフセットから size バイトを dst に読み出す。成功で true。位置は更新しない。
	bool ReadAt(uint64_t offset, void* dst, size_t size);

	// 現在位置から size バイトを読み、位置を進める。ifstream::read 相当。
	bool Read(void* dst, size_t size);

	// 読み出し位置を移動する。ifstream::seekg 相当。
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
	static AssetLocator* GetInstance();

	// 個別ファイル直読みモードで初期化
	void InitializeFromFilesystem();

	// .pack 経由モードで初期化。失敗で false（pack ファイルなし、フォーマット不正）。
	bool InitializeFromPack(const std::string& packPath);

	// 主要 API: 部分読み出し用ハンドルを得る
	AssetHandle Open(const std::string& path);

	// 補助 API: ファイル全体を一括読み込み（小サイズ向け）
	std::vector<uint8_t> LoadAll(const std::string& path);

	// 存在チェック
	bool Exists(const std::string& path) const;

	// 現在のロードモード文字列（"FS" / "Pack" / "Uninitialized"）
	const char* GetModeName() const;
	bool IsPackMode() const;

	// 拡張子による列挙（SceneEditor 用、ext は "." 含む形式: ".mesh" 等）
	// root はスキャン起点（既定は "Resources"）。
	// FS モード: 指定 root 配下を再帰的にスキャン
	// pack モード: pack の目次から prefix 一致するものを抽出（B.2 で実装）
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
		std::string path;
	};
	std::string packPath_;
	std::vector<PackEntry> packIndex_;  // name_hash 昇順ソート済み

	// pack エントリのバイナリサーチ。見つかれば true、out にコピー。
	bool FindPackEntry(const std::string& path, PackEntry& out) const;
};
