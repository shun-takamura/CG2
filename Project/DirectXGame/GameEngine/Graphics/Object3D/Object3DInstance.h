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
#include"ModelCore.h"
#include"ModelInstance.h"
#include"ModelManager.h"
#include"Camera.h"
#include"CameraForGPU.h"

class Object3DManager;

class Object3DInstance{

	//==============================
	// メンバ変数
	//==============================
	Object3DManager* object3DManager_ = nullptr;
	ModelInstance* modelInstance_ = nullptr;
	Camera* camera_ = nullptr;

	// バッファリソース
	Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> directionalLightResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;

	// バッファ内データへのCPU側ポインタ
	TransformationMatrix* transformationMatrixData_ = nullptr;
	DirectionalLight* directionalLightData_ = nullptr;
	CameraForGPU* cameraData_ = nullptr;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	Transform transform_;
	Transform cameraTransform_;

	//==============================
	// メンバ関数
	//==============================
	
	// 座標変換行列リソース作成
	void CreateTransformationMatrixResource(DirectXCore* dxCore);

	// 平行光源リソース作成
	void CreateDirectionalLight(DirectXCore* dxCore);

	void CreateCameraResource(DirectXCore* dxCore);

public:

	// セッター
	void SetModel(ModelInstance* modelInstance) { modelInstance_ = modelInstance; }
	void SetModel(const std::string& filePath);
	void SetCamera(Camera* camera) { camera_ = camera; }
	void SetScale(const Vector3& scele) { transform_.scale = scele; }
	void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
	void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

	// ゲッター
	const Vector3& GetScale()const { return transform_.scale; }
	const Vector3& GetRotate()const { return transform_.rotate; }
	const Vector3& GetTranslate()const { return transform_.translate; }

	void Initialize(Object3DManager* object3DManager,DirectXCore* dxCore, const std::string& directorPath, const std::string& filename);

	void Update();

	void Draw(DirectXCore*dxCore);

};

