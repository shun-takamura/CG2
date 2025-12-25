#include "TextureManager.h"
TextureManager* TextureManager::instance = nullptr;

void TextureManager::Initialize()
{
	// SRVの数と同数
	textureDatas.reserve(DirectXCore::kMaxTextureCount);
}

void TextureManager::LoadTexture(const std::string& filePath)
{
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
