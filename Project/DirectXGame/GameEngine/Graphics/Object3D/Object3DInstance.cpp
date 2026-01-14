#include "Object3DInstance.h"

void Object3DInstance::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore)
{
	object3DManager_ = object3DManager;

	ModelManager* modelManager = new ModelManager;
	modelManager->Initialize(dxCore);

	modelInstance_ = new ModelInstance;
	modelInstance_->Initialize(modelManager);

	// モデル読み込み
	//modelData_ = LoadObjFile("Resources", "axis.obj");

	/*CreateVertexData(dxCore);
	CreateMaterialData(dxCore);*/
	CreateTransformationMatrixResource(dxCore);
	CreateDirectionalLight(dxCore);

	//// objファイルの参照しているテクスチャファイルの読み込み
	//TextureManager::GetInstance()->LoadTexture(modelData_.materialData.textureFilePath);
	////ここで停止する原因はテクスチャマネージャーの生成順だったからちょっと資料と変えた

	//// 読み込んだテクスチャの番号を取得
	//modelData_.materialData.textureIndex =
	//	TextureManager::GetInstance()->GetTextureIndexByFilePath(modelData_.materialData.textureFilePath);

	// Transform変数を作る
	transform_ = {
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f}
	};

	cameraTransform_ = {
		{1.0f,1.0f,1.0f},
		{0.3f,0.0f,0.0f},
		{0.0f,0.0f,-10.0f}
	};

}

void Object3DInstance::Update()
{
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
	Matrix4x4 cameraMatrix = MakeAffineMatrix(cameraTransform_);
	Matrix4x4 viewMatrix= Inverse(cameraMatrix);

	Matrix4x4 projectionMatrix = MakePerspectiveFovMatrix(0.45f, 16.0f / 9.0f, 0.1f, 100.0f);
	Matrix4x4 worldViewProjectionMatrix = Multiply(worldMatrix, Multiply(viewMatrix, projectionMatrix));
	transformationMatrixData_->World = worldMatrix;
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
}

void Object3DInstance::Draw(DirectXCore* dxCore)
{
	//// VBVを設定
	//dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);

	//// マテリアルCBufferの場所を設定
	//dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
	//	0,materialResource_->GetGPUVirtualAddress()
	//);

	// 座標変換行列CBufferの場所を設定
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
		1,transformationMatrixResource_->GetGPUVirtualAddress()
	);

	//// SRVのDescriptorTableの先頭を設定
	//dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
	//	2,TextureManager::GetInstance()->GetSrvHandleGPU(modelData_.materialData.textureIndex)
	//);

	// rootParameter[3]のところにライトのリソースを入れる。CPUに送る
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

	// 3Dモデルがあれば描画
	if (modelInstance_) {
		modelInstance_->Draw(dxCore);
	}

	//// ドローコール
	//dxCore->GetCommandList()->DrawInstanced(UINT(modelData_.vertices.size()), 1, 0, 0);

}
Object3DInstance::~Object3DInstance()
{
	if (modelInstance_) {
		delete modelInstance_;
	}
}
//
//ModelData Object3DInstance::LoadObjFile(const std::string& directorPath, const std::string& filename)
//{
//	//==========================
//	// 必要なファイルの宣言
//	//==========================
//	ModelData modelData; // 構築するModelData
//	std::vector<Vector4> positions; // 位置
//	std::vector<Vector3> normals; // 法線
//	std::vector<Vector2> texcoords; // テクスチャの座標
//	std::string line; // ファイルから読んだ1行を格納する場所
//
//	//==========================
//	// ファイルを開く
//	//==========================
//	std::ifstream file(directorPath + "/" + filename); // ファイルを開く
//	assert(file.is_open()); // 開けなかったら止める
//
//	//==========================================
//	// 実際にファイルを読み、ModelDataを構築していく
//	//==========================================
//	while (std::getline(file, line)) {
//		std::string identifier;
//		std::istringstream s(line);
//		s >> identifier; // 先頭の識別子を読む
//
//		//=============================
//		// identifierに応じて処理をしていく
//		//=============================
//		if (identifier == "v") { // "v" :頂点位置
//			Vector4 position;
//			s >> position.x >> position.y >> position.z;
//			position.w = 1.0f;
//			positions.push_back(position);
//
//		} else if (identifier == "vt") { // "vt":頂点テクスチャ座標
//			Vector2 texcood;
//			s >> texcood.x >> texcood.y;
//			texcoords.push_back(texcood);
//
//		} else if (identifier == "vn") { // "vn":頂点法線
//			Vector3 normal;
//			s >> normal.x >> normal.y >> normal.z;
//			normals.push_back(normal);
//
//		} else if (identifier == "f") { // "f" :面
//			VertexData triangle[3];
//
//			// 面は三角形限定。その他は未対応
//			for (int32_t faceVertex = 0; faceVertex < 3; ++faceVertex) {
//				std::string vertexDefinition;
//				s >> vertexDefinition;
//				// 頂点の要素へのIndexは「位置/UV/法線」で格納されているので、分解してIndexを取得する
//				std::istringstream v(vertexDefinition);
//				uint32_t elementIndices[3];
//				for (int32_t element = 0; element < 3; ++element) {
//					std::string index;
//					std::getline(v, index, '/');// "/"区切りでindexを読んでいく
//					elementIndices[element] = std::stoi(index);
//				}
//
//				// 要素へのIndexから、実際の要素の値を取得して頂点を構築する
//				// 1始まりだから-1しておく
//				Vector4 position = positions[elementIndices[0] - 1];
//				Vector2 texcoord = texcoords[elementIndices[1] - 1];
//				Vector3 normal = normals[elementIndices[2] - 1];
//				texcoord.y = 1.0f - texcoord.y;
//
//				position.x *= -1.0f;
//				normal.x *= -1.0f;
//
//				triangle[faceVertex] = { position,texcoord,normal };
//
//			}
//
//			// 頂点を逆順で登録することで回り順を逆にする
//			modelData.vertices.push_back(triangle[2]);
//			modelData.vertices.push_back(triangle[1]);
//			modelData.vertices.push_back(triangle[0]);
//
//		} else if (identifier == "mtllib") {
//			// materialTemplateLibraryファイルの名前を取得する
//			std::string materialFilename;
//			s >> materialFilename;
//
//			// 基本的にobjファイルと同一階層にmtlは存在するので、ディレクトリ名とファイル名を渡す
//			modelData.materialData = LoadMaterialTemplateFile(directorPath, materialFilename);
//
//		}
//
//	}
//
//	//==========================
//	// ModelDataを返す
//	//==========================
//	return modelData;
//
//}
//
//MaterialData Object3DInstance::LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename)
//{
//	//======================
//	// 中で必要となる変数の宣言
//	//======================
//	MaterialData materialData; // 構築するMaterialData
//	std::string line; // ファイルから読んだ1行を格納する場所
//
//	//======================
//	// ファイルを開く
//	//======================
//	std::ifstream file(directoryPath + "/" + filename); // ファイルを開く
//	assert(file.is_open()); // 開けなかったら止める
//
//	//============================================
//	// 実際にファイルを読み,MaterialDataを構築していく
//	//============================================
//	while (std::getline(file, line)) {
//		std::string identifier;
//		std::istringstream s(line);
//		s >> identifier;
//
//		//=============================
//		// identifierに応じて処理をしていく
//		//=============================
//		if (identifier == "map_Kd") {
//			std::string textureFilename;
//			s >> textureFilename;
//
//			// 連結してファイルパスにする
//			materialData.textureFilePath = directoryPath + "/" + textureFilename;
//
//		}
//
//	}
//
//	//======================
//	// MaterialDataを返す
//	//======================
//	return materialData;
//
//}
//
//void Object3DInstance::CreateVertexData(DirectXCore* dxCore)
//{
//	vertexResource_=dxCore->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
//
//	// リソースの先頭アドレスから使用
//	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
//
//	// 使用するリソースのサイズ
//	vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
//
//	// 1頂点当たりのサイズ
//	vertexBufferView_.StrideInBytes = sizeof(VertexData);
//
//	// 書き込むためのアドレスを取得
//	vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
//
//	// 取得したアドレスに書き込む
//	std::memcpy(vertexData_, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
//
//}
//
//void Object3DInstance::CreateMaterialData(DirectXCore* dxCore)
//{
//	// マテリアル用のリソースを作る。
//	materialResource_=dxCore->CreateBufferResource(sizeof(Material));
//
//	// 書き込むためのアドレスを取得してmaterialDataに割り当てる
//	materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material_));
//
//	// 白で書き込む
//	material_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
//	
//	// 球のみLightingを有効にする
//	material_->enableLighting = true;
//	
//	// UVTransformに単位行列を書き込む
//	material_->uvTransform = MakeIdentity4x4();
//}

void Object3DInstance::CreateTransformationMatrixResource(DirectXCore* dxCore)
{
	// サイズを設定
	UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

	//// WVP用のリソースを作る。
	//Microsoft::WRL::ComPtr<ID3D12Resource> wvpResource = dxCore->CreateBufferResource(transformationMatrixSize);

	//// 書き込むためのアドレスを取得
	//wvpResource->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));
	
	// WVP用のリソースを作る。
	transformationMatrixResource_ = dxCore->CreateBufferResource(transformationMatrixSize);

	// 書き込むためのアドレスを取得
	transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

	// 単位行列を書き込む
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();

}

void Object3DInstance::CreateDirectionalLight(DirectXCore* dxCore)
{
	// サイズを設定
	directionalLightResource_ = dxCore->CreateBufferResource(sizeof(DirectionalLight));

	// 書き込むためのアドレスを取得してdirectionalLightDataに割り当てる
	directionalLightResource_->Map(0, nullptr, reinterpret_cast<void**>(&directionalLightData_));

	// 白で書き込む
	directionalLightData_->color = { 1.0f,1.0f,1.0f,1.0f };

	// ライトの向きを設定
	directionalLightData_->direction = { 0.0f,-1.0f,0.0f };

	// なんの設定かわすれた
	directionalLightData_->intensity = 1.0f;

	// ライトの方式の設定
	LightingMode lightingMode = LightingMode::None;
}
