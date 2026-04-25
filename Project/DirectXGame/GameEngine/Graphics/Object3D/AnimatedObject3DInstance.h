#pragma once
#include "Object3DManager.h"
#include "VertexData.h"
#include <fstream>
#include <sstream>
#include "Material.h"
#include <cassert>
#include "DirectXCore.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include "TransformationMatrix.h"
#include "DirectionalLight.h"
#include "PointLight.h"
#include "TextureManager.h"
#include "Transform.h"
#include "ModelCore.h"
#include "AnimatedModelInstance.h"
#include "Camera.h"
#include "CameraForGPU.h"

// ImGui対応
#include "IImGuiEditable.h"

class Object3DManager;

class AnimatedObject3DInstance : public IImGuiEditable {

    //==============================
    // メンバ変数
    //==============================
    std::string name_;

    Object3DManager* object3DManager_ = nullptr;
    AnimatedModelInstance* animatedModelInstance_ = nullptr;
    Camera* camera_ = nullptr;

    // バッファリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> transformationMatrixResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;

    TransformationMatrix* transformationMatrixData_ = nullptr;
    CameraForGPU* cameraData_ = nullptr;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

    Transform transform_;
    Transform cameraTransform_;

    std::string textureFilePath_;
    std::string modelFileName_;

    //==============================
    // ★アニメーション関連メンバ変数
    //==============================
    float animationTime_ = 0.0f;       // 現在の再生時刻（秒）
    bool isPlaying_ = true;            // 再生中フラグ
    bool isLoop_ = true;               // ループ再生フラグ
    float playbackSpeed_ = 1.0f;       // 再生速度（1.0が等倍）

    //==============================
    // 内部関数
    //==============================
    void CreateTransformationMatrixResource(DirectXCore* dxCore);
    void CreateCameraResource(DirectXCore* dxCore);

public:
    AnimatedObject3DInstance() = default;
    ~AnimatedObject3DInstance() override = default;

    //==============================
    // IImGuiEditable実装
    //==============================
    std::string GetName() const override { return name_; }
    std::string GetTypeName() const override { return "AnimatedObject3D"; }
    void OnImGuiInspector() override;

    //==============================
    // セッター
    //==============================
    void SetName(const std::string& name) { name_ = name; }
    void SetModel(AnimatedModelInstance* animatedModel) { animatedModelInstance_ = animatedModel; }
    void SetCamera(Camera* camera) { camera_ = camera; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // Material関連
    void SetUseEnvironmentMap(bool use);
    void SetEnvironmentCoefficient(float coef);

    //==============================
    // ★アニメーション制御
    //==============================
    void Play() { isPlaying_ = true; }
    void Pause() { isPlaying_ = false; }
    void Stop() { isPlaying_ = false; animationTime_ = 0.0f; }
    void SetAnimationTime(float time) { animationTime_ = time; }
    float GetAnimationTime() const { return animationTime_; }
    void SetLoop(bool loop) { isLoop_ = loop; }
    bool GetLoop() const { return isLoop_; }
    void SetPlaybackSpeed(float speed) { playbackSpeed_ = speed; }
    float GetPlaybackSpeed() const { return playbackSpeed_; }
    bool IsPlaying() const { return isPlaying_; }

    //==============================
    // ゲッター
    //==============================
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    const std::string& GetModelFileName() const { return modelFileName_; }

    //==============================
    // 初期化・更新・描画
    //==============================
    void Initialize(Object3DManager* object3DManager, DirectXCore* dxCore,
        AnimatedModelInstance* animatedModel,
        const std::string& name = "");

    void Update(float deltaTime);  // ★引数にdeltaTimeを追加（可変フレーム対応）

    void Draw(DirectXCore* dxCore);
};