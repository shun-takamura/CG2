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
	//// 読み込み済みテクスチャを検索
	//auto it = std::find_if(
	//	textureDatas.begin(),
	//	textureDatas.end(),
	//	[&](const TextureData& textureData) {
	//		return textureData.filePath == filePath;
	//	}
	//);

	//// テクスチャ枚数上限チェック
	//assert(textureDatas.size() + kSRVIndexTop < DirectXCore::kMaxTextureCount);

	//if (it != textureDatas.end()) {
	//	// 既に読み込み済みなので何もしない
	//	return;
	//}

	//// テクスチャファイルを読み込んでプログラムで扱えるようにする
	//DirectX::ScratchImage image{};
	//std::wstring filePathW = ConvertString(filePath);
	//HRESULT hr = DirectX::LoadFromWICFile(
	//	filePathW.c_str(), DirectX::WIC_FLAGS_FORCE_SRGB, nullptr, image);
	//assert(SUCCEEDED(hr));

	//// ミップマップの作成
	//DirectX::ScratchImage mipImages{};
	//hr = DirectX::GenerateMipMaps(
	//	image.GetImages(), image.GetImageCount(), image.GetMetadata(),
	//	DirectX::TEX_FILTER_SRGB, 0, mipImages);
	//assert(SUCCEEDED(hr));

	//// テクスチャデータを追加
	//// std::vectorに対して現在の要素数+1
	//textureDatas.resize(textureDatas.size() + 1);

	//// 追加したテクスチャデータの参照を取得。上で追加したやつ
	//TextureData& textureData = textureDatas.back();

	//// テクスチャデータの書き込み
	//textureData.filePath = filePath;
	//textureData.metadata = mipImages.GetMetadata();

	//// テクスチャリソース生成
	//textureData.resource = spriteManager_->CreateTextureResource(
	//	dxCore_->GetDevice(),
	//	textureData.metadata
	//);

	//// テクスチャデータ転送
	//spriteManager_->UploadTextureData(
	//	textureData.resource.Get(),
	//	mipImages,
	//	dxCore_->GetDevice(),
	//	dxCore_->GetCommandList()
	//);

	//// SRVインデックス計算
	//uint32_t srvIndex =static_cast<uint32_t>(textureDatas.size() - 1) + kSRVIndexTop;

	//// SRVハンドル取得
	//ID3D12DescriptorHeap* srvHeap =
	//	dxCore_->GetSrvDescriptorHeap();

	//UINT descriptorSize =
	//	dxCore_->GetSrvDescriptorSize();

	//textureData.srvHandleCPU =
	//	srvHeap->GetCPUDescriptorHandleForHeapStart();
	//textureData.srvHandleCPU.ptr +=
	//	descriptorSize * srvIndex;

	//textureData.srvHandleGPU =
	//	srvHeap->GetGPUDescriptorHandleForHeapStart();
	//textureData.srvHandleGPU.ptr +=
	//	descriptorSize * srvIndex;

	//// --- SRV 生成 ---
	//D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	//srvDesc.Format = textureData.metadata.format;
	//srvDesc.Shader4ComponentMapping =
	//	D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	//srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//srvDesc.Texture2D.MipLevels =
	//	static_cast<UINT>(textureData.metadata.mipLevels);

	//dxCore_->GetDevice()->CreateShaderResourceView(
	//	textureData.resource.Get(),
	//	&srvDesc,
	//	textureData.srvHandleCPU
	//);
}

TextureManager* TextureManager::GetInstance()
{
	if (instance == nullptr) {
		instance = new TextureManager;
	}

	return instance;
}

//uint32_t TextureManager::GetTextureIndexByFilePath(const std::string& filePath)
//{
//	// 読み込み済みテクスチャを検索
//	auto it = std::find_if(
//		textureDatas.begin(),
//		textureDatas.end(),
//		[&](const TextureData& textureData) {
//			return textureData.filePath == filePath;
//		}
//	);
//
//	if (it != textureDatas.end()) {
//		// 読み込み済みなら要素番号を返す（LoadTextureと同じSRVIndex規則に合わせる）
//		uint32_t textureIndex =
//			static_cast<uint32_t>(std::distance(textureDatas.begin(), it));
//
//		return textureIndex + kSRVIndexTop;
//	}
//
//	// 見つからないなら事前に読み込みできていないので停止する
//	assert(0);
//	return 0;
//}
//
//D3D12_GPU_DESCRIPTOR_HANDLE TextureManager::GetSrvHandleGPU(uint32_t textureIndex)
//{
//	// SRVマネージャーからGPUハンドルを取得
//	return srvManager_->GetGPUDescriptorHandle(textureIndex);
//
//	//// 範囲外指定速度チェック
//	//// textureIndex は SRVヒープ上のインデックス（0番はImGui想定）
//	//assert(textureIndex >= kSRVIndexTop);
//	//assert(textureIndex < (kSRVIndexTop + textureDatas.size()));
//
//	//// TextureData配列の添字へ変換
//	//const uint32_t dataIndex = textureIndex - kSRVIndexTop;
//
//	//// テクスチャデータの参照を取得
//	//TextureData& textureData = textureDatas[dataIndex];
//
//	//return textureData.srvHandleGPU;
//}
//
//
//const DirectX::TexMetadata& TextureManager::GetMetaData(uint32_t textureIndex)
//{
//	// テクスチャデータを検索
//	for (const auto& pair : textureDatas) {
//		if (pair.second.srvIndex == textureIndex) {
//			return pair.second.metadata;
//		}
//	}
//
//	// 見つからない場合は停止
//	assert(0);
//	static DirectX::TexMetadata dummy{};
//	return dummy;
//}

//const DirectX::TexMetadata& TextureManager::GetMetaData(uint32_t textureIndex)
//{
//	// 範囲外指定速度チェック
//    // textureIndex は SRVヒープ上のインデックス（0番はImGui想定）
//	assert(textureIndex >= kSRVIndexTop);
//	assert(textureIndex < (kSRVIndexTop + textureDatas.size()));
//
//	// TextureData配列の添字へ変換
//	const uint32_t dataIndex = textureIndex - kSRVIndexTop;
//
//	// テクスチャデータの参照を取得
//	TextureData& textureData = textureDatas[dataIndex];
//
//	return textureData.metadata;
//}

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
