#include "MoviePlayer.h"

#include "TextureManager.h"
#include "ConvertString.h"
#include <propvarutil.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#pragma comment(lib, "propsys.lib")

namespace {
	using Clock = std::chrono::steady_clock;
	// フレームの表示時刻より遅れたとき、これ以上遅れていたら時計を合わせ直す（重い所で早送りにしない）
	constexpr auto kResyncThreshold = std::chrono::milliseconds(250);
	// 表示時刻まで待つときの 1 回の最大の眠り（Stop の効きを早くする）
	constexpr auto kMaxSleep = std::chrono::milliseconds(5);
}

MoviePlayer::~MoviePlayer() {
	Stop();
}

bool MoviePlayer::Start(const std::string& filePath, const std::string& textureBaseName) {
	Stop();
	if (!std::filesystem::exists(filePath)) {
		failed_ = true;
		return false;
	}
	failed_ = false;
	hasFrame_ = false;
	textureBaseName_ = textureBaseName;
	{
		std::lock_guard<std::mutex> lock(bufferMutex_);
		newFrame_ = false;
	}
	threadRunning_ = true;
	thread_ = std::thread(&MoviePlayer::ThreadFunc, this, ConvertString(filePath));
	return true;
}

void MoviePlayer::Stop() {
	threadRunning_ = false;
	if (thread_.joinable()) thread_.join();
	hasFrame_ = false;
}

bool MoviePlayer::OpenReader(const std::wstring& path) {
	Microsoft::WRL::ComPtr<IMFAttributes> attributes;
	if (FAILED(MFCreateAttributes(&attributes, 2))) return false;
	// 色の変換（NV12 → RGB32）を読み取り器に任せる。ハードウェアのデコーダーも使ってよい（HEVC の録画でも読めるように）
	attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING, TRUE);
	attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);

	if (FAILED(MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_))) return false;

	// 映像だけ読む（音のストリームを読まないと、デコードの手間もバッファの溜まりも無い）
	reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS), FALSE);
	reader_->SetStreamSelection(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), TRUE);

	Microsoft::WRL::ComPtr<IMFMediaType> type;
	if (FAILED(MFCreateMediaType(&type))) return false;
	type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
	type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);  // BGRA の並び
	if (FAILED(reader_->SetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), nullptr, type.Get()))) {
		return false;
	}
	ReadCurrentMediaType();
	return frameWidth_ > 0 && frameHeight_ > 0;
}

void MoviePlayer::ReadCurrentMediaType() {
	Microsoft::WRL::ComPtr<IMFMediaType> current;
	if (FAILED(reader_->GetCurrentMediaType(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), &current))) return;
	UINT32 width = 0, height = 0;
	MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &width, &height);
	frameWidth_ = width;
	frameHeight_ = height;
	// 1 行のバイト数。負なら下から上の並び。属性が無ければ詰めた幅
	UINT32 stride = 0;
	if (SUCCEEDED(current->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
		defaultStride_ = static_cast<LONG>(stride);
	} else {
		defaultStride_ = static_cast<LONG>(width * 4);
	}
}

void MoviePlayer::CopySample(IMFSample* sample) {
	const uint32_t rowBytes = frameWidth_ * 4;
	workBuffer_.resize(static_cast<size_t>(rowBytes) * frameHeight_);

	// 2D のバッファなら Lock2D で本当の行の幅（1080p のデコードで出る 1088 行の余りなど）を取る
	Microsoft::WRL::ComPtr<IMFMediaBuffer> buffer;
	if (FAILED(sample->GetBufferByIndex(0, &buffer))) return;
	Microsoft::WRL::ComPtr<IMF2DBuffer> buffer2D;
	BYTE* scan0 = nullptr;
	LONG pitch = 0;
	bool locked2D = false;
	BYTE* data = nullptr;
	if (SUCCEEDED(buffer.As(&buffer2D)) && SUCCEEDED(buffer2D->Lock2D(&scan0, &pitch))) {
		locked2D = true;
	} else {
		DWORD maxLength = 0, currentLength = 0;
		if (FAILED(buffer->Lock(&data, &maxLength, &currentLength))) return;
		pitch = defaultStride_;
		scan0 = (pitch < 0) ? data + static_cast<size_t>(-pitch) * (frameHeight_ - 1) : data;
		if (static_cast<size_t>(std::abs(pitch)) * frameHeight_ > currentLength) {
			buffer->Unlock();
			return;
		}
	}

	for (uint32_t y = 0; y < frameHeight_; ++y) {
		const BYTE* src = scan0 + static_cast<ptrdiff_t>(pitch) * static_cast<ptrdiff_t>(y);
		uint8_t* dst = workBuffer_.data() + static_cast<size_t>(rowBytes) * y;
		std::memcpy(dst, src, rowBytes);
		// RGB32 の 4 バイト目は 0 のことがあるので不透明にする
		for (uint32_t x = 3; x < rowBytes; x += 4) dst[x] = 255;
	}

	if (locked2D) {
		buffer2D->Unlock2D();
	} else {
		buffer->Unlock();
	}

	std::lock_guard<std::mutex> lock(bufferMutex_);
	threadBuffer_.swap(workBuffer_);
	threadWidth_ = frameWidth_;
	threadHeight_ = frameHeight_;
	newFrame_ = true;
}

void MoviePlayer::ThreadFunc(std::wstring path) {
	// Media Foundation は COM なので、スレッドごとに初期化する（MFStartup は SoundManager の初期化で済んでいる）
	const HRESULT coResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

	if (!OpenReader(path)) {
		failed_ = true;
		threadRunning_ = false;
	}

	// 動画の時刻 0 に当たる実時間
	Clock::time_point origin = Clock::now();
	while (threadRunning_) {
		DWORD streamIndex = 0;
		DWORD flags = 0;
		LONGLONG timestamp = 0;  // 100ns 単位
		Microsoft::WRL::ComPtr<IMFSample> sample;
		const HRESULT hr = reader_->ReadSample(static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM), 0,
			&streamIndex, &flags, &timestamp, &sample);
		if (FAILED(hr) || (flags & MF_SOURCE_READERF_ERROR)) {
			failed_ = true;
			break;
		}
		if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
			ReadCurrentMediaType();
		}
		if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
			// 先頭へ戻ってループ
			PROPVARIANT position;
			InitPropVariantFromInt64(0, &position);
			reader_->SetCurrentPosition(GUID_NULL, position);
			PropVariantClear(&position);
			origin = Clock::now();
			continue;
		}
		if (!sample) continue;

		// 表示時刻まで待つ（実時間）。大きく遅れていたら時計を合わせ直す
		const auto showAt = origin + std::chrono::duration_cast<Clock::duration>(std::chrono::nanoseconds(timestamp * 100));
		auto now = Clock::now();
		if (now - showAt > kResyncThreshold) {
			origin += (now - showAt);
		}
		while (threadRunning_ && (now = Clock::now()) < showAt) {
			std::this_thread::sleep_for((std::min)(std::chrono::duration_cast<Clock::duration>(kMaxSleep), showAt - now));
		}
		if (!threadRunning_) break;

		CopySample(sample.Get());
	}

	reader_.Reset();
	if (SUCCEEDED(coResult)) CoUninitialize();
}

void MoviePlayer::UpdateTexture() {
	uint32_t width = 0;
	uint32_t height = 0;
	{
		std::lock_guard<std::mutex> lock(bufferMutex_);
		if (!newFrame_) return;
		mainBuffer_.swap(threadBuffer_);
		width = threadWidth_;
		height = threadHeight_;
		newFrame_ = false;
	}
	if (width == 0 || height == 0 || mainBuffer_.empty()) return;

	auto* textureManager = TextureManager::GetInstance();
	// 動的テクスチャは消せないので、大きさごとに名前を分けて一度だけ作る
	if (width != textureWidth_ || height != textureHeight_ || textureName_.empty()) {
		textureName_ = textureBaseName_ + "_" + std::to_string(width) + "x" + std::to_string(height);
		if (!textureManager->HasTexture(textureName_)) {
			textureManager->CreateDynamicTexture(textureName_, width, height, DXGI_FORMAT_B8G8R8A8_UNORM);
		}
		textureWidth_ = width;
		textureHeight_ = height;
	}
	if (!textureManager->HasTexture(textureName_)) return;
	textureManager->UpdateDynamicTexture(textureName_, mainBuffer_.data(), mainBuffer_.size());
	hasFrame_ = true;
}
