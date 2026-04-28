#include "AnimatedObject3DInstance.h"
#include "imgui.h"
#include <cmath>
#include "PrimitiveMesh.h"
#include "PrimitiveGenerator.h"
#include "LineRenderer.h"
#include "SRVManager.h"
#include "SkinningObject3DManager.h"
#include "SRVManager.h"
#include "LightManager.h"

AnimatedObject3DInstance::~AnimatedObject3DInstance() = default;

void AnimatedObject3DInstance::Initialize(Object3DManager* object3DManager,
    SkinningObject3DManager* skinningObject3DManager,
    DirectXCore* dxCore,
    SRVManager* srvManager,
    AnimatedModelInstance* animatedModel,
    const std::string& name)
{
    object3DManager_ = object3DManager;
    skinningObject3DManager_ = skinningObject3DManager;
    srvManager_ = srvManager;
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

    // Skeleton構築
    if (animatedModelInstance_) {
        skeleton_ = CreateSkeleton(animatedModelInstance_->GetModelData().rootNode);
        hasSkeleton_ = true;

        // SkinCluster構築
        skinCluster_ = CreateSkinCluster(
            dxCore,
            srvManager,
            skeleton_,
            animatedModelInstance_->GetModelData());
        hasSkinCluster_ = true;
    }

#ifdef USE_IMGUI
    // デバッグ描画用の球メッシュをJoint数分準備
    if (hasSkeleton_) {
        debugSpheres_.resize(skeleton_.joints.size());
        for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
            debugSpheres_[i] = std::make_unique<PrimitiveMesh>();
            MeshData sphereMesh = PrimitiveGenerator::CreateSphere(jointSphereRadius_, 8);
            debugSpheres_[i]->Initialize(sphereMesh);
            debugSpheres_[i]->SetBlendMode(PrimitivePipeline::kBlendModeNone);
            debugSpheres_[i]->SetDepthWrite(true);
            debugSpheres_[i]->SetTexture("DistributionAssets/Textures/white1x1.png");
        }
    }
#endif

}

void AnimatedObject3DInstance::Update(float deltaTime)
{
    // アニメーション再生時刻の更新
    if (animatedModelInstance_ && isPlaying_) {
        animationTime_ += deltaTime * playbackSpeed_;
        const Animation& animation = animatedModelInstance_->GetAnimation();
        if (isLoop_) {
            animationTime_ = std::fmod(animationTime_, animation.duration);
            if (animationTime_ < 0.0f) {
                animationTime_ += animation.duration;
            }
        } else {
            if (animationTime_ >= animation.duration) {
                animationTime_ = animation.duration;
                isPlaying_ = false;
            }
        }
    }

    // SkeletonにAnimationを適用しSkeleton全体を更新
    if (animatedModelInstance_ && hasSkeleton_) {
        ApplyAnimation(skeleton_, animatedModelInstance_->GetAnimation(), animationTime_);
        UpdateSkeleton(skeleton_);

        // SkinClusterの更新
        if (hasSkinCluster_) {
            UpdateSkinCluster(skinCluster_, skeleton_);
        }
    }

    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);

    // Skinning用Boneがないモデルの場合、rootJointのskeletonSpaceMatrixを掛ける
    // ノードアニメーションを反映するため
    if (hasSkeleton_ && !skeleton_.joints.empty()) {
        // 先頭頂点のweightが0 = Skinningなしモデルと判定
        bool isRigidAnimation = false;
        if (hasSkinCluster_ && !skinCluster_.mappedInfluence.empty()) {
            const auto& inf = skinCluster_.mappedInfluence[0];
            float totalWeight = inf.weights[0] + inf.weights[1] + inf.weights[2] + inf.weights[3];
            isRigidAnimation = (totalWeight < 0.001f);
        }

        if (isRigidAnimation) {
            // Skinningなし：rootJointのskeletonSpaceMatrixをworldMatrixに掛ける
            const Matrix4x4& rootMatrix = skeleton_.joints[skeleton_.root].skeletonSpaceMatrix;
            worldMatrix = Multiply(rootMatrix, worldMatrix);
        }
    }

    Matrix4x4 worldViewProjectionMatrix;
    if (camera_) {
        const Matrix4x4& viewProjectionMatrix = camera_->GetViewProjectionMatrix();
        worldViewProjectionMatrix = Multiply(worldMatrix, viewProjectionMatrix);
        cameraData_->worldPosition = camera_->GetTranslate();
    } else {
        worldViewProjectionMatrix = worldMatrix;
    }

    transformationMatrixData_->World = worldMatrix;
    transformationMatrixData_->WVP = worldViewProjectionMatrix;
    transformationMatrixData_->WorldInverseTranspose = Transpose(Inverse(worldMatrix));
}

void AnimatedObject3DInstance::Draw(DirectXCore* dxCore)
{
    // Skinning用RootSignatureを再設定
    if (skinningObject3DManager_) {
        dxCore->GetCommandList()->SetGraphicsRootSignature(
            skinningObject3DManager_->GetRootSignature()
        );
    }

    // PSO切り替え
    if (skinningObject3DManager_ && animatedModelInstance_) {
        Material* mat = animatedModelInstance_->GetMaterialPointer();
        if (mat && mat->useEnvironmentMap) {
            dxCore->GetCommandList()->SetPipelineState(
                skinningObject3DManager_->GetPipelineState(SkinningObject3DManager::kShaderEnvironmentMap)
            );
        } else {
            dxCore->GetCommandList()->SetPipelineState(
                skinningObject3DManager_->GetPipelineState(SkinningObject3DManager::kShaderNoEnvironmentMap)
            );
        }
    }

    // RootParam[1] TransformationMatrix
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // RootParam[4] Camera
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        4, cameraResource_->GetGPUVirtualAddress()
    );

    // ★ライトをバインド
    LightManager::GetInstance()->BindLights(dxCore->GetCommandList());

    // 環境マップは必ずバインドする（PSのvalidationが要求するため）
    // useEnvironmentMap=falseの場合でもサンプルコードはコンパイルされており、
    // GPU-Based Validationは未バインドを許可しない
    if (skinningObject3DManager_) {
        const std::string& envPath = skinningObject3DManager_->GetEnvironmentTexturePath();
        if (!envPath.empty()) {
            dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
                7, TextureManager::GetInstance()->GetSrvHandleGPU(envPath)
            );
        }
    }

    // Skinning版Draw
    if (animatedModelInstance_ && hasSkinCluster_) {
        animatedModelInstance_->DrawSkinning(dxCore, skinCluster_, srvManager_);
    }
}

#ifdef USE_IMGUI
void AnimatedObject3DInstance::DrawSkeletonDebug(DirectXCore* dxCore)
{
    if (!showSkeleton_ || !hasSkeleton_ || !camera_) return;

    // Skinning時はメッシュのworldMatrix = transform_由来のworldMatrix なので
    // Joint位置も skeletonSpaceMatrix * worldMatrix で求める
    Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);

    // 各Joint位置に球を描画（Joint数分のメッシュを使う）
    for (size_t i = 0; i < skeleton_.joints.size(); ++i) {
        const Joint& joint = skeleton_.joints[i];
        Matrix4x4 jointWorld = Multiply(joint.skeletonSpaceMatrix, worldMatrix);
        Vector3 jointPos = { jointWorld.m[3][0], jointWorld.m[3][1], jointWorld.m[3][2] };

        debugSpheres_[i]->GetTransform().translate = jointPos;
        debugSpheres_[i]->GetTransform().rotate = { 0.0f, 0.0f, 0.0f };
        debugSpheres_[i]->GetTransform().scale = transform_.scale;
        debugSpheres_[i]->SetColor(jointSphereColor_);
        debugSpheres_[i]->Update(camera_);
        debugSpheres_[i]->Draw();
    }

    // 親子間に線を引く
    for (const Joint& joint : skeleton_.joints) {
        if (joint.parent.has_value()) {
            const Joint& parent = skeleton_.joints[*joint.parent];

            Matrix4x4 jWorld = Multiply(joint.skeletonSpaceMatrix, worldMatrix);
            Matrix4x4 pWorld = Multiply(parent.skeletonSpaceMatrix, worldMatrix);

            Vector3 jPos = { jWorld.m[3][0], jWorld.m[3][1], jWorld.m[3][2] };
            Vector3 pPos = { pWorld.m[3][0], pWorld.m[3][1], pWorld.m[3][2] };
            LineRenderer::GetInstance()->AddLine(pPos, jPos, jointLineColor_);
        }
    }
}
#endif

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

    // Animation制御
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

    // Skeleton Debug
    if (ImGui::CollapsingHeader("Skeleton Debug")) {
        ImGui::Checkbox("Show Skeleton", &showSkeleton_);
        ImGui::ColorEdit4("Joint Sphere Color", &jointSphereColor_.x);
        ImGui::ColorEdit4("Joint Line Color", &jointLineColor_.x);
        if (hasSkeleton_) {
            ImGui::Text("Joint Count: %zu", skeleton_.joints.size());

            // Rootの位置とサンプルJointの位置を表示
            if (!skeleton_.joints.empty()) {
                Matrix4x4 worldMatrix = MakeAffineMatrix(transform_);
                const Joint& root = skeleton_.joints[skeleton_.root];
                Matrix4x4 rootWorld = Multiply(root.skeletonSpaceMatrix, worldMatrix);
                ImGui::Text("Root: (%.2f, %.2f, %.2f)",
                    rootWorld.m[3][0], rootWorld.m[3][1], rootWorld.m[3][2]);

                // 末端Joint
                size_t lastIdx = skeleton_.joints.size() - 1;
                Matrix4x4 lastWorld = Multiply(
                    skeleton_.joints[lastIdx].skeletonSpaceMatrix, worldMatrix);
                ImGui::Text("Joint[%zu] '%s': (%.2f, %.2f, %.2f)",
                    lastIdx, skeleton_.joints[lastIdx].name.c_str(),
                    lastWorld.m[3][0], lastWorld.m[3][1], lastWorld.m[3][2]);
            }

            // SkinCluster情報（フェーズA確認用）
            if (hasSkinCluster_) {
                ImGui::Separator();
                ImGui::Text("InverseBindPose Count: %zu",
                    skinCluster_.inverseBindPoseMatrices.size());
                ImGui::Text("Influence Count: %zu",
                    skinCluster_.mappedInfluence.size());

                // 先頭頂点のInfluenceを表示
                if (!skinCluster_.mappedInfluence.empty()) {
                    auto& inf = skinCluster_.mappedInfluence[0];
                    ImGui::Text("Vertex[0] weights: %.2f, %.2f, %.2f, %.2f",
                        inf.weights[0], inf.weights[1], inf.weights[2], inf.weights[3]);
                    ImGui::Text("Vertex[0] jointIndices: %d, %d, %d, %d",
                        inf.jointIndices[0], inf.jointIndices[1],
                        inf.jointIndices[2], inf.jointIndices[3]);
                }
            }
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