#include "AnimatedObject3DInstance.h"
#include "imgui.h"
#include <cmath>

void AnimatedObject3DInstance::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore,
    AnimatedModelInstance* animatedModel,
    const std::string& name)
{
    object3DManager_ = object3DManager;
    animatedModelInstance_ = animatedModel;

    if (name.empty()) {
        name_ = "AnimatedObject";
    } else {
        name_ = name;
    }

    camera_ = object3DManager_->GetDefaultCamera();

    CreateTransformationMatrixResource(dxCore);
    CreateCameraResource(dxCore);

    transform_ = {
        {1.0f, 1.0f, 1.0f},
        {0.0f, 0.0f, 0.0f},
        {0.0f, 0.0f, 0.0f}
    };
}

void AnimatedObject3DInstance::Update(float deltaTime)
{
    // ★アニメーション再生処理
    Matrix4x4 animationLocalMatrix = MakeIdentity4x4();

    if (animatedModelInstance_ && isPlaying_) {
        // 時刻を進める
        animationTime_ += deltaTime * playbackSpeed_;

        // ループ処理
        const Animation& animation = animatedModelInstance_->GetAnimation();
        if (isLoop_) {
            animationTime_ = std::fmod(animationTime_, animation.duration);
            if (animationTime_ < 0.0f) {
                animationTime_ += animation.duration;
            }
        } else {
            // ループしない場合は最後の時刻でストップ
            if (animationTime_ >= animation.duration) {
                animationTime_ = animation.duration;
                isPlaying_ = false;
            }
        }
    }

    // ★rootNodeのアニメーションからlocalMatrixを計算
    if (animatedModelInstance_) {
        const Animation& animation = animatedModelInstance_->GetAnimation();
        const std::string& rootNodeName = animatedModelInstance_->GetModelData().rootNode.name;

        // rootNodeに紐づくアニメーションがあるかチェック
        auto it = animation.nodeAnimations.find(rootNodeName);
        if (it != animation.nodeAnimations.end()) {
            const NodeAnimation& rootNodeAnim = it->second;
            Vector3 translate = CalculateValue(rootNodeAnim.translate.keyframes, animationTime_);
            Quaternion rotate = CalculateValue(rootNodeAnim.rotate.keyframes, animationTime_);
            Vector3 scale = CalculateValue(rootNodeAnim.scale.keyframes, animationTime_);
            animationLocalMatrix = MakeAffineMatrix(scale, rotate, translate);
        } else {
            // アニメーションがないNodeはモデルが持つlocalMatrixを使う
            animationLocalMatrix = animatedModelInstance_->GetModelData().rootNode.localMatrix;
        }
    }

    // ワールド行列の計算
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
    Matrix4x4 worldViewProjectionMatrix;

    if (camera_) {
        const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
        // localMatrix * worldMatrix * VP の順
        worldViewProjectionMatrix = Multiply(animationLocalMatrix, Multiply(worldMatrix, viewProjectionMatrix));
        cameraData_->worldPosition = camera_->GetTranslate();
    } else {
        worldViewProjectionMatrix = Multiply(animationLocalMatrix, worldMatrix);
    }

    transformationMatrixData_->World = Multiply(animationLocalMatrix, worldMatrix);
    transformationMatrixData_->WVP = worldViewProjectionMatrix;
    transformationMatrixData_->WorldInverseTranspose = Transpose(Inverse(transformationMatrixData_->World));
}

void AnimatedObject3DInstance::Draw(DirectXCore* dxCore)
{
    // PSO切り替え
    if (animatedModelInstance_) {
        Material* mat = animatedModelInstance_->GetMaterialPointer();
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

    // 座標変換行列CBuffer
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // カメラCBuffer
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        4, cameraResource_->GetGPUVirtualAddress()
    );

    // 描画
    if (animatedModelInstance_) {
        animatedModelInstance_->Draw(dxCore);
    }
}

void AnimatedObject3DInstance::CreateTransformationMatrixResource(DirectXCore* dxCore)
{
    UINT transformationMatrixSize = (sizeof(TransformationMatrix) + 255) & ~255;
    transformationMatrixResource_ = dxCore->CreateBufferResource(transformationMatrixSize);
    transformationMatrixResource_->Map(0, nullptr, reinterpret_cast<void**>(&transformationMatrixData_));

    transformationMatrixData_->WVP = MakeIdentity4x4();
    transformationMatrixData_->World = MakeIdentity4x4();
    transformationMatrixData_->WorldInverseTranspose = MakeIdentity4x4();
}

void AnimatedObject3DInstance::CreateCameraResource(DirectXCore* dxCore)
{
    cameraResource_ = dxCore->CreateBufferResource(sizeof(CameraForGPU));
    cameraResource_->Map(0, nullptr, reinterpret_cast<void**>(&cameraData_));

    cameraData_->worldPosition = { 0.0f, 0.0f, -10.0f };
    cameraData_->padding = 0.0f;
}

void AnimatedObject3DInstance::SetUseEnvironmentMap(bool use)
{
    if (!animatedModelInstance_) return;
    Material* mat = animatedModelInstance_->GetMaterialPointer();
    if (!mat) return;
    mat->useEnvironmentMap = use ? 1 : 0;
}

void AnimatedObject3DInstance::SetEnvironmentCoefficient(float coef)
{
    if (!animatedModelInstance_) return;
    Material* mat = animatedModelInstance_->GetMaterialPointer();
    if (!mat) return;
    mat->environmentCoefficient = coef;
}

//==============================
// ImGui Inspector
//==============================
void AnimatedObject3DInstance::OnImGuiInspector()
{
#ifdef USE_IMGUI

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", &transform_.translate.x, 0.1f);

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

    // ★Animation制御
    if (ImGui::CollapsingHeader("Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (animatedModelInstance_) {
            const Animation& animation = animatedModelInstance_->GetAnimation();

            // 再生/一時停止ボタン
            if (isPlaying_) {
                if (ImGui::Button("Pause")) {
                    Pause();
                }
            } else {
                if (ImGui::Button("Play")) {
                    Play();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Stop")) {
                Stop();
            }

            // 時刻シーク
            ImGui::SliderFloat("Time", &animationTime_, 0.0f, animation.duration);
            ImGui::Text("Duration: %.2f s", animation.duration);

            // ループ
            ImGui::Checkbox("Loop", &isLoop_);

            // 再生速度
            ImGui::DragFloat("Speed", &playbackSpeed_, 0.1f, -5.0f, 5.0f);
        } else {
            ImGui::Text("No animated model loaded");
        }
    }

    // Material
    if (ImGui::CollapsingHeader("Material")) {
        if (animatedModelInstance_) {
            Material* mat = animatedModelInstance_->GetMaterialPointer();
            if (mat) {
                bool useEnv = (mat->useEnvironmentMap != 0);
                if (ImGui::Checkbox("Use Environment Map", &useEnv)) {
                    mat->useEnvironmentMap = useEnv ? 1 : 0;
                }
                if (useEnv) {
                    ImGui::SliderFloat("Environment Coefficient",
                        &mat->environmentCoefficient, 0.0f, 1.0f);
                }
                ImGui::DragFloat("Shininess", &mat->shininess, 1.0f, 0.0f, 1000.0f);
            }
        }
    }

#endif
}