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
#include "Skeleton.h"
#include "Animation.h"
#include <memory>
#include "SkinCluster.h"
#include <vector>

// ImGui対応
#include "IImGuiEditable.h"

class Object3DManager;
class PrimitiveMesh;  // unique_ptrで持つので前方宣言でOK
struct SkinCluster;
class SkinningComputeManager;
class SRVManager;

class AnimatedObject3DInstance : public IImGuiEditable {

    //==============================
    // メンバ変数
    //==============================
    std::string name_;

    Object3DManager* object3DManager_ = nullptr;
    AnimatedModelInstance* animatedModelInstance_ = nullptr;
    Camera* camera_ = nullptr;

    SkinningComputeManager* skinningComputeManager_ = nullptr;
    SRVManager* srvManager_ = nullptr;  // Drawで使うので保持

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
    std::string directoryPath_;  // ロード元 dirPath（シーンJSON保存用。明示的に SetSourcePath で設定）

    //==============================
    // アニメーション関連メンバ変数
    //==============================
    float animationTime_ = 0.0f;       // 現在の再生時刻（秒）
    bool isPlaying_ = true;            // 再生中フラグ
    bool visible_   = true;            // 描画フラグ（false で Draw をスキップ。全構成で有効）
    bool isLoop_ = true;               // ループ再生フラグ
    float playbackSpeed_ = 1.0f;       // 再生速度（1.0が等倍）

    // ----- クロスフェード状態 -----
    // PlayAnimation で旧クリップを prev に snapshot し、fadeDuration_ 秒かけて新クリップへ補間する。
    Animation prevAnimation_{};
    float prevAnimationTime_ = 0.0f;
    bool  hasPrevAnimation_ = false;
    float fadeTimer_ = 0.0f;           // フェード経過秒
    float fadeDuration_ = 0.0f;        // 0 ならフェード未進行
    float defaultFadeTime_ = 0.25f;    // .anim ドロップ時のデフォルト

    //==============================
    // Skeleton関連
    //==============================
    Skeleton skeleton_;
    bool hasSkeleton_ = false;

    SkinCluster skinCluster_;
    bool hasSkinCluster_ = false;

    // このフレームで既にスキニングをDispatch済みか。Update でリセットし、
    // DispatchSkinning で立てる。シャドウパス先行Dispatchとメイン描画の二重実行を防ぐ。
    bool skinningDispatchedThisFrame_ = false;

#ifdef USE_IMGUI
    // デバッグ描画用
    bool showSkeleton_ = false;
    Vector4 jointSphereColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    Vector4 jointLineColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
    float   jointSphereRadius_ = 0.025f;
    std::vector<std::unique_ptr<PrimitiveMesh>> debugSpheres_;
#endif

    //==============================
    // 内部関数
    //==============================
    void CreateTransformationMatrixResource(DirectXCore* dxCore);
    void CreateCameraResource(DirectXCore* dxCore);

public:
    AnimatedObject3DInstance() = default;
    ~AnimatedObject3DInstance() override;

    //==============================
    // IImGuiEditable実装
    //==============================
    std::string GetName() const override { return name_; }
    std::string GetTypeName() const override { return "AnimatedObject3D"; }
    void OnImGuiInspector() override;
    Vector3* GetEditableTranslate() override { return &transform_.translate; }
    const Vector3* GetEditableRotate() const override { return &transform_.rotate; }

    //==============================
    // セッター
    //==============================
    void SetName(const std::string& name) override { name_ = name; }
    void SetModel(AnimatedModelInstance* animatedModel) { animatedModelInstance_ = animatedModel; }
    void SetCamera(Camera* camera) { camera_ = camera; }
    void SetVisible(bool v) { visible_ = v; }
    bool GetVisible() const { return visible_; }
    void SetScale(const Vector3& scale) { transform_.scale = scale; }
    void SetRotate(const Vector3& rotate) { transform_.rotate = rotate; }
    void SetTranslate(const Vector3& translate) { transform_.translate = translate; }

    // Material関連
    void SetUseEnvironmentMap(bool use);
    void SetEnvironmentCoefficient(float coef);

    /// <summary>
    /// マテリアルカラー（GPU側 Material::color）を直接書き換える。
    /// 被弾点滅やヒット演出など、ランタイムでティント色を変えたい時に使う。
    /// SetModel 前は no-op。alpha < 1.0f で半透明になる。
    /// </summary>
    void SetMaterialColor(const Vector4& color);

    /// <summary>
    /// 現在のマテリアルカラーを取得（SetModel 前は白を返す）。
    /// </summary>
    Vector4 GetMaterialColor() const;

    //==============================
    // アニメーション制御
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

    /// <summary>
    /// 別の .anim へクロスフェード付きで切り替える。
    /// fadeTime > 0 のとき、現在のアニメーションを snapshot して fadeTime 秒かけて補間。
    /// fadeTime = 0 のときは即座に差し替え。
    /// </summary>
    void PlayAnimation(const std::string& animPath, float fadeTime);

    bool IsFading() const { return hasPrevAnimation_; }
    float GetDefaultFadeTime() const { return defaultFadeTime_; }
    void  SetDefaultFadeTime(float t) { defaultFadeTime_ = (t < 0.0f) ? 0.0f : t; }

    //==============================
    // ゲッター
    //==============================
    const Vector3& GetScale() const { return transform_.scale; }
    const Vector3& GetRotate() const { return transform_.rotate; }
    const Vector3& GetTranslate() const { return transform_.translate; }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }
    const std::string& GetModelFileName() const { return modelFileName_; }
    const std::string& GetDirectoryPath() const { return directoryPath_; }
    void SetSourcePath(const std::string& dirPath, const std::string& filename) {
        directoryPath_ = dirPath;
        modelFileName_ = filename;
    }

    //==============================
    // ボーン（ソケット）アクセス
    //==============================
    bool HasSkeleton() const { return hasSkeleton_; }
    const Skeleton& GetSkeleton() const { return skeleton_; }

    /// <summary>
    /// 指定ジョイントのワールド行列を返す（武器追従・エフェクト発生点に使う）。
    /// skeletonSpaceMatrix × モデルのworldMatrix。スケルトン未生成や名前不一致なら
    /// モデル本体の worldMatrix をそのまま返す。Update 後に呼ぶこと。
    /// </summary>
    Matrix4x4 GetJointWorldMatrix(const std::string& jointName) const {
        Matrix4x4 world = MakeAffineMatrix(transform_);
        if (!hasSkeleton_) return world;
        auto it = skeleton_.jointMap.find(jointName);
        if (it == skeleton_.jointMap.end()) return world;
        return Multiply(skeleton_.joints[it->second].skeletonSpaceMatrix, world);
    }

    /// <summary>
    /// ソケット用：ジョイントのワールド行列からスケールを除去（基底ベクトルを正規化）し、
    /// 回転＋位置のみを返す。mixamo の Armature に焼き込まれた cm→m スケール等を取り除き、
    /// アタッチした武器/エフェクトが極小に潰れるのを防ぐ。サイズはアタッチ側オフセットで決める。
    /// </summary>
    Matrix4x4 GetJointSocketMatrix(const std::string& jointName) const {
        const Matrix4x4 w = GetJointWorldMatrix(jointName);
        const Vector3 bx = Normalize(Vector3{ w.m[0][0], w.m[0][1], w.m[0][2] });
        const Vector3 by = Normalize(Vector3{ w.m[1][0], w.m[1][1], w.m[1][2] });
        const Vector3 bz = Normalize(Vector3{ w.m[2][0], w.m[2][1], w.m[2][2] });
        return Matrix4x4{ {
            { bx.x, bx.y, bx.z, 0.0f },
            { by.x, by.y, by.z, 0.0f },
            { bz.x, bz.y, bz.z, 0.0f },
            { w.m[3][0], w.m[3][1], w.m[3][2], 1.0f },
        } };
    }

    //==============================
    // 初期化・更新・描画
    //==============================
    void Initialize(Object3DManager* object3DManager,
        SkinningComputeManager* skinningComputeManager,
        DirectXCore* dxCore,
        SRVManager* srvManager,
        AnimatedModelInstance* animatedModel,
        const std::string& name = "");

    void Update(float deltaTime);  // 引数にdeltaTimeを追加（可変フレーム対応）

    // ComputeShader版でSkinning計算をDispatchする（Drawの前に呼ぶ）
    void DispatchSkinning(DirectXCore* dxCore);

    void Draw(DirectXCore* dxCore);

    // ID Pass 用（Skinning 済み VBV を再利用、Object3DManager の ID PSO を使用）
    void DrawIdPass(DirectXCore* dxCore);

    // シャドウパス用：transform を b0 にバインドし、Skinning 済み VBV で深度を書く。
    // スキニングのDispatchは事前に済んでいる前提（DispatchDynamicAnimatedSkinning）。
    void DrawShadowPass(DirectXCore* dxCore);

#ifdef USE_IMGUI
    // Skeletonのデバッグ描画（全モデル描画後に呼ぶ）
    void DrawSkeletonDebug(DirectXCore* dxCore);
#endif
};