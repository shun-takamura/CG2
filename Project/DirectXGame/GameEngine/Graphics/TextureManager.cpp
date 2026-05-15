#include "TextureManager.h"
#include "DDSHeader.h"
#include "AssetLocator.h"
//TextureManager* TextureManager::instance = nullptr;

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

void TextureManager::CreateSolidColorTexture(const std::string& name, uint8_t r, uint8_t g, uint8_t b, uint8_t a)
{
    OutputDebugStringA(("CreateSolidColorTexture called: " + name + "\n").c_str());

    // 既に存在する場合は何もしない
    if (textureDatas.contains(name)) {
        OutputDebugStringA("  -> Already exists, skipping\n");
        return;
    }

    // テクスチャ枚数上限チェック
    if (!srvManager_->CanAllocate()) {
        OutputDebugStringA("TextureManager: SRV limit reached\n");
        return;
    }

    // 1x1ピクセルのテクスチャデータを作成
    uint8_t pixelData[4] = { r, g, b, a };

    // ScratchImageを使って1x1テクスチャを作成
    DirectX::ScratchImage image;
    HRESULT hr = image.Initialize2D(
        DXGI_FORMAT_R8G8B8A8_UNORM_SRGB,
        1, 1,  // width, height
        1, 1   // arraySize, mipLevels
    );

    if (FAILED(hr)) {
        OutputDebugStringA("TextureManager: Failed to initialize ScratchImage\n");
        return;
    }

    // ピクセルデータをコピー
    const DirectX::Image* img = image.GetImage(0, 0, 0);
    if (img && img->pixels) {
        memcpy(img->pixels, pixelData, 4);
    }

    // テクスチャデータの作成
    TextureData& textureData = textureDatas[name];
    textureData.metadata = image.GetMetadata();
    textureData.isDynamic = false;

    // テクスチャリソース生成
    textureData.resource = spriteManager_->CreateTextureResource(
        dxCore_->GetDevice(),
        textureData.metadata
    );

    // テクスチャデータ転送
    spriteManager_->UploadTextureData(
        textureData.resource.Get(),
        image,
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

    // 同期で完了するパスなので GPUReady 状態に遷移させる
    textureData.loadState = TextureData::LoadState::GPUReady;

    OutputDebugStringA(("TextureManager: Created solid color texture: " + name + "\n").c_str());
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

    char buf[256];
    sprintf_s(buf, "CreateDynamicTexture: %s, %u x %u\n", name.c_str(), width, height);
    OutputDebugStringA(buf);

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
        D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
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

    // 同期で完了するパスなので GPUReady 状態に遷移させる
    textureData.loadState = TextureData::LoadState::GPUReady;

    OutputDebugStringA("TextureManager: Dynamic texture created successfully\n");
}

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

    if (!textureData.resource || !textureData.uploadBuffer)
    {
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

    // アップロードバッファにデータをコピー
    uint8_t* mappedData = nullptr;
    D3D12_RANGE readRange{ 0, 0 };
    HRESULT hr = textureData.uploadBuffer->Map(0, &readRange, reinterpret_cast<void**>(&mappedData));
    if (FAILED(hr))
    {
        return;
    }

    // 行ごとにコピー
    uint32_t srcRowPitch = static_cast<uint32_t>(textureData.metadata.width * 4);
    uint32_t dstRowPitch = footprint.Footprint.RowPitch;

    for (UINT row = 0; row < numRows; row++)
    {
        size_t srcOffset = row * srcRowPitch;
        size_t dstOffset = footprint.Offset + row * dstRowPitch;

        if (srcOffset + srcRowPitch <= dataSize)
        {
            memcpy(mappedData + dstOffset, data + srcOffset, srcRowPitch);
        }
    }

    textureData.uploadBuffer->Unmap(0, nullptr);

    // コマンドリストでコピー
    ID3D12GraphicsCommandList* commandList = dxCore_->GetCommandList();

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = textureData.resource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = textureData.resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = textureData.uploadBuffer.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    commandList->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    commandList->ResourceBarrier(1, &barrier);
}

// =====================================================================
// 非同期ロード (CPU/GPU 二段分離)
// =====================================================================

void TextureManager::LoadTextureCPU(const std::string& filePath, bool linear)
{
    // 既にエントリがある（CPU/GPU いずれかのフェーズが進行中 or 完了）なら何もしない
    {
        std::lock_guard<std::mutex> lock(textureDatasMutex_);
        if (textureDatas.contains(filePath)) {
            return;
        }
        // プレースホルダエントリを置いて多重ロードを防ぐ
        TextureData& slot = textureDatas[filePath];
        slot.loadState = TextureData::LoadState::Unloaded;
        slot.isLinear = linear;
    }

    // --- ロックを外して重い WIC/DDS デコードを実行（スレッド安全な部分）---
    // AssetLocator 経由でバイト列を取得 → 専用の Memory 系 API でデコード。
    // これで pack モードでも同じコードが動く。
    std::vector<uint8_t> bytes = AssetLocator::GetInstance()->LoadAll(filePath);
    if (bytes.empty()) {
        std::lock_guard<std::mutex> lock(textureDatasMutex_);
        textureDatas.erase(filePath);
        return;
    }

    DirectX::ScratchImage image{};
    HRESULT hr;

    // 拡張子で DDS / WIC を分岐（パスベース判定でファイル本体は触らない）
    bool isDDS = filePath.size() >= 4 &&
        std::equal(filePath.end() - 4, filePath.end(), ".dds",
                   [](char a, char b) { return std::tolower(static_cast<unsigned char>(a))
                                              == std::tolower(static_cast<unsigned char>(b)); });
    if (isDDS) {
        hr = DirectX::LoadFromDDSMemory(
            bytes.data(), bytes.size(), DirectX::DDS_FLAGS_NONE, nullptr, image);
    } else {
        auto wicFlags = linear ? DirectX::WIC_FLAGS_IGNORE_SRGB : DirectX::WIC_FLAGS_FORCE_SRGB;
        hr = DirectX::LoadFromWICMemory(
            bytes.data(), bytes.size(), wicFlags, nullptr, image);
    }
    if (FAILED(hr)) {
        std::lock_guard<std::mutex> lock(textureDatasMutex_);
        textureDatas.erase(filePath);
        return;
    }

    DirectX::ScratchImage mipImages{};
    if (DirectX::IsCompressed(image.GetMetadata().format)) {
        // 圧縮フォーマット（BC7/BC6H など）は既存ミップを使う
        mipImages = std::move(image);
    } else {
        auto filter = linear ? DirectX::TEX_FILTER_DEFAULT : DirectX::TEX_FILTER_SRGB;
        hr = DirectX::GenerateMipMaps(
            image.GetImages(), image.GetImageCount(), image.GetMetadata(),
            filter, 0, mipImages);
        if (FAILED(hr)) {
            std::lock_guard<std::mutex> lock(textureDatasMutex_);
            textureDatas.erase(filePath);
            return;
        }
    }

    // --- 結果を書き戻す ---
    {
        std::lock_guard<std::mutex> lock(textureDatasMutex_);
        auto it = textureDatas.find(filePath);
        if (it == textureDatas.end()) return;
        it->second.metadata = mipImages.GetMetadata();
        it->second.cpuImage = std::move(mipImages);
        it->second.loadState = TextureData::LoadState::CPUReady;
    }
}

void TextureManager::LoadTextureGPU(const std::string& filePath)
{
    // メインスレッド専用。CPU フェーズが先に完了している前提。
    TextureData* data = nullptr;
    {
        std::lock_guard<std::mutex> lock(textureDatasMutex_);
        auto it = textureDatas.find(filePath);
        if (it == textureDatas.end()) return;
        if (it->second.loadState == TextureData::LoadState::GPUReady) return;
        assert(it->second.loadState == TextureData::LoadState::CPUReady
               && "LoadTextureGPU called before LoadTextureCPU");
        data = &it->second;
    }
    // ※ unordered_map は要素を挿入してもポインタ無効化しないため
    //   ロック外で data を経由して触ってよい（同じエントリへは他から触らない契約）

    assert(srvManager_->CanAllocate());

    data->resource = spriteManager_->CreateTextureResource(
        dxCore_->GetDevice(), data->metadata);

    spriteManager_->UploadTextureData(
        data->resource.Get(), data->cpuImage,
        dxCore_->GetDevice(), dxCore_->GetCommandList());

    data->srvIndex = srvManager_->Allocate();

    if (data->metadata.IsCubemap()) {
        srvManager_->CreateSRVForCubemap(
            data->srvIndex, data->resource.Get(),
            data->metadata.format,
            static_cast<UINT>(data->metadata.mipLevels));
    } else {
        srvManager_->CreateSRVForTexture2D(
            data->srvIndex, data->resource.Get(),
            data->metadata.format,
            static_cast<UINT>(data->metadata.mipLevels));
    }

    // CPU 側 ScratchImage はもう不要、解放
    data->cpuImage = DirectX::ScratchImage{};
    data->loadState = TextureData::LoadState::GPUReady;
}

bool TextureManager::IsCPUReady(const std::string& filePath) const
{
    std::lock_guard<std::mutex> lock(textureDatasMutex_);
    auto it = textureDatas.find(filePath);
    return it != textureDatas.end()
        && it->second.loadState >= TextureData::LoadState::CPUReady;
}

bool TextureManager::IsGPUReady(const std::string& filePath) const
{
    std::lock_guard<std::mutex> lock(textureDatasMutex_);
    auto it = textureDatas.find(filePath);
    return it != textureDatas.end()
        && it->second.loadState == TextureData::LoadState::GPUReady;
}

// =====================================================================
// 同期ラッパー（既存 API 互換）
// =====================================================================

void TextureManager::LoadTexture(const std::string& filePath)
{
    OutputDebugStringA(("LoadTexture called: " + filePath + "\n").c_str());
    if (IsGPUReady(filePath)) {
        OutputDebugStringA("  -> Already exists, skipping\n");
        return;
    }
    OutputDebugStringA("  -> Loading from file...\n");
    LoadTextureCPU(filePath, false);
    LoadTextureGPU(filePath);
}

void TextureManager::LoadTextureLinear(const std::string& filePath)
{
    if (IsGPUReady(filePath)) return;
    LoadTextureCPU(filePath, true);
    LoadTextureGPU(filePath);
}

TextureManager* TextureManager::GetInstance()
{
    //if (instance == nullptr) {
        //instance = new TextureManager;
    //}

    static TextureManager instance;

    return &instance;
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
    //OutputDebugStringA(("GetSrvHandleGPU called with: " + filePath + "\n").c_str());

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
    //delete instance;
    //instance = nullptr;
    textureDatas.clear();
}