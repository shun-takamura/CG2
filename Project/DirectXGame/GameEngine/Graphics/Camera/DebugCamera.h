#pragma once
#include "Vector3.h"
#include "Matrix4x4.h"
#include "MathUtility.h"

/// <summary>
/// ピボット周りを Orbit / Pan / Zoom で操作するデバッグ用カメラ。
/// 入力は host (ImGui ウィンドウ等) が読み取り、Orbit/Pan/Zoom メソッドで適用する。
/// </summary>
class DebugCamera
{
public:
    DebugCamera() = default;
    void Initialize();

    /// <summary>
    /// View / Projection を現在の状態から再構築する。毎フレーム末に呼ぶ。
    /// </summary>
    void Update();

    /// <summary>
    /// ピボット周りに回転（カメラ右軸=pitch, ワールドY=yaw）。
    /// </summary>
    void Orbit(float deltaYaw, float deltaPitch);

    /// <summary>
    /// カメラの右/上ベクトル方向にピボットを動かす。値はワールド単位。
    /// </summary>
    void Pan(float deltaRight, float deltaUp);

    /// <summary>
    /// ピボットからの距離を変える。正で離れる、負で近づく。
    /// </summary>
    void Zoom(float delta);

    // 設定
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }
    void SetFovY(float fovY)         { fovY_ = fovY; }
    void SetNearFar(float n, float f) { nearClip_ = n; farClip_ = f; }
    void SetPivot(const Vector3& p)  { pivot_ = p; }
    void SetDistance(float d)        { distance_ = d < kMinDistance ? kMinDistance : d; }

    /// <summary>
    /// eye(目の位置) / rot(回転行列) / distance から周回状態(pivot,matRot,distance)を逆算してセットする。
    /// 通常カメラのポーズをそのまま引き継いでデバッグカメラを始めたいときに使う。
    /// </summary>
    void SetPose(const Vector3& eye, const Matrix4x4& rot, float distance);

    // 取得
    const Matrix4x4& GetViewMatrix() const           { return viewMatrix_; }
    const Matrix4x4& GetProjectionMatrix() const     { return projectionMatrix_; }
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    const Vector3&   GetTranslate() const            { return translate_; }
    const Vector3&   GetPivot() const                { return pivot_; }
    float            GetDistance() const             { return distance_; }
    float            GetFovY() const                 { return fovY_; }

private:
    static constexpr float kMinDistance = 0.1f;

    // 状態
    Matrix4x4 matRot_ = MakeIdentity4x4();
    Vector3   pivot_  = { 0.0f, 0.0f, 0.0f };
    float     distance_ = 10.0f;

    // 投影パラメータ
    float fovY_        = 0.45f;            // ~25.8°
    float aspectRatio_ = 16.0f / 9.0f;
    float nearClip_    = 0.1f;
    float farClip_     = 10000.0f;

    // キャッシュ
    Matrix4x4 viewMatrix_           = MakeIdentity4x4();
    Matrix4x4 projectionMatrix_     = MakeIdentity4x4();
    Matrix4x4 viewProjectionMatrix_ = MakeIdentity4x4();
    Vector3   translate_            = { 0.0f, 0.0f, -10.0f };
};
