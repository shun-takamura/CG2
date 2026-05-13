#include "ModelInstance.h"
#include <filesystem> // std::filesystem::path を使うために必要

void ModelInstance::Initialize(ModelCore* modelCore, const std::string& directorPath, const std::string& filename)
{
	// CPU フェーズ → GPU フェーズの順で同期実行
	LoadCPU(directorPath, filename);
	InitializeGPU(modelCore, modelCore->GetDXCore());
}

void ModelInstance::LoadCPU(const std::string& directoryPath, const std::string& filename)
{
	// 多重ロード防止
	if (loadState_ != LoadState::Unloaded) return;

	// Assimp パース。modelData_ を埋めるだけで GPU 呼び出しは行わない
	// ※ バックグラウンドスレッドからも呼ばれるため、ここでは assert を使わない
	LoadModel(directoryPath, filename);

	// テクスチャファイルパスは文字列コピーのみ（GPUロードはGPUフェーズで検証する）
	textureFilePath_ = modelData_.materialData.textureFilePath;

	loadState_ = LoadState::CPUReady;
}

void ModelInstance::InitializeGPU(ModelCore* modelCore, DirectXCore* dxCore)
{
	// 既に GPU まで完了済みなら何もしない
	if (loadState_ == LoadState::GPUReady) return;

	// CPU パースが終わっていることを保証
	assert(loadState_ == LoadState::CPUReady && "InitializeGPU called before LoadCPU");
	assert(!textureFilePath_.empty() && "textureFilePath is empty!");

	modelCore_ = modelCore;

	// 頂点 / インデックス / マテリアル GPU リソースの作成
	CreateVertexData(dxCore);
	CreateIndexData(dxCore);
	CreateMaterialData(dxCore);

	// テクスチャを TextureManager に登録（GPU リソース作成）
	TextureManager::GetInstance()->LoadTexture(textureFilePath_);

	loadState_ = LoadState::GPUReady;
}

void ModelInstance::Draw(DirectXCore* dxCore)
{
	// デバッグ: ファイルパスが空でないか確認
	assert(!textureFilePath_.empty() && "textureFilePath is empty in Draw!");

	// 追加：インデックスバッファが有効か確認
	assert(indexResource_ != nullptr && "indexResource is nullptr!");
	assert(indexBufferView_.BufferLocation != 0 && "indexBufferView is not set!");
	assert(!modelData_.indices.empty() && "indices is empty in Draw!");

	// VBVを設定
	dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// インデックスバッファ設定
	dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

	// マテリアルCBufferの場所を設定
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
		0, materialResource_->GetGPUVirtualAddress()
	);

	// SRVのDescriptorTableの先頭を設定
	dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
	);

	// ドローコール
	dxCore->GetCommandList()->DrawIndexedInstanced(
		UINT(modelData_.indices.size()), 1, 0, 0, 0);
}

void ModelInstance::CreateVertexData(DirectXCore* dxCore)
{
	const size_t sizeInBytes = sizeof(VertexData) * modelData_.vertices.size();

	// DEFAULT heap に頂点バッファを作成（中間 UPLOAD バッファ経由でアップロード）
	vertexResource_ = dxCore->CreateDefaultBuffer(
		sizeInBytes,
		modelData_.vertices.data(),
		dxCore->GetCommandList(),
		D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);

	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = UINT(sizeInBytes);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void ModelInstance::CreateMaterialData(DirectXCore* dxCore)
{
	// マテリアル用のリソースを作る。
	materialResource_ = dxCore->CreateBufferResource(sizeof(Material));

	// 書き込むためのアドレスを取得してmaterialDataに割り当てる
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material_));

	// 白で書き込む
	material_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// Lightingを有効にする
	material_->enableLighting = true;

	// UVTransformに単位行列を書き込む
	material_->uvTransform = MakeIdentity4x4();

	// 光沢度の初期値。小さいほどぼんやり広く、大きいほどくっきり狭く
	material_->shininess = 50.0f;

	// 環境マップの反映度合い。1.0だと映り込みすぎになる
	material_->environmentCoefficient = 1.0f;

	// デフォルトで環境マップを使用しない
	material_->useEnvironmentMap = false;  
}

void ModelInstance::CreateIndexData(DirectXCore* dxCore)
{
	const size_t sizeInBytes = sizeof(uint32_t) * modelData_.indices.size();

	indexResource_ = dxCore->CreateDefaultBuffer(
		sizeInBytes,
		modelData_.indices.data(),
		dxCore->GetCommandList(),
		D3D12_RESOURCE_STATE_INDEX_BUFFER);

	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = UINT(sizeInBytes);
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

Node ModelInstance::ReadNode(aiNode* node)
{
	Node result;

	// assimpの行列をSRTに分解する（Decomposeはassimpの関数）
	aiVector3D scale, translate;
	aiQuaternion rotate;
	node->mTransformation.Decompose(scale, rotate, translate);

	// 右手系→左手系の変換
	result.transform.scale = { scale.x, scale.y, scale.z };               // Scaleはそのまま
	result.transform.rotate = { rotate.x, -rotate.y, -rotate.z, rotate.w }; // x軸を反転、回転方向が逆なので軸も反転
	result.transform.translate = { -translate.x, translate.y, translate.z };  // x軸を反転

	// localMatrixを再構築（Quaternion版MakeAffineMatrix）
	result.localMatrix = MakeAffineMatrix(
		result.transform.scale, result.transform.rotate, result.transform.translate);

	result.name = node->mName.C_Str();
	result.children.resize(node->mNumChildren);
	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
	}
	return result;
}

void ModelInstance::LoadModel(const std::string& directoryPath, const std::string& filename)
{
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;

	const aiScene* scene = importer.ReadFile(filePath.c_str(),
		aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate);
	assert(scene->HasMeshes());

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());
		assert(mesh->HasTextureCoords(0));

		// 複数メッシュ対応：このmeshの頂点が始まる位置を記録
		uint32_t vertexOffset = static_cast<uint32_t>(modelData_.vertices.size());

		// 頂点解析
		for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
			aiVector3D& position = mesh->mVertices[vertexIndex];
			aiVector3D& normal = mesh->mNormals[vertexIndex];
			aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

			VertexData vertex;
			vertex.position = { -position.x, position.y, position.z, 1.0f };
			vertex.normal = { -normal.x, normal.y, normal.z };
			vertex.texcoord = { texcoord.x, texcoord.y };
			modelData_.vertices.push_back(vertex);
		}

		// Index解析
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3);

			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				modelData_.indices.push_back(vertexOffset + vertexIndex);
			}
		}
	}

	// Material解析
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			// モデルファイルのあるディレクトリを基準にテクスチャパスを解決
			std::string modelDirectory = std::filesystem::path(filePath).parent_path().string();
			std::filesystem::path fullPath = std::filesystem::path(modelDirectory) / textureFilePath.C_Str();
			modelData_.materialData.textureFilePath = fullPath.lexically_normal().string();
		}
	}

	modelData_.rootNode = ReadNode(scene->mRootNode);

	assert(!modelData_.indices.empty() && "indices is empty!");
}

MaterialData ModelInstance::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
{
	//======================
	// 中で必要となる変数の宣言
	//======================
	MaterialData materialData; // 構築するMaterialData
	std::string line; // ファイルから読んだ1行を格納する場所

	//======================
	// ファイルを開く
	//======================
	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	//============================================
	// 実際にファイルを読み,MaterialDataを構築していく
	//============================================
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier;

		//=============================
		// identifierに応じて処理をしていく
		//=============================
		if (identifier == "map_Kd") {
			std::string textureFilename;
			s >> textureFilename;

			// 連結してファイルパスにする
			materialData.textureFilePath = directoryPath + "/" + textureFilename;

		}

	}

	//======================
	// MaterialDataを返す
	//======================
	return materialData;

}
