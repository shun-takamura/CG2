#include "TextureManager.h"
TextureManager* TextureManager::instance = nullptr;

uint32_t TextureManager::kSRVIndexTop = 1;

void TextureManager::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore, SRVManager* srvManager)
{
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	srvManager_ = srvManager;

	// SRVの数と同数
	//textureDatas.reserve(SRVManager::kMaxSRVCount);
}

bool TextureManager::HasTexture(const std::string& filePath) const
{
    return textureDatas.contains(filePath);
}

void TextureManager::CreateDynamicTexture(const std::string& name, uint32_t width, uint32_t height,
    DXGI_FORMAT format)
{
    // 既に存在する場合は何もしない
    if (textureDatas.contains(name))
    {
        OutputDebugStringA("TextureManager: Dynamic texture already exists\n");
        return;
    }

    // テクスチャ枚数上限チェック
    if (!srvManager_->CanAllocate())
    {
        OutputDebugStringA("TextureManager: SRV limit reached\n");
        return;
    }

    TextureData& textureData = textureDatas[name];
    textureData.isDynamic = true;

    // メタデータを設定
    textureData.metadata.width = width;
    textureData.metadata.height = height;
    textureData.metadata.depth = 1;
    textureData.metadata.arraySize = 1;
    textureData.metadata.mipLevels = 1;
    textureData.metadata.format = format;
    textureData.metadata.dimension = DirectX::TEX_DIMENSION_TEXTURE2D;

    // テクスチャリソースを作成（DEFAULTヒープ）
    D3D12_RESOURCE_DESC resourceDesc{};
    resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resourceDesc.Width = width;
    resourceDesc.Height = height;
    resourceDesc.DepthOrArraySize = 1;
    resourceDesc.MipLevels = 1;
    resourceDesc.Format = format;
    resourceDesc.SampleDesc.Count = 1;
    resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

    D3D12_HEAP_PROPERTIES heapProps{};
    heapProps.Type = D3D12_HEAP_TYPE_DEFAULT;

    // 初期ステートをPIXEL_SHADER_RESOURCEにする
    HRESULT hr = dxCore_->GetDevice()->CreateCommittedResource(
        &heapProps,
        D3D12_HEAP_FLAG_NONE,
        &resourceDesc,
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,  // 変更
        nullptr,
        IID_PPV_ARGS(&textureData.resource)
    );

    if (FAILED(hr))
    {
        char buf[256];
        sprintf_s(buf, "TextureManager: CreateCommittedResource failed (0x%08X)\n", hr);
        OutputDebugStringA(buf);
        textureDatas.erase(name);
        return;
    }

    // アップロードバッファを作成
    UINT64 uploadBufferSize = 0;
    dxCore_->GetDevice()->GetCopyableFootprints(
        &resourceDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadBufferSize
    );

    D3D12_HEAP_PROPERTIES uploadHeapProps{};
    uploadHeapProps.Type = D3D12_HEAP_TYPE_UPLOAD;

    D3D12_RESOURCE_DESC uploadBufferDesc{};
    uploadBufferDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    uploadBufferDesc.Width = uploadBufferSize;
    uploadBufferDesc.Height = 1;
    uploadBufferDesc.DepthOrArraySize = 1;
    uploadBufferDesc.MipLevels = 1;
    uploadBufferDesc.Format = DXGI_FORMAT_UNKNOWN;
    uploadBufferDesc.SampleDesc.Count = 1;
    uploadBufferDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

    hr = dxCore_->GetDevice()->CreateCommittedResource(
        &uploadHeapProps,
        D3D12_HEAP_FLAG_NONE,
        &uploadBufferDesc,
        D3D12_RESOURCE_STATE_GENERIC_READ,
        nullptr,
        IID_PPV_ARGS(&textureData.uploadBuffer)
    );

    if (FAILED(hr))
    {
        char buf[256];
        sprintf_s(buf, "TextureManager: CreateCommittedResource for upload failed (0x%08X)\n", hr);
        OutputDebugStringA(buf);
        textureDatas.erase(name);
        return;
    }

    // SRVインデックスを確保
    textureData.srvIndex = srvManager_->Allocate();

    // SRVを生成
    srvManager_->CreateSRVForTexture2D(
        textureData.srvIndex,
        textureData.resource.Get(),
        format,
        1
    );

    OutputDebugStringA("TextureManager: Dynamic texture created successfully\n");
}

// TextureManager.cpp

void TextureManager::UpdateDynamicTexture(const std::string& name, const uint8_t* data, size_t dataSize)
{
    if (data == nullptr || dataSize == 0)
    {
        return;
    }

    auto it = textureDatas.find(name);
    if (it == textureDatas.end() || !it->second.isDynamic)
    {
        return;
    }

    TextureData& textureData = it->second;

    // リソースのnullチェック
    if (!textureData.resource || !textureData.uploadBuffer)
    {
        OutputDebugStringA("TextureManager: Resource is null\n");
        return;
    }

    // アップロードバッファにデータをコピー
    uint8_t* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    HRESULT hr = textureData.uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
    if (FAILED(hr))
    {
        OutputDebugStringA("TextureManager: Map failed\n");
        return;
    }

    // フットプリントを取得
    D3D12_RESOURCE_DESC resourceDesc = textureData.resource->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
    UINT numRows;
    UINT64 rowSizeInBytes;
    UINT64 totalBytes;
    dxCore_->GetDevice()->GetCopyableFootprints(
        &resourceDesc, 0, 1, 0, &footprint, &numRows, &rowSizeInBytes, &totalBytes
    );

    // 行ごとにコピー（ピッチが異なる場合があるため）
    uint32_t srcRowPitch = static_cast<uint32_t>(textureData.metadata.width * 4);
    for (UINT row = 0; row < numRows; row++)
    {
        size_t srcOffset = row * srcRowPitch;
        if (srcOffset + srcRowPitch > dataSize)
        {
            break;  // データが足りない場合は中断
        }
        memcpy(
            mappedData + footprint.Offset + row * footprint.Footprint.RowPitch,
            data + srcOffset,
            srcRowPitch
        );
    }

    textureData.uploadBuffer->Unmap(0, nullptr);

    // コマンドリストでコピー
    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();

    // リソースバリア: PIXEL_SHADER_RESOURCE -> COPY_DEST
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = textureData.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    // コピー
    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = textureData.resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = textureData.uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // リソースバリア: COPY_DEST -> PIXEL_SHADER_RESOURCE
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

void TextureManager::LoadTexture(const std::string& filePath)
{
	// 読み込み済みテクスチャを検索
	if (textureDatas.contains(filePath)) {
		// 既に読み込み済みなので何もしない
		return;
	}

	// テクスチャ枚数上限チェック
	assert(srvManager_->CanAllocate());

	// テクスチャファイルを読み込んでプログラムで扱えるようにする
	DirectX::ScratchImage image{};
	std::wstring filePathW = ConvertString(filePath);
	HRESULT hr = DirectX::LoadFromWICFile(
		filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	assert(SUCCEEDED(hr));

	// ミップマップの作成
	DirectX::ScratchImage mipImages{};
	hr = DirectX::GenerateMipMaps(
		image.GetImages(), image.GetImageCount(), image.GetMetadata(),
		DirectX::TEX_FILTER_SRGB, 0, mipImages);
	assert(SUCCEEDED(hr));

	// テクスチャデータの作成
	TextureData& textureData = textureDatas[filePath];
	textureData.metadata = mipImages.GetMetadata();

	// テクスチャリソース生成
	textureData.resource = spriteManager_->CreateTextureResource(
		dxCore_->GetDevice(),
		textureData.metadata
	);

	// テクスチャデータ転送
	spriteManager_->UploadTextureData(
		textureData.resource.Get(),
		mipImages,
		dxCore_->GetDevice(),
		dxCore_->GetCommandList()
	);

	// SRVインデックスを確保
	textureData.srvIndex = srvManager_->Allocate();

	// SRVを生成
	srvManager_->CreateSRVForTexture2D(
		textureData.srvIndex,
		textureData.resource.Get(),
		textureData.metadata.format,
		static_cast<UINT>(textureData.metadata.mipLevels)
	);
	
}

TextureManager* TextureManager::GetInstance()
{
	if (instance == nullptr) {
		instance = new TextureManager;
	}

	return instance;
}

const DirectX::TexMetadata& TextureManager::GetMetaData(const std::string& filePath)
{
	// filePathがキーなので、unordered_mapから直接取得
	auto it = textureDatas.find(filePath);

	if (it != textureDatas.end()) {
		return it->second.metadata;
	}

	// 見つからない場合は停止
	assert(0);
	static DirectX::TexMetadata dummy{};
	return dummy;
}

uint32_t TextureManager::GetSrvIndex(const std::string& filePath)
{
	// filePathがキーなので、unordered_mapから直接取得
	auto it = textureDatas.find(filePath);

	if (it != textureDatas.end()) {
		return it->second.srvIndex;
	}

	// 見つからない場合は停止
	assert(0);
	return 0;
}

D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(const std::string& filePath)
{
	// デバッグ出力
	OutputDebugStringA(("GetSrvHandleGPU called with: " + filePath + "\n").c_str());

	auto it = textureDatas.find(filePath);

	if (it != textureDatas.end()) {
		return srvManager_->GetGPUDescriptorHandle(it->second.srvIndex);
	}

	// 見つからない場合、登録されているキーを出力
	OutputDebugStringA("Registered textures:\n");
	for (const auto& pair : textureDatas) {
		OutputDebugStringA(("  - " + pair.first + "\n").c_str());
	}

	assert(0 && "Texture not found!");
	return D3D12_GPU_DESCRIPTOR_HANDLE{};
}

void TextureManager::Finalize()
{
	delete instance;
	instance = nullptr;
}
