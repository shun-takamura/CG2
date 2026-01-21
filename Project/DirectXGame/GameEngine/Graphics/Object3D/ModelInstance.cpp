#include "ModelInstance.h"

void ModelInstance::Initialize(ModelCore* modelCore, const std::string& directorPath, const std::string& filename)
{
	modelCore_ = modelCore;

	// モデル読み込み
	LoadModel(directorPath, filename);

	// 頂点データ作成
	CreateVertexData(modelCore_->GetDXCore());

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

	// VBVを設定
	dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// マテリアルCBufferの場所を設定
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
		0, materialResource_->GetGPUVirtualAddress()
	);

	// SRVのDescriptorTableの先頭を設定
	dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
		2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
	);

	// ドローコール
	dxCore->GetCommandList()->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);
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

ModelData ModelInstance::LoadObjFile(const std::string& directorPath, const std::string& filename)
{
	//==========================
	// 必要なファイルの宣言
	//==========================
	ModelData modelData; // 構築するModelData
	std::vector<Vector4> positions; // 位置
	std::vector<Vector3> normals; // 法線
	std::vector<Vector2> texcoords; // テクスチャの座標
	std::string line; // ファイルから読んだ1行を格納する場所

	//==========================
	// ファイルを開く
	//==========================
	std::ifstream file(directorPath + "/" + filename); // ファイルを開く
	assert(file.is_open()); // 開けなかったら止める

	//==========================================
	// 実際にファイルを読み、ModelDataを構築していく
	//==========================================
	while (std::getline(file, line)) {
		std::string identifier;
		std::istringstream s(line);
		s >> identifier; // 先頭の識別子を読む

		//=============================
		// identifierに応じて処理をしていく
		//=============================
		if (identifier == "v") { // "v" :頂点位置
			Vector4 position;
			s >> position.x >> position.y >> position.z;
			position.w = 1.0f;
			positions.push_back(position);

		} else if (identifier == "vt") { // "vt":頂点テクスチャ座標
			Vector2 texcood;
			s >> texcood.x >> texcood.y;
			texcoords.push_back(texcood);

		} else if (identifier == "vn") { // "vn":頂点法線
			Vector3 normal;
			s >> normal.x >> normal.y >> normal.z;
			normals.push_back(normal);

		} else if (identifier == "f") { // "f" :面
			VertexData triangle[3];

			// 面は三角形限定。その他は未対応
			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
				std::string vertexDefinition;
				s >> vertexDefinition;
				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
				std::istringstream v(vertexDefinition);
				uint32_t elementIndices[3];
				for (int32_t element = 0; element < 3; ++element) {
					std::string index;
					std::getline(v, index, '/');// "/"区切りでindexを読んでいく
					elementIndices[element] = std::stoi(index);
				}

				// 要素へのIndexから、実際の要素の値を取得して頂点を構築する
				// 1始まりだから-1しておく
				Vector4 position = positions[elementIndices[0] - 1];
				Vector2 texcoord = texcoords[elementIndices[1] - 1];
				Vector3 normal = normals[elementIndices[2] - 1];
				texcoord.y = 1.0f - texcoord.y;

				position.x *= -1.0f;
				normal.x *= -1.0f;

				triangle[faceVertex] = { position,texcoord,normal };

			}

			// 頂点を逆順で登録することで回り順を逆にする
			modelData.vertices.push_back(triangle[2]);
			modelData.vertices.push_back(triangle[1]);
			modelData.vertices.push_back(triangle[0]);

		} else if (identifier == "mtllib") {
			// materialTemplateLibraryファイルの名前を取得する
			std::string materialFilename;
			s >> materialFilename;

			// 基本的にobjファイルと同一階層にmtlは存在するので、ディレクトリ名とファイル名を渡す
			modelData.materialData = LoadMaterialTemplateFile(directorPath, materialFilename);

		}

	}

	//==========================
	// ModelDataを返す
	//==========================
	return modelData;

}

void ModelInstance::LoadModel(const std::string& directoryPath, const std::string& filename)
{
	Assimp::Importer importer;
	std::string filePath = directoryPath + "/" + filename;

	const aiScene* scene = importer.ReadFile(filePath.c_str(),
		aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate);
	assert(scene->HasMeshes());

	// Meshの解析
	for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
		aiMesh* mesh = scene->mMeshes[meshIndex];
		assert(mesh->HasNormals());
		assert(mesh->HasTextureCoords(0));

		// Faceの解析
		for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
			aiFace& face = mesh->mFaces[faceIndex];
			assert(face.mNumIndices == 3);

			// Vertexの解析
			for (uint32_t element = 0; element < face.mNumIndices; ++element) {
				uint32_t vertexIndex = face.mIndices[element];
				aiVector3D& position = mesh->mVertices[vertexIndex];
				aiVector3D& normal = mesh->mNormals[vertexIndex];
				aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

				VertexData vertex;
				vertex.position = { position.x, position.y, position.z, 1.0f };
				vertex.normal = { normal.x, normal.y, normal.z };
				vertex.texcoord = { texcoord.x, texcoord.y };

				// 左手座標系への変換
				vertex.position.x *= -1.0f;
				vertex.normal.x *= -1.0f;

				modelData_.vertices.push_back(vertex);
			}
		}
	}

	// Materialの解析
	for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
		aiMaterial* material = scene->mMaterials[materialIndex];
		if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
			aiString textureFilePath;
			material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
			modelData_.materialData.textureFilePath = directoryPath + "/" + textureFilePath.C_Str();
		}
	}
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
