#include "ModelInstance.h"
#include <unordered_map>

struct VertexHash {
	size_t operator()(const VertexData& v) const {
		size_t h1 = std::hash<float>()(v.position.x);
		size_t h2 = std::hash<float>()(v.position.y);
		size_t h3 = std::hash<float>()(v.position.z);
		size_t h4 = std::hash<float>()(v.texcoord.x);
		size_t h5 = std::hash<float>()(v.texcoord.y);
		return h1 ^ (h2 << 1) ^ (h3 << 2) ^ (h4 << 3) ^ (h5 << 4);
	}
};

struct VertexEqual {
	bool operator()(const VertexData& a, const VertexData& b) const {
		return a.position.x == b.position.x && a.position.y == b.position.y &&
			a.position.z == b.position.z && a.normal.x == b.normal.x &&
			a.normal.y == b.normal.y && a.normal.z == b.normal.z &&
			a.texcoord.x == b.texcoord.x && a.texcoord.y == b.texcoord.y;
	}
};

void ModelInstance::Initialize(ModelCore* modelCore, const std::string& directorPath, const std::string& filename)
{
	modelCore_ = modelCore;

	// モデル読み込み
	LoadModel(directorPath, filename);

	// 頂点データ作成
	CreateVertexData(modelCore_->GetDXCore());

	// インデックスデータ作成
	CreateIndexData(modelCore_->GetDXCore());

	// マテリアルデータ作成
	CreateMaterialData(modelCore_->GetDXCore());

	// objファイルの参照しているテクスチャファイルの読み込み
	textureFilePath_ = modelData_.materialData.textureFilePath;

	// デバッグ: ファイルパスが空でないか確認
	assert(!textureFilePath_.empty() && "textureFilePath is empty!");

	TextureManager::GetInstance()->LoadTexture(textureFilePath_);
	//ここで停止する原因はテクスチャマネージャーの生成順だったからちょっと資料と変えた

	// 読み込んだテクスチャの番号を取得
	/*modelData_.materialData.textureIndex =
		TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData_.materialData.textureFilePath);*/
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
	vertexResource_ = dxCore->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());

	// リソースの先頭アドレスから使用
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();

	// 使用するリソースのサイズ
	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());

	// 1頂点当たりのサイズ
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	// 書き込むためのアドレスを取得
	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));

	// 取得したアドレスに書き込む
	std::memcpy(vertexData_, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
}

void ModelInstance::CreateMaterialData(DirectXCore* dxCore)
{
	// マテリアル用のリソースを作る。
	materialResource_ = dxCore->CreateBufferResource(sizeof(Material));

	// 書き込むためのアドレスを取得してmaterialDataに割り当てる
	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material_));

	// 白で書き込む
	material_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

	// 球のみLightingを有効にする
	material_->enableLighting = true;

	// UVTransformに単位行列を書き込む
	material_->uvTransform = MakeIdentity4x4();

	// 光沢度の初期値。小さいほどぼんやり広く、大きいほどくっきり狭く
	material_->shininess = 50.0f;
}

void ModelInstance::CreateIndexData(DirectXCore* dxCore)
{
	indexResource_ = dxCore->CreateBufferResource(sizeof(uint32_t) * modelData_.indices.size());

	indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
	indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * modelData_.indices.size());
	indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

	uint32_t* indexData = nullptr;
	indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
	std::memcpy(indexData, modelData_.indices.data(), sizeof(uint32_t) * modelData_.indices.size());
	indexResource_->Unmap(0, nullptr);
}

Node ModelInstance::ReadNode(aiNode* node)
{
	Node result;

	// ノードのローカルマトリクスを取得
	aiMatrix4x4 aiLocalMatrix = node->mTransformation;

	// 列ベクトルを行ベクトルに転置
	aiLocalMatrix.Transpose();

	// ローカルマトリクスを代入
	result.localMatrix.m[0][0] = aiLocalMatrix.a1;
	result.localMatrix.m[0][1] = aiLocalMatrix.a2;
	result.localMatrix.m[0][2] = aiLocalMatrix.a3;
	result.localMatrix.m[0][3] = aiLocalMatrix.a4;

	result.localMatrix.m[1][0] = aiLocalMatrix.b1;
	result.localMatrix.m[1][1] = aiLocalMatrix.b2;
	result.localMatrix.m[1][2] = aiLocalMatrix.b3;
	result.localMatrix.m[1][3] = aiLocalMatrix.b4;

	result.localMatrix.m[2][0] = aiLocalMatrix.c1;
	result.localMatrix.m[2][1] = aiLocalMatrix.c2;
	result.localMatrix.m[2][2] = aiLocalMatrix.c3;
	result.localMatrix.m[2][3] = aiLocalMatrix.c4;

	result.localMatrix.m[3][0] = aiLocalMatrix.d1;
	result.localMatrix.m[3][1] = aiLocalMatrix.d2;
	result.localMatrix.m[3][2] = aiLocalMatrix.d3;
	result.localMatrix.m[3][3] = aiLocalMatrix.d4;

	// Node名を格納
	result.name = node->mName.C_Str();

	// 子供の数だけ確保
	result.children.resize(node->mNumChildren);

	for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
		// 再帰的に読んで階層構造を作っていく
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

	// 重複チェック用
	std::unordered_map<VertexData, uint32_t, VertexHash, VertexEqual> uniqueVertices;

	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());
		assert(mesh->HasTextureCoords(0));

		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3);

			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				aiVector3D& position = mesh->mVertices[vertexIndex];
				aiVector3D& normal = mesh->mNormals[vertexIndex];
				aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

				VertexData vertex;
				vertex.position = { -position.x, position.y, position.z, 1.0f };
				vertex.normal = { -normal.x, normal.y, normal.z };
				vertex.texcoord = { texcoord.x, texcoord.y };

				// 重複チェック
				if (uniqueVertices.count(vertex) == 0) {
					uniqueVertices[vertex] = static_cast<uint32_t>(modelData_.vertices.size());
					modelData_.vertices.push_back(vertex);
				}
				modelData_.indices.push_back(uniqueVertices[vertex]);
			}
		}
	}

	// Material解析
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			modelData_.materialData.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
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
