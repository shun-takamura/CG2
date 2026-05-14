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
#include "QuaternionTransform.h"
#include <map>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

struct Node{
	QuaternionTransform transform;
	Matrix4x4 localMatrix;
	std::string name;
	std::vector<Node>children;
};

// Skinning用のデータ構造
struct VertexWeightData {
	float weight;
	uint32_t vertexIndex;
};

struct JointWeightData {
	Matrix4x4 inverseBindPoseMatrix;
	std::vector<VertexWeightData> vertexWeights;
};

struct ModelData
{
	std::vector<VertexData>vertices;
	std::vector<uint32_t> indices;
	MaterialData materialData;
	Node rootNode;
	std::map<std::string, JointWeightData> skinClusterData;
};

class ModelInstance
{
	//==============================
	// ロード状態
	//==============================
	enum class LoadState { Unloaded, CPUReady, GPUReady };
	LoadState loadState_ = LoadState::Unloaded;

	//==============================
	// メンバ変数
	//==============================
	std::string textureFilePath_;  // テクスチャファイルパスを保持

	ModelData modelData_;

	ModelCore* modelCore_;

	// VertexData は CPU 側 modelData_.vertices に保持。GPU バッファは DEFAULT heap で書き換え不可
	Material* material_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	//==============================
	// メンバ関数
	//==============================
public:
	// 既存の同期ロード（CPU + GPU両方）
	void Initialize(ModelCore* modelManager, const std::string& directorPath, const std::string& filename);

	// CPU フェーズのみ（Assimp パース）スレッド安全
	void LoadCPU(const std::string& directoryPath, const std::string& filename);

	// GPU フェーズのみ（リソース作成）メインスレッドのみ
	void InitializeGPU(ModelCore* modelCore, DirectXCore* dxCore);

	void Draw(DirectXCore* dxCore);

	const ModelData& GetModelData() const { return modelData_; }

	// ImGui/PSO切り替えから Material にアクセスするためのGetter
	Material* GetMaterialPointer() const { return material_; }

	// Inspector からテクスチャを差し替える際に使う（GPU リソース作成は呼び出し側で行う）
	void SetTextureFilePath(const std::string& filePath) {
		textureFilePath_ = filePath;
		modelData_.materialData.textureFilePath = filePath;
	}
	const std::string& GetTextureFilePath() const { return textureFilePath_; }

	// ロード状態の確認
	bool IsCPUReady() const { return loadState_ >= LoadState::CPUReady; }
	bool IsGPUReady() const { return loadState_ == LoadState::GPUReady; }

private:

	// 頂点データ作成
	void CreateVertexData(DirectXCore* dxCore);

	// マテリアルデータ作成
	void CreateMaterialData(DirectXCore* dxCore);

	// indexを作成
	void CreateIndexData(DirectXCore* dxCore);

	Node ReadNode(aiNode* node);

	//static ModelData LoadObjFile(const std::string& directorPath, const std::string& filename);
	void LoadModel(const std::string& directoryPath, const std::string& filename);

	// .mesh バイナリ直読み（Cooker が生成したもの）
	void LoadMeshBinary(const std::string& directoryPath, const std::string& filename);

	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

};

