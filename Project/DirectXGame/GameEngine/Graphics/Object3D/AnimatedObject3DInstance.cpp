#include "AnimatedObject3DInstance.h"
#include "imgui.h"
#include <cmath>
#include "PrimitiveMesh.h"
#include "PrimitiveGenerator.h"
#include "LineRenderer.h"
#include "SRVManager.h"
#include "SkinningComputeManager.h"
#include "LightManager.h"
#include "Object3DManager.h"
#include "AnimatedModelInstance.h"
#include "Material.h"
#include "TextureManager.h"
#include "SceneEditorWindow.h"  // ドロップペイロード定義

AnimatedObject3DInstance::~AnimatedObject3DInstance() = default;

void AnimatedObject3DInstance::Initialize(Object3DManager* object3DManager,
    SkinningComputeManager* skinningComputeManager,
    DirectXCore* dxCore,
    SRVManager* srvManager,
    AnimatedModelInstance* animatedModel,
    const std::string& name)
{
    object3DManager_ = object3DManager;
    skinningComputeManager_ = skinningComputeManager;
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
            animatedModelInstance_);
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
            debugSpheres_[i]->SetTexture("Resources/Textures/white1x1.dds");
        }
    }
#endif

}

void AnimatedObject3DInstance::PlayAnimation(const std::string& animPath, float fadeTime)
{
    if (!animatedModelInstance_) return;
    if (animPath.empty()) return;

    if (fadeTime > 0.0f) {
        // 現在のアニメを snapshot して旧クリップ側に。フェード中の再ドロップは prev を上書き
        prevAnimation_ = animatedModelInstance_->GetAnimation();
        prevAnimationTime_ = animationTime_;
        hasPrevAnimation_ = (prevAnimation_.duration > 0.0f);
        fadeTimer_ = 0.0f;
        fadeDuration_ = fadeTime;
    } else {
        hasPrevAnimation_ = false;
        prevAnimation_ = Animation{};
        fadeTimer_ = 0.0f;
        fadeDuration_ = 0.0f;
    }

    animatedModelInstance_->ReplaceAnimation(animPath);
    animationTime_ = 0.0f;
    isPlaying_ = true;
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

    // フェード中は旧アニメーションの時刻も進める（自然に動き続けて見せる）
    if (hasPrevAnimation_) {
        prevAnimationTime_ += deltaTime * playbackSpeed_;
        if (prevAnimation_.duration > 0.0f) {
            prevAnimationTime_ = std::fmod(prevAnimationTime_, prevAnimation_.duration);
            if (prevAnimationTime_ < 0.0f) {
                prevAnimationTime_ += prevAnimation_.duration;
            }
        }
        fadeTimer_ += deltaTime;
        if (fadeTimer_ >= fadeDuration_) {
            // フェード完了 → 旧アニメ破棄
            hasPrevAnimation_ = false;
            prevAnimation_ = Animation{};
            fadeTimer_ = 0.0f;
            fadeDuration_ = 0.0f;
        }
    }

    // SkeletonにAnimationを適用しSkeleton全体を更新
    if (animatedModelInstance_ && hasSkeleton_) {
        if (hasPrevAnimation_ && fadeDuration_ > 0.0f) {
            const float w = fadeTimer_ / fadeDuration_;
            ApplyAnimationBlended(skeleton_,
                prevAnimation_, prevAnimationTime_,
                animatedModelInstance_->GetAnimation(), animationTime_,
                w);
        } else {
            ApplyAnimation(skeleton_, animatedModelInstance_->GetAnimation(), animationTime_);
        }
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
        } else if (animatedModelInstance_ && animatedModelInstance_->UseDirectStorage()) {
            // DStorage 経路では mappedInfluence が CPU 不可視。.mesh ヘッダのフラグで判定。
            isRigidAnimation = !animatedModelInstance_->HasSkinning();
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

void AnimatedObject3DInstance::DispatchSkinning(DirectXCore* dxCore)
{
    if (!skinningComputeManager_ || !hasSkinCluster_ || !animatedModelInstance_) {
        return;
    }

    auto* commandList = dxCore->GetCommandList();

    // skinnedVertexResourceを書き込み可能なStateへ遷移
    if (skinCluster_.skinnedVertexState != D3D12_RESOURCE_STATE_UNORDERED_ACCESS) {
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
        barrier.Transition.pResource = skinCluster_.skinnedVertexResource.Get();
        barrier.Transition.StateBefore = skinCluster_.skinnedVertexState;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        commandList->ResourceBarrier(1, &barrier);
        skinCluster_.skinnedVertexState = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    }

    // Compute用のRootSignature/PSOを設定
    skinningComputeManager_->DispatchSetting();

    // RootParam設定
    commandList->SetComputeRootDescriptorTable(
        0, srvManager_->GetGPUDescriptorHandle(skinCluster_.paletteSrvIndex));
    commandList->SetComputeRootDescriptorTable(
        1, srvManager_->GetGPUDescriptorHandle(skinCluster_.inputVertexSrvIndex));
    commandList->SetComputeRootDescriptorTable(
        2, srvManager_->GetGPUDescriptorHandle(skinCluster_.influenceSrvIndex));
    commandList->SetComputeRootDescriptorTable(
        3, srvManager_->GetGPUDescriptorHandle(skinCluster_.skinnedVertexUavIndex));
    commandList->SetComputeRootConstantBufferView(
        4, skinCluster_.skinningInformationResource->GetGPUVirtualAddress());

    // Dispatch（1024頂点ずつ処理。切り上げ）
    const uint32_t threadGroupCount = (skinCluster_.numVertices + 1023) / 1024;
    commandList->Dispatch(threadGroupCount, 1, 1);

    // 描画でVBVとして読めるStateへ遷移
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = skinCluster_.skinnedVertexResource.Get();
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    commandList->ResourceBarrier(1, &barrier);
    skinCluster_.skinnedVertexState = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;
}

void AnimatedObject3DInstance::Draw(DirectXCore* dxCore)
{
#ifdef _DEBUG
    if (!visibleInEditor_) return;
#endif
    if (!animatedModelInstance_ || !hasSkinCluster_ || !skinningComputeManager_) {
        return;
    }

    // CS実行（バリア込み）
    DispatchSkinning(dxCore);

    // Object3DManagerのRootSignature/PSOへ切替
    if (object3DManager_) {
        dxCore->GetCommandList()->SetGraphicsRootSignature(
            object3DManager_->GetRootSignature()
        );

        Material* mat = animatedModelInstance_->GetMaterialPointer();
        Object3DManager::ShaderType shaderType = (mat && mat->useEnvironmentMap)
            ? Object3DManager::kShaderEnvironmentMap
            : Object3DManager::kShaderNoEnvironmentMap;
        dxCore->GetCommandList()->SetPipelineState(
            object3DManager_->GetPipelineState(shaderType)
        );
    }

    // RootParam[1] TransformationMatrix
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        1, transformationMatrixResource_->GetGPUVirtualAddress()
    );

    // RootParam[4] Camera
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        4, cameraResource_->GetGPUVirtualAddress()
    );

    LightManager::GetInstance()->BindLights(dxCore->GetCommandList());

    // 環境マップは必ずバインドする（PSのvalidationが要求するため）
    if (object3DManager_) {
        const std::string& envPath = object3DManager_->GetEnvironmentTexturePath();
        if (!envPath.empty()) {
            dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
                7, TextureManager::GetInstance()->GetSrvHandleGPU(envPath)
            );
        }
    }

    // Skinning済みVBVで描画
    animatedModelInstance_->DrawSkinning(dxCore, skinCluster_);
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

            // ----- .anim ドロップ受付 -----
            const std::string& curAnim = animatedModelInstance_->GetAnimationPath();
            ImGui::Text("Current: %s", curAnim.empty() ? "(model default)" : curAnim.c_str());
            ImGui::DragFloat("Fade Time", &defaultFadeTime_, 0.01f, 0.0f, 5.0f, "%.2f s");
            ImGui::Button("Drop .anim here to play (with fade)", ImVec2(-FLT_MIN, 0));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload(ANIM_DROP_PAYLOAD_TYPE)) {
                    const auto* p = static_cast<const AnimDropPayload*>(payload->Data);
                    PlayAnimation(p->animPath, defaultFadeTime_);
                }
                ImGui::EndDragDropTarget();
            }
            if (hasPrevAnimation_ && fadeDuration_ > 0.0f) {
                ImGui::Text("Fading: %.2f / %.2f s", fadeTimer_, fadeDuration_);
            }
            ImGui::Separator();

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
                // .mat ドロップ受付
                ImGui::Button("Drop .mat here to apply", ImVec2(-FLT_MIN, 0));
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(MATERIAL_DROP_PAYLOAD_TYPE)) {
                        const auto* p = static_cast<const MaterialDropPayload*>(payload->Data);
                        MaterialData data;
                        if (LoadMatFile(p->materialPath, data, mat)) {
                            if (!data.textureFilePath.empty()) {
                                TextureManager::GetInstance()->LoadTexture(data.textureFilePath);
                                animatedModelInstance_->SetTextureFilePath(data.textureFilePath);
                            }
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

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

    // Texture
    if (ImGui::CollapsingHeader("Texture")) {
        const std::string& cur = animatedModelInstance_
            ? animatedModelInstance_->GetTextureFilePath() : std::string();
        ImGui::Text("Current: %s", cur.empty() ? "(none)" : cur.c_str());

        ImGui::Button("Drop .dds here to apply", ImVec2(-FLT_MIN, 0));
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload(SPRITE_DROP_PAYLOAD_TYPE)) {
                const auto* p = static_cast<const SpriteDropPayload*>(payload->Data);
                if (animatedModelInstance_) {
                    TextureManager::GetInstance()->LoadTexture(p->texturePath);
                    animatedModelInstance_->SetTextureFilePath(p->texturePath);
                }
            }
            ImGui::EndDragDropTarget();
        }
    }

#endif
}