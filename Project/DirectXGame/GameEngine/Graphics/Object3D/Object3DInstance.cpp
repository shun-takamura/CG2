#include "Object3DInstance.h"
#include "imgui.h"
#include "ModelInstance.h"
#include "Material.h"
#include "TextureManager.h"
#include "SceneEditorWindow.h"  // MATERIAL_DROP / MODEL_DROP 等ゲーム側ペイロード（SPRITE は transitive）

void Object3DInstance::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore,
    const std::string& directorPath, const std::string& filename,
    const std::string& name)
{
    object3DManager_ = object3DManager;
    modelFileName_ = filename;
    directoryPath_ = directorPath;

    // 名前が指定されていなければファイル名を使用
    if (name.empty()) {
        name_ = filename;
    } else {
        name_ = name;
    }

    camera_ = object3DManager_->GetDefaultCamera();

    // ModelManagerからモデルをロード
    ModelManager::GetInstance()->LoadModel(directorPath,filename);

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

        // RootNodeのlocalMatrixを適用
        Matrix4x4 localMatrix = modelInstance_->GetModelData().rootNode.localMatrix;
        worldViewProjectionMatrix = Multiply(localMatrix, Multiply(worldMatrix, viewProjectionMatrix));

        // カメラ位置をGPUに送る
        cameraData_->worldPosition = camera_->GetTranslate();
    } else {
        worldViewProjectionMatrix = worldMatrix;
    }

    Matrix4x4 localMatrix = modelInstance_->GetModelData().rootNode.localMatrix;
    transformationMatrixData_->World = Multiply(localMatrix, worldMatrix);
    transformationMatrixData_->WVP = worldViewProjectionMatrix;
    transformationMatrixData_->WorldInverseTranspose = Transpose(Inverse(transformationMatrixData_->World));

}

void Object3DInstance::Draw(DirectXCore* dxCore)
{
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
  // Materialのフラグに応じてPSOを切り替え
    if (modelInstance_) {
        Material* mat = modelInstance_->GetMaterialPointer();
        if (mat && mat->useEnvironmentMap) {
            dxCore->GetCommandList()->SetPipelineState(
                object3DManager_->GetPipelineState(Object3DManager::kShaderEnvironmentMap)
            );
        } else {
            dxCore->GetCommandList()->SetPipelineState(
                object3DManager_->GetPipelineState(Object3DManager::kShaderNoEnvironmentMap)
            );
        }
    }

    // 座標変換行列CBufferの場所を設定
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // カメラCBufferの場所を設定
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        4, cameraResource_->GetGPUVirtualAddress()
    );

    if (modelInstance_) {
        modelInstance_->Draw(dxCore);
    }
}

void Object3DInstance::DrawIdPass(DirectXCore* dxCore)
{
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
    if (!modelInstance_) return;

    auto* cmd = dxCore->GetCommandList();
    cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    cmd->SetGraphicsRootSignature(object3DManager_->GetIdRootSignature());
    cmd->SetPipelineState(object3DManager_->GetIdPipelineState());

    // VS CBV b0 = TransformationMatrix
    cmd->SetGraphicsRootConstantBufferView(0, transformationMatrixResource_->GetGPUVirtualAddress());

    // PS Root Constant b0 = objectId
    const UINT idValue = static_cast<UINT>(objectId_);
    cmd->SetGraphicsRoot32BitConstant(1, idValue, 0);

    modelInstance_->DrawIdPass(dxCore);
}

void Object3DInstance::DrawShadowPass(DirectXCore* dxCore)
{
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
    if (!modelInstance_) return;

    // VS CBV b0 = TransformationMatrix（シャドウVSは .World を使う）
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, transformationMatrixResource_->GetGPUVirtualAddress());

    modelInstance_->DrawShadowPass(dxCore);
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

void Object3DInstance::SetUseEnvironmentMap(bool use)
{
    // ModelInstanceがまだ生成されていない場合（Initialize前）は何もしない
    if (!modelInstance_) {
        return;
    }

    Material* mat = modelInstance_->GetMaterialPointer();
    if (!mat) {
        return;
    }

    // int32_t に変換して書き込む
    mat->useEnvironmentMap = use ? 1 : 0;
}

void Object3DInstance::SetEnvironmentCoefficient(float coef)
{
    if (!modelInstance_) {
        return;
    }

    Material* mat = modelInstance_->GetMaterialPointer();
    if (!mat) {
        return;
    }

    mat->environmentCoefficient = coef;
}

void Object3DInstance::SetMaterialColor(const Vector4& color)
{
    if (!modelInstance_) return;
    Material* mat = modelInstance_->GetMaterialPointer();
    if (!mat) return;
    mat->color = color;
}

Vector4 Object3DInstance::GetMaterialColor() const
{
    if (!modelInstance_) return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    Material* mat = modelInstance_->GetMaterialPointer();
    if (!mat) return Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    return mat->color;
}

//==============================
// ImGui Inspector描画
//==============================
void Object3DInstance::OnImGuiInspector()
{
#ifdef USE_IMGUI

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);

        // 回転をDegreeで表示（内部はラジアン保存）
        Vector3 rotateDegree = RadToDeg(transform_.rotate);
        if (ImGui::DragFloat3("Rotation", &rotateDegree.x, 1.0f)) {
            transform_.rotate = DegToRad(rotateDegree);
        }

        ImGui::DragFloat3("Scale", &transform_.scale.x, 0.01f);
    }

    // Material
    if (ImGui::CollapsingHeader("Material")) {
        if (modelInstance_) {
            Material* mat = modelInstance_->GetMaterialPointer();
            if (mat) {
                // .mat ドロップ受付
                ImGui::Button("Drop .mat here to apply", ImVec2(-FLT_MIN, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(MATERIAL_DROP_PAYLOAD_TYPE)) {
                        const auto* p = static_cast<const MaterialDropPayload*>(payload->Data);
                        MaterialData data;
                        if (LoadMatFile(p->materialPath, data, mat)) {
                            textureFilePath_ = data.textureFilePath;
                            if (!textureFilePath_.empty()) {
                                TextureManager::GetInstance()->LoadTexture(textureFilePath_);
                                modelInstance_->SetTextureFilePath(textureFilePath_);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                // 環境マップ使用のON/OFF
                bool useEnv = (mat->useEnvironmentMap != 0);
                if (ImGui::Checkbox("Use Environment Map", &useEnv)) {
                    mat->useEnvironmentMap = useEnv ? 1 : 0;
                }

                // 係数スライダー（環境マップONの時だけ表示）
                if (useEnv) {
                    ImGui::SliderFloat("Environment Coefficient",
                        &mat->environmentCoefficient,
                        0.0f, 1.0f);
                }

                // Shininess
                ImGui::DragFloat("Shininess", &mat->shininess, 1.0f, 0.0f, 1000.0f);
            }
        }
    }

    // Texture
    if (ImGui::CollapsingHeader("Texture")) {
        ImGui::Text("Current: %s", textureFilePath_.empty() ? "(none)" : textureFilePath_.c_str());

        // .dds ドロップ受付（SceneEditor の Sprites セクションからドラッグ）
        ImGui::Button("Drop .dds here to apply", ImVec2(-FLT_MIN, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                const auto* p = static_cast<const SpriteDropPayload*>(payload->Data);
                textureFilePath_ = p->texturePath;
                TextureManager::GetInstance()->LoadTexture(textureFilePath_);
                if (modelInstance_) modelInstance_->SetTextureFilePath(textureFilePath_);
            }
            ImGui::EndDragDropTarget();
        }
    }

#endif // USE_IMGUI
}