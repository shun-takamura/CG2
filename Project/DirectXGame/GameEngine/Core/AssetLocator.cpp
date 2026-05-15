#include "AssetLocator.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstring>
#include <filesystem>

namespace {
// FNV-1a 64bit (pack_assets.py と完全に同じアルゴリズム)
uint64_t Fnv1a64(const std::string& s) {
	uint64_t h = 0xcbf29ce484222325ULL;
	for (unsigned char c : s) {
		h ^= c;
		h *= 0x100000001b3ULL;
	}
	return h;
}

// pack_assets.py と一致させるフォーマット定数
constexpr uint32_t kPackMagic = 0x4B434150;       // "PACK"
constexpr uint32_t kPackVersion = 1;
constexpr uint32_t kPackHeaderSize = 16;
constexpr uint32_t kPackIndexEntrySize = 40;
}

// =====================================================================
// AssetHandle
// =====================================================================
bool AssetHandle::ReadAt(uint64_t offset, void* dst, size_t size)
{
	if (!valid_ || !stream_) return false;
	if (offset + size > size_) return false;

	stream_->clear();  // 前回の eof 状態をクリア
	// pack モードでは baseOffset_ + offset、FS モードでは baseOffset_ = 0
	stream_->seekg(static_cast<std::streamoff>(baseOffset_ + offset));
	stream_->read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(size));
	return stream_->good() || stream_->eof();
}

bool AssetHandle::Read(void* dst, size_t size)
{
	if (!ReadAt(position_, dst, size)) return false;
	position_ += size;
	return true;
}

// =====================================================================
// AssetLocator
// =====================================================================
AssetLocator* AssetLocator::GetInstance()
{
	static AssetLocator instance;
	return &instance;
}

const char* AssetLocator::GetModeName() const
{
	switch (mode_) {
	case Mode::Filesystem: return "FS";
	case Mode::Pack:       return "Pack";
	default:               return "Uninitialized";
	}
}

bool AssetLocator::IsPackMode() const
{
	return mode_ == Mode::Pack;
}

void AssetLocator::InitializeFromFilesystem()
{
	mode_ = Mode::Filesystem;
	packPath_.clear();
	packIndex_.clear();
}

bool AssetLocator::InitializeFromPack(const std::string& packPath)
{
	std::ifstream f(packPath, std::ios::binary);
	if (!f) return false;

	// ---- ヘッダー ----
	uint32_t magic = 0, version = 0, assetCount = 0, indexOffset = 0;
	f.read(reinterpret_cast<char*>(&magic), 4);
	f.read(reinterpret_cast<char*>(&version), 4);
	f.read(reinterpret_cast<char*>(&assetCount), 4);
	f.read(reinterpret_cast<char*>(&indexOffset), 4);
	if (magic != kPackMagic || version != kPackVersion) return false;

	// ---- インデックスを 40 バイト/エントリで直読み ----
	// C++ の struct はパディングで 48B になるためフィールド直読みする
	struct ParsedIndex {
		uint64_t name_hash;
		uint32_t path_offset;
		uint16_t path_length;
		uint64_t uncompressed_size;
		uint64_t payload_offset;
	};
	std::vector<ParsedIndex> parsed(assetCount);
	f.seekg(indexOffset);
	for (uint32_t i = 0; i < assetCount; ++i) {
		uint8_t skip1, skip2;
		uint64_t skipU64;
		ParsedIndex& p = parsed[i];
		f.read(reinterpret_cast<char*>(&p.name_hash), 8);
		f.read(reinterpret_cast<char*>(&p.path_offset), 4);
		f.read(reinterpret_cast<char*>(&p.path_length), 2);
		f.read(reinterpret_cast<char*>(&skip1), 1);   // compression_type（pack モードでは未使用）
		f.read(reinterpret_cast<char*>(&skip2), 1);   // asset_type
		f.read(reinterpret_cast<char*>(&skipU64), 8); // compressed_size
		f.read(reinterpret_cast<char*>(&p.uncompressed_size), 8);
		f.read(reinterpret_cast<char*>(&p.payload_offset), 8);
	}

	// ---- 文字列テーブル ----
	uint32_t stringTableOffset = indexOffset + assetCount * kPackIndexEntrySize;
	uint32_t stringTableSize = 0;
	for (const auto& p : parsed) {
		uint32_t end = p.path_offset + p.path_length;
		if (end > stringTableSize) stringTableSize = end;
	}
	std::vector<char> stringTable(stringTableSize);
	f.seekg(stringTableOffset);
	f.read(stringTable.data(), stringTableSize);

	// ---- packIndex_ を構築 ----
	packIndex_.clear();
	packIndex_.reserve(assetCount);
	for (const auto& p : parsed) {
		PackEntry e;
		e.name_hash = p.name_hash;
		e.payload_offset = p.payload_offset;
		e.uncompressed_size = p.uncompressed_size;
		e.path.assign(stringTable.data() + p.path_offset, p.path_length);
		packIndex_.push_back(std::move(e));
	}
	// 念のためソートを保証（packer 側で済んでいるが）
	std::sort(packIndex_.begin(), packIndex_.end(),
	          [](const PackEntry& a, const PackEntry& b) { return a.name_hash < b.name_hash; });

	mode_ = Mode::Pack;
	packPath_ = packPath;
	return true;
}

bool AssetLocator::FindPackEntry(const std::string& path, PackEntry& out) const
{
	uint64_t h = Fnv1a64(path);
	auto it = std::lower_bound(packIndex_.begin(), packIndex_.end(), h,
	                           [](const PackEntry& e, uint64_t k) { return e.name_hash < k; });
	// ハッシュ衝突対応: 同一ハッシュの中からパス文字列が一致するものを探す
	while (it != packIndex_.end() && it->name_hash == h) {
		if (it->path == path) {
			out = *it;
			return true;
		}
		++it;
	}
	return false;
}

AssetHandle AssetLocator::Open(const std::string& path)
{
	AssetHandle h;
	if (mode_ == Mode::Filesystem) {
		auto stream = std::make_unique<std::ifstream>(path, std::ios::binary | std::ios::ate);
		if (!stream || !*stream) return h;
		auto size = stream->tellg();
		if (size <= 0) return h;
		stream->seekg(0);
		h.valid_ = true;
		h.size_ = static_cast<uint64_t>(size);
		h.baseOffset_ = 0;
		h.stream_ = std::move(stream);
		return h;
	}
	if (mode_ == Mode::Pack) {
		PackEntry entry;
		if (!FindPackEntry(path, entry)) return h;
		auto stream = std::make_unique<std::ifstream>(packPath_, std::ios::binary);
		if (!stream || !*stream) return h;
		h.valid_ = true;
		h.size_ = entry.uncompressed_size;
		h.baseOffset_ = entry.payload_offset;
		h.stream_ = std::move(stream);
		return h;
	}
	return h;
}

std::vector<uint8_t> AssetLocator::LoadAll(const std::string& path)
{
	std::vector<uint8_t> buf;
	if (mode_ == Mode::Filesystem) {
		std::ifstream f(path, std::ios::binary | std::ios::ate);
		if (!f) return buf;
		auto size = f.tellg();
		if (size <= 0) return buf;
		buf.resize(static_cast<size_t>(size));
		f.seekg(0);
		f.read(reinterpret_cast<char*>(buf.data()), size);
		return buf;
	}
	if (mode_ == Mode::Pack) {
		PackEntry entry;
		if (!FindPackEntry(path, entry)) return buf;
		std::ifstream f(packPath_, std::ios::binary);
		if (!f) return buf;
		buf.resize(static_cast<size_t>(entry.uncompressed_size));
		f.seekg(static_cast<std::streamoff>(entry.payload_offset));
		f.read(reinterpret_cast<char*>(buf.data()),
		       static_cast<std::streamsize>(entry.uncompressed_size));
		return buf;
	}
	return buf;
}

bool AssetLocator::Exists(const std::string& path) const
{
	if (mode_ == Mode::Filesystem) {
		return std::filesystem::exists(path);
	}
	if (mode_ == Mode::Pack) {
		PackEntry tmp;
		return FindPackEntry(path, tmp);
	}
	return false;
}

std::vector<std::string> AssetLocator::ListByExtension(
	const std::string& ext, const std::string& root) const
{
	std::vector<std::string> results;

	// 拡張子を小文字に正規化
	std::string extLower = ext;
	std::transform(extLower.begin(), extLower.end(), extLower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

	if (mode_ == Mode::Filesystem) {
		const std::filesystem::path rootPath = root;
		if (!std::filesystem::exists(rootPath)) return results;

		for (auto it = std::filesystem::recursive_directory_iterator(rootPath);
		     it != std::filesystem::recursive_directory_iterator(); ++it)
		{
			if (!it->is_regular_file()) continue;
			const auto& p = it->path();
			std::string e = p.extension().string();
			std::transform(e.begin(), e.end(), e.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (e == extLower) {
				results.push_back(p.generic_string());
			}
		}
		return results;
	}

	if (mode_ == Mode::Pack) {
		// root を "root/" 形式に正規化（末尾スラッシュなしなら追加）
		std::string prefix = root;
		if (!prefix.empty() && prefix.back() != '/') prefix.push_back('/');

		for (const auto& e : packIndex_) {
			// 末尾が ext と一致 + 先頭が prefix と一致
			if (e.path.size() < extLower.size()) continue;
			std::string entryExt = e.path.substr(e.path.size() - extLower.size());
			std::transform(entryExt.begin(), entryExt.end(), entryExt.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			if (entryExt != extLower) continue;

			if (!prefix.empty() && e.path.compare(0, prefix.size(), prefix) != 0) continue;

			results.push_back(e.path);
		}
		return results;
	}
	return results;
}
