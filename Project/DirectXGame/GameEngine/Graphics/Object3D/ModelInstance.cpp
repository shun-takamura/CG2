#include "ModelInstance.h"
#include <filesystem> // std::filesystem::path を使うために必要
#include <cstring>
#include "AssetLocator.h"
#include "DStorageManager.h"
#include "PepperMacros.h"

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

	// 拡張子で経路分岐: .mesh は Cooker が出した直読みバイナリ、それ以外は assimp
	// ※ バックグラウンドスレッドからも呼ばれるため、ここでは assert を使わない
	std::filesystem::path fname(filename);
	if (fname.extension() == ".mesh") {
		LoadMeshBinary(directoryPath, filename);
	} else {
		LoadModel(directoryPath, filename);
	}

	// テクスチャファイルパスは文字列コピーのみ（GPUロードはGPUフェーズで検証する）
	textureFilePath_ = modelData_.materialData.textureFilePath;

	// CPU 経路のときは modelData_ から count を反映（DStorage 経路は LoadMeshBinary で設定済み）
	if (!useDirectStorage_) {
		vertexCount_ = static_cast<uint32_t>(modelData_.vertices.size());
		indexCount_  = static_cast<uint32_t>(modelData_.indices.size());
	}

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
	assert(indexCount_ > 0 && "indexCount is 0 in Draw!");

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
	PEPPER_COUNT("DrawCall");
	dxCore->GetCommandList()->DrawIndexedInstanced(
		indexCount_, 1, 0, 0, 0);
}

void ModelInstance::DrawIdPass(DirectXCore* dxCore)
{
	if (!indexResource_ || indexCount_ == 0) return;
	dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
	dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
	PEPPER_COUNT("DrawCall");
	dxCore->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void ModelInstance::DrawShadowPass(DirectXCore* dxCore)
{
	if (!indexResource_ || indexCount_ == 0) return;
	dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
	dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
	PEPPER_COUNT("DrawCall");
	dxCore->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void ModelInstance::CreateVertexData(DirectXCore* dxCore)
{
	const size_t sizeInBytes = useDirectStorage_
		? sizeof(VertexData) * vertexCount_
		: sizeof(VertexData) * modelData_.vertices.size();

	if (useDirectStorage_) {
		// DEFAULT heap に COMMON state でバッファ作成（DStorage の前提）
		D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = sizeInBytes;
		desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&vertexResource_));
		assert(SUCCEEDED(hr));

		// pack 内 .mesh のオフセットを取得して DStorage で直接 VRAM へ転送
		uint64_t packOffset = 0, packSize = 0;
		AssetLocator::GetInstance()->GetPackEntryInfo(meshFilePath_, packOffset, packSize);
		auto* ds = DStorageManager::GetInstance();
		ds->EnqueueBufferRead(ds->GetPackFile(),
			packOffset + vertexFileOffset_,
			static_cast<uint32_t>(sizeInBytes),
			vertexResource_.Get(), 0);
		ds->SubmitAndWait();

		// COMMON → VERTEX_AND_CONSTANT_BUFFER
		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = vertexResource_.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
		dxCore->GetCommandList()->ResourceBarrier(1, &barrier);
	} else {
		// DEFAULT heap に頂点バッファを作成（中間 UPLOAD バッファ経由でアップロード）
		vertexResource_ = dxCore->CreateDefaultBuffer(
			sizeInBytes,
			modelData_.vertices.data(),
			dxCore->GetCommandList(),
			D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);
	}

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

	// PBR パラメータの初期値（shadingModel=0=BlinnPhong。未初期化のゴミ値で誤って PBR に飛ばないよう明示）
	material_->metallic = 0.0f;
	material_->roughness = 0.5f;
	material_->shadingModel = 0;
}

void ModelInstance::CreateIndexData(DirectXCore* dxCore)
{
	const size_t sizeInBytes = useDirectStorage_
		? sizeof(uint32_t) * indexCount_
		: sizeof(uint32_t) * modelData_.indices.size();

	if (useDirectStorage_) {
		D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = sizeInBytes;
		desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
			&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COMMON, nullptr,
			IID_PPV_ARGS(&indexResource_));
		assert(SUCCEEDED(hr));

		uint64_t packOffset = 0, packSize = 0;
		AssetLocator::GetInstance()->GetPackEntryInfo(meshFilePath_, packOffset, packSize);
		auto* ds = DStorageManager::GetInstance();
		ds->EnqueueBufferRead(ds->GetPackFile(),
			packOffset + indexFileOffset_,
			static_cast<uint32_t>(sizeInBytes),
			indexResource_.Get(), 0);
		ds->SubmitAndWait();

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource   = indexResource_.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
		barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
		dxCore->GetCommandList()->ResourceBarrier(1, &barrier);
	} else {
		indexResource_ = dxCore->CreateDefaultBuffer(
			sizeInBytes,
			modelData_.indices.data(),
			dxCore->GetCommandList(),
			D3D12_RESOURCE_STATE_INDEX_BUFFER);
	}

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

// .mat (MATL) v1 から base_color_path を抽出するヘルパー
static std::string ReadMatBaseColorPath(const std::string& matPath)
{
	auto h = AssetLocator::GetInstance()->Open(matPath);
	if (!h.IsValid()) return {};

	char magic[4]{};
	h.Read(magic, 4);
	if (std::memcmp(magic, "MATL", 4) != 0) return {};

	uint32_t version = 0;
	h.Read(&version, 4);
	if (version != 1) return {};

	char baseColorPath[256]{};
	h.Read(baseColorPath, 256);
	return std::string(baseColorPath);
}

void ModelInstance::LoadMeshBinary(const std::string& directoryPath, const std::string& filename)
{
	// Cooker (tools/Python/cook_assets.py) が出力した .mesh v2 バイナリを直読みする。
	// v2 フォーマット:
	//   [Header 296B] magic("MESH") + version(2) + flags + vertex_count + index_count
	//                 + submesh_count + vertex_offset + index_offset + skin_offset
	//                 + submesh_offset + skeleton_path[256]
	//   [Vertex Data]   VertexData × vertex_count
	//   [Index Data]    uint32 × index_count
	//   [Skin Data]     HAS_SKINNING のときのみ
	//   [Submesh Data]  Submesh × submesh_count (index_start + index_count + material_path[256])
	// 頂点は既に LH 座標系・V 反転済み、インデックスも winding 反転済み。
	std::string filePath = directoryPath + "/" + filename;
	auto h = AssetLocator::GetInstance()->Open(filePath);
	if (!h.IsValid()) return;

	char magic[4]{};
	h.Read(magic, 4);
	if (std::memcmp(magic, "MESH", 4) != 0) return;

	uint32_t version = 0, flags = 0, vertexCount = 0, indexCount = 0, submeshCount = 0;
	uint32_t vertexOffset = 0, indexOffset = 0, skinOffset = 0, submeshOffset = 0;
	h.Read(&version, 4);
	h.Read(&flags, 4);
	h.Read(&vertexCount, 4);
	h.Read(&indexCount, 4);
	h.Read(&submeshCount, 4);
	h.Read(&vertexOffset, 4);
	h.Read(&indexOffset, 4);
	h.Read(&skinOffset, 4);
	h.Read(&submeshOffset, 4);

	if (version != 2) return;  // v2 のみサポート

	// skeleton_path は今は使わない（スキニングモデルは別経路）
	h.Seek(h.GetPosition() + 256);

	// 静的 .mesh はノード階層を持たないので rootNode の localMatrix を単位行列にしておく
	modelData_.rootNode.localMatrix = MakeIdentity4x4();

	// GPU フェーズで使うメタ情報を保持
	meshFilePath_     = filePath;
	vertexFileOffset_ = vertexOffset;
	vertexCount_      = vertexCount;
	indexFileOffset_  = indexOffset;
	indexCount_       = indexCount;

	// pack モード + DStorage 利用可能なら CPU 経由をスキップして VRAM 直行
	auto* loc = AssetLocator::GetInstance();
	auto* ds  = DStorageManager::GetInstance();
	useDirectStorage_ = loc->IsPackMode() && ds->IsInitialized() && ds->GetPackFile();

	if (!useDirectStorage_) {
		// 頂点
		modelData_.vertices.resize(vertexCount);
		h.ReadAt(vertexOffset, modelData_.vertices.data(),
		         static_cast<size_t>(vertexCount) * sizeof(VertexData));

		// インデックス
		modelData_.indices.resize(indexCount);
		h.ReadAt(indexOffset, modelData_.indices.data(),
		         static_cast<size_t>(indexCount) * sizeof(uint32_t));
	}

	// submesh[0] のマテリアルパスを取得（マルチマテリアル未対応、最初の一つだけ使う）
	if (submeshCount > 0) {
		h.Seek(submeshOffset);
		uint32_t idxStart = 0, idxCount = 0;
		h.Read(&idxStart, 4);
		h.Read(&idxCount, 4);
		char matPath[256]{};
		h.Read(matPath, 256);

		// .mat を開いて base_color_path を取得
		std::string baseColor = ReadMatBaseColorPath(std::string(matPath));
		modelData_.materialData.textureFilePath = baseColor;
	}
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
