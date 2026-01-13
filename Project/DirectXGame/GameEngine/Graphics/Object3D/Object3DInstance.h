#pragma once
#include"Object3DManager.h"
#include"VertexData.h"
#include <fstream>
#include <sstream>
#include "Material.h"
#include <cassert>
#include"DirectXCore.h"
#include"MathUtility.h"
#include"TransformationMatrix.h"
#include"DirectionalLight.h"
#include"TextureManager.h"
#include"Transform.h"

class Object3DManager;

struct ModelData
{
	std::vector<VertexData>vertices;
	MaterialData materialData;
};

class Object3DInstance{

	//==============================
	// メンバ変数
	//==============================
	Object3DManager* object3DManager_ = nullptr;

	ModelData modelData_;

	// バッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;

	// バッファ内データへのCPU側ポインタ
	VertexData* vertexData_ = nullptr;
	Material* material_ = nullptr;
	TransformationMatrix* transformationMatrixData_ = nullptr;
	DirectionalLight* directionalLightData_ = nullptr;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	Transform transform_;
	Transform cameraTransform_;

	//==============================
	// メンバ関数
	//==============================
	static ModelData LoadObjFile(const std::string& directorPath, const std::string& filename);
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

	// 頂点データ作成
	void CreateVertexData(DirectXCore* dxCore);

	// マテリアルデータ作成
	void CreateMaterialData(DirectXCore* dxCore);

	// 座標変換行列リソース作成
	void CreateTransformationMatrixResource(DirectXCore* dxCore);

	// 平行光源リソース作成
	void CreateDirectionalLight(DirectXCore* dxCore);

public:
	void Initialize(Object3DManager* object3DManager,DirectXCore* dxCore);

	void Update();

	void Draw(DirectXCore*dxCore);

};

