#include "AssetLocator.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <filesystem>

// =====================================================================
// AssetHandle
// =====================================================================
bool AssetHandle::ReadAt(uint64_t offset, void* dst, size_t size)
{
	if (!valid_ || !stream_) return false;
	if (offset + size > size_) return false;

	stream_->clear();  // 前回の eof 状態をクリア
	stream_->seekg(static_cast<std::streamoff>(offset));
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

void AssetLocator::InitializeFromFilesystem()
{
	mode_ = Mode::Filesystem;
}

AssetHandle AssetLocator::Open(const std::string& path)
{
	AssetHandle h;
	if (mode_ != Mode::Filesystem) return h;

	auto stream = std::make_unique<std::ifstream>(path, std::ios::binary | std::ios::ate);
	if (!stream || !*stream) return h;

	auto size = stream->tellg();
	if (size <= 0) return h;

	stream->seekg(0);
	h.valid_ = true;
	h.size_ = static_cast<uint64_t>(size);
	h.stream_ = std::move(stream);
	return h;
}

std::vector<uint8_t> AssetLocator::LoadAll(const std::string& path)
{
	std::vector<uint8_t> buf;
	if (mode_ != Mode::Filesystem) return buf;

	std::ifstream f(path, std::ios::binary | std::ios::ate);
	if (!f) return buf;

	auto size = f.tellg();
	if (size <= 0) return buf;

	buf.resize(static_cast<size_t>(size));
	f.seekg(0);
	f.read(reinterpret_cast<char*>(buf.data()), size);
	return buf;
}

bool AssetLocator::Exists(const std::string& path) const
{
	if (mode_ != Mode::Filesystem) return false;
	return std::filesystem::exists(path);
}

std::vector<std::string> AssetLocator::ListByExtension(
	const std::string& ext, const std::string& root) const
{
	std::vector<std::string> results;
	if (mode_ != Mode::Filesystem) return results;

	std::string extLower = ext;
	std::transform(extLower.begin(), extLower.end(), extLower.begin(),
	               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

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
