#include "DStorageManager.h"
#include "Log.h"
#include "ConvertString.h"
#include <cassert>
#include <filesystem>

DStorageManager* DStorageManager::GetInstance()
{
	static DStorageManager instance;
	return &instance;
}

DStorageManager::~DStorageManager()
{
	Finalize();
}

bool DStorageManager::Initialize(ID3D12Device* device)
{
	if (initialized_) return true;
	assert(device);

	// Factory 作成
	HRESULT hr = DStorageGetFactory(IID_PPV_ARGS(&factory_));
	if (FAILED(hr)) {
		Log("[DStorage] DStorageGetFactory failed\n");
		return false;
	}

	// Queue 作成（SourceType=FILE、優先度 NORMAL、最大キャパシティ）
	DSTORAGE_QUEUE_DESC queueDesc{};
	queueDesc.Capacity = DSTORAGE_MAX_QUEUE_CAPACITY;
	queueDesc.Priority = DSTORAGE_PRIORITY_NORMAL;
	queueDesc.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	queueDesc.Device = device;

	hr = factory_->CreateQueue(&queueDesc, IID_PPV_ARGS(&queue_));
	if (FAILED(hr)) {
		Log("[DStorage] CreateQueue failed\n");
		return false;
	}

	// Fence 作成
	hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence_));
	if (FAILED(hr)) {
		Log("[DStorage] CreateFence failed\n");
		return false;
	}
	fenceValue_ = 0;
	fenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
	if (!fenceEvent_) {
		Log("[DStorage] CreateEvent failed\n");
		return false;
	}

	initialized_ = true;
	Log("[DStorage] Initialized\n");
	return true;
}

void DStorageManager::Finalize()
{
	if (!initialized_) return;

	if (fenceEvent_) {
		CloseHandle(fenceEvent_);
		fenceEvent_ = nullptr;
	}
	packFile_.Reset();
	fence_.Reset();
	queue_.Reset();
	factory_.Reset();
	initialized_ = false;
}

bool DStorageManager::OpenPackFile(const std::string& path)
{
	if (!initialized_) return false;

	// 事前に存在チェック。DirectStorage::OpenFile は内部で wil 例外を投げるため、
	// 存在しないパスに対して呼ぶとデバッガ出力が荒れる。
	if (!std::filesystem::exists(path)) {
		return false;
	}

	std::wstring wpath = ConvertString(path);
	HRESULT hr = factory_->OpenFile(wpath.c_str(), IID_PPV_ARGS(&packFile_));
	if (FAILED(hr)) {
		Log("[DStorage] OpenFile failed: " + path + "\n");
		packFile_.Reset();
		return false;
	}
	Log("[DStorage] Pack opened: " + path + "\n");
	return true;
}

void DStorageManager::EnqueueMemoryRead(IDStorageFile* file, uint64_t offset,
                                        uint32_t size, void* dst)
{
	if (!initialized_ || !file || !dst || size == 0) return;

	DSTORAGE_REQUEST req{};
	req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MEMORY;
	req.Source.File.Source = file;
	req.Source.File.Offset = offset;
	req.Source.File.Size = size;
	req.Destination.Memory.Buffer = dst;
	req.Destination.Memory.Size = size;
	req.UncompressedSize = size;

	queue_->EnqueueRequest(&req);
}

void DStorageManager::EnqueueBufferRead(IDStorageFile* file, uint64_t offset, uint32_t size,
                                        ID3D12Resource* dst, uint64_t dstOffset)
{
	if (!initialized_ || !file || !dst || size == 0) return;

	DSTORAGE_REQUEST req{};
	req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_BUFFER;
	req.Source.File.Source = file;
	req.Source.File.Offset = offset;
	req.Source.File.Size = size;
	req.Destination.Buffer.Resource = dst;
	req.Destination.Buffer.Offset = dstOffset;
	req.Destination.Buffer.Size = size;
	req.UncompressedSize = size;

	queue_->EnqueueRequest(&req);
}

void DStorageManager::EnqueueTextureRead(IDStorageFile* file, uint64_t offset, uint32_t size,
                                         ID3D12Resource* dst, uint32_t subresource,
                                         uint32_t width, uint32_t height, uint32_t depth)
{
	if (!initialized_ || !file || !dst || size == 0) return;

	DSTORAGE_REQUEST req{};
	req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_TEXTURE_REGION;
	req.Source.File.Source = file;
	req.Source.File.Offset = offset;
	req.Source.File.Size = size;
	req.Destination.Texture.Resource = dst;
	req.Destination.Texture.SubresourceIndex = subresource;
	// Region は zero-init では zero-sized になり書き込まれない。
	// subresource 全体を覆う box を明示する。
	req.Destination.Texture.Region.left   = 0;
	req.Destination.Texture.Region.top    = 0;
	req.Destination.Texture.Region.front  = 0;
	req.Destination.Texture.Region.right  = width;
	req.Destination.Texture.Region.bottom = height;
	req.Destination.Texture.Region.back   = depth;
	req.UncompressedSize = size;

	queue_->EnqueueRequest(&req);
}

void DStorageManager::EnqueueMultipleSubresources(IDStorageFile* file, uint64_t offset,
                                                  uint32_t size,
                                                  ID3D12Resource* dst,
                                                  uint32_t firstSubresource)
{
	if (!initialized_ || !file || !dst || size == 0) return;

	DSTORAGE_REQUEST req{};
	req.Options.SourceType = DSTORAGE_REQUEST_SOURCE_FILE;
	req.Options.DestinationType = DSTORAGE_REQUEST_DESTINATION_MULTIPLE_SUBRESOURCES;
	req.Source.File.Source = file;
	req.Source.File.Offset = offset;
	req.Source.File.Size = size;
	req.Destination.MultipleSubresources.Resource = dst;
	req.Destination.MultipleSubresources.FirstSubresource = firstSubresource;
	req.UncompressedSize = size;

	queue_->EnqueueRequest(&req);
}

void DStorageManager::Submit()
{
	if (!initialized_) return;
	queue_->Submit();
}

void DStorageManager::SubmitAndWait()
{
	if (!initialized_) return;

	++fenceValue_;
	queue_->EnqueueSignal(fence_.Get(), fenceValue_);
	queue_->Submit();

	if (fence_->GetCompletedValue() < fenceValue_) {
		fence_->SetEventOnCompletion(fenceValue_, fenceEvent_);
		WaitForSingleObject(fenceEvent_, INFINITE);
	}
}
