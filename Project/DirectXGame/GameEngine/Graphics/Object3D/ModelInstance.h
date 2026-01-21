#pragma once
#include"DirectXCore.h"
#include <wrl.h>
#include"ModelCore.h"
#include"VertexData.h"
#include "Material.h"
#include <fstream>
#include <cassert>
#include"TextureManager.h"
#include"MathUtility.h"

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

struct Node{
	Matrix4x4 localMatrix;
	std::string name;
	std::vector<Node>children;
};

struct ModelData
{
	std::vector<VertexData>vertices;
	MaterialData materialData;
	Node rootNode;
};

class ModelInstance
{
	//==============================
	// メンバ変数
	//==============================
	std::string textureFilePath_;  // テクスチャファイルパスを保持

	ModelData modelData_;

	ModelCore* modelCore_;

	VertexData* vertexData_ = nullptr;
	Material* material_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	//==============================
	// メンバ関数
	//==============================
public:
	void Initialize(ModelCore* modelManager, const std::string& directorPath, const std::string& filename);

	void Draw(DirectXCore* dxCore);

	const ModelData& GetModelData() const { return modelData_; }

private:

	// 頂点データ作成
	void CreateVertexData(DirectXCore* dxCore);

	// マテリアルデータ作成
	void CreateMaterialData(DirectXCore* dxCore);

	Node ReadNode(aiNode* node);

	static ModelData LoadObjFile(const std::string& directorPath, const std::string& filename);
	void LoadModel(const std::string& directoryPath, const std::string& filename);
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

};

