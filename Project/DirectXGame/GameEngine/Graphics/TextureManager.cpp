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
