#include "TextureManager.h"
TextureManager* TextureManager::instance = nullptr;

void TextureManager::Initialize()
{
	// SRVの数と同数
	textureDatas.reserve(DirectXCore::kMaxTextureCount);
}

void TextureManager::LoadTexture(const std::string& filePath)
{
	// 読み込み済みテクスチャを検索
	auto it = std::find_if(
		textureDatas.begin(),
		textureDatas.end(),
		[&](const TextureData& textureData) {
			return textureData.filePath == filePath;
		}
	);

	if (it != textureDatas.end()) {
		// 既に読み込み済みなので何もしない
		return;
	}

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

	// テクスチャデータを追加
	// std::vectorに対して現在の要素数+1
	textureDatas.resize(textureDatas.size() + 1);

	// 追加したテクスチャデータの参照を取得。上で追加したやつ
	TextureData& textureData = textureDatas.back();

	// テクスチャデータの書き込み
	textureData.filePath = filePath;
	textureData.metadata = mipImages.GetMetadata();

	// テクスチャリソース生成
	textureData.resource = DirectXCore::GetInstance()->CreateTextureResource(
		textureData.metadata
	);

	// テクスチャデータ転送
	UploadTextureData(
		textureData.resource.Get(),
		mipImages
	);

	// SRVインデックス計算
	uint32_t srvIndex =
		static_cast<uint32_t>(textureDatas.size() - 1);

	// SRVハンドル取得
	ID3D12DescriptorHeap* srvHeap =
		DirectXCore::GetSrvDescriptorHeap();

	UINT descriptorSize =
		DirectXCore::GetSrvDescriptorSize();

	textureData.srvHandleCPU =
		srvHeap->GetCPUDescriptorHandleForHeapStart();
	textureData.srvHandleCPU.ptr +=
		descriptorSize * srvIndex;

	textureData.srvHandleGPU =
		srvHeap->GetGPUDescriptorHandleForHeapStart();
	textureData.srvHandleGPU.ptr +=
		descriptorSize * srvIndex;

	// --- SRV 生成 ---
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Format = textureData.metadata.format;
	srvDesc.Shader4ComponentMapping =
		D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels =
		static_cast<UINT>(textureData.metadata.mipLevels);

	DirectXCore::GetDevice()->CreateShaderResourceView(
		textureData.resource.Get(),
		&srvDesc,
		textureData.srvHandleCPU
	);
}

TextureManager* TextureManager::GetInstance()
{
	if (instance == nullptr) {
		instance = new TextureManager;
	}

	return instance;
}

void TextureManager::Finalize()
{
	delete instance;
	instance = nullptr;
}
