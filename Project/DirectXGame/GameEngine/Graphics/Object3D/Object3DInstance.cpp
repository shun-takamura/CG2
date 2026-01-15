#include "Object3DInstance.h"

void Object3DInstance::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const std::string& directorPath, const std::string& filename)
{
	object3DManager_ = object3DManager;

	camera_ = object3DManager_->GetDefaultCamera();

	// ModelManagerからモデルをロード
	ModelManager::GetInstance()->LoadModel(filename);

	// ロードしたモデルを取得してセット
	modelInstance_ = ModelManager::GetInstance()->FindModel(filename);

	CreateTransformationMatrixResource(dxCore);
	CreateDirectionalLight(dxCore);

	// Transform変数を作る
	transform_ = {
		{1.0f,1.0f,1.0f},
		{0.0f,0.0f,0.0f},
		{0.0f,0.0f,0.0f}
	};

}

void Object3DInstance::Update()
{
	Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
	Matrix4x4 worldViewProjectionMatrix;

	if (camera_) {
		const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
		worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
	} else{
		worldViewProjectionMatrix = worldMatrix;
	}

	transformationMatrixData_->World = worldMatrix;
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
}

void Object3DInstance::Draw(DirectXCore* dxCore)
{
	// 座標変換行列CBufferの場所を設定
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
		1,transformationMatrixResource_->GetGPUVirtualAddress()
	);

	// rootParameter[3]のところにライトのリソースを入れる。CPUに送る
	dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

	// 3Dモデルがあれば描画
	if (modelInstance_) {
		modelInstance_->Draw(dxCore);
	}

}

void Object3DInstance::CreateTransformationMatrixResource(DirectXCore* dxCore)
{
	// サイズを設定
	UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;

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

void Object3DInstance::SetModel(const std::string& filePath)
{
	// モデルを検索してセット
	modelInstance_ = ModelManager::GetInstance()->FindModel(filePath);
}
