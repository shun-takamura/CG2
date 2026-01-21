#include "Object3DInstance.h"
#include "imgui.h"

void Object3DInstance::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore,
    const std::string& directorPath, const std::string& filename,
    const std::string& name)
{
    object3DManager_ = object3DManager;

    // 名前が指定されていなければファイル名を使用
    if (name.empty()) {
        name_ = filename;
    } else {
        name_ = name;
    }

    camera_ = object3DManager_->GetDefaultCamera();

    // ModelManagerからモデルをロード
    ModelManager::GetInstance()->LoadModel(filename);

    // ロードしたモデルを取得してセット
    modelInstance_ = ModelManager::GetInstance()->FindModel(filename);

    CreateTransformationMatrixResource(dxCore);
    CreateCameraResource(dxCore);

    // Transform変数を作る
    transform_ = {
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };
}

void Object3DInstance::Update()
{
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
    Matrix4x4 worldViewProjectionMatrix;

    if (camera_) {
        const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
        worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);

        // カメラ位置をGPUに送る
        cameraData_->worldPosition = camera_->GetTranslate();
    } else {
        worldViewProjectionMatrix = worldMatrix;
    }

    transformationMatrixData_->World = worldMatrix;
    transformationMatrixData_->WVP = worldViewProjectionMatrix;
    transformationMatrixData_->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
}

void Object3DInstance::Draw(DirectXCore* dxCore)
{
    // 座標変換行列CBufferの場所を設定
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // カメラCBufferの場所を設定
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        4, cameraResource_->GetGPUVirtualAddress()
    );

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
    transformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
}

void Object3DInstance::CreateCameraResource(DirectXCore* dxCore)
{
    // カメラ用リソースを作成
    cameraResource_ = dxCore->CreateBufferResource(sizeof(CameraForGPU));

    // 書き込み用アドレスを取得
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

    // 初期値
    cameraData_->worldPosition = { 0.0f, 0.0f, -10.0f };
    cameraData_->padding = 0.0f;
}

void Object3DInstance::SetModel(const std::string& filePath)
{
    // モデルを検索してセット
    modelInstance_ = ModelManager::GetInstance()->FindModel(filePath);
}

void Object3DInstance::SetTexture(const std::string& filePath)
{
    textureFilePath_ = filePath;
    // TODO: モデルのテクスチャを変更する処理
    // ModelInstanceにテクスチャ変更機能があれば呼び出す
}

//==============================
// ImGui Inspector描画
//==============================
void Object3DInstance::OnImGuiInspector()
{
    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);

        // 回転をDegreeで表示
        Vector3 rotateDegree = {
            transform_.rotate.x * (180.0f / 3.14159265f),
            transform_.rotate.y * (180.0f / 3.14159265f),
            transform_.rotate.z * (180.0f / 3.14159265f)
        };
        if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
            transform_.rotate.x = rotateDegree.x * (3.14159265f / 180.0f);
            transform_.rotate.y = rotateDegree.y * (3.14159265f / 180.0f);
            transform_.rotate.z = rotateDegree.z * (3.14159265f / 180.0f);
        }

        ImGui::DragFloat3("Scale", &transform_.scale.x, 0.01f);
    }

    // Texture（将来の拡張用）
    if (ImGui::CollapsingHeader("Texture")) {
        ImGui::Text("Current: %s", textureFilePath_.empty() ? "(none)" : textureFilePath_.c_str());

        // テクスチャ変更ボタン（将来実装）
        if (ImGui::Button("Change Texture...")) {
            // TODO: テクスチャ選択ダイアログ
        }
    }
}