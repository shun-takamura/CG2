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
    /// <summary>回転なし・ピボット原点・距離 10 に戻して行列を作り直す</summary>
    void Initialize();

    /// <summary>
    /// View / Projection を現在の状態から再構築する。毎フレーム末に呼ぶ。
    /// </summary>
    void Update();

    /// <summary>
    /// ピボット周りに回転（カメラ右軸=pitch, ワールドY=yaw）。
    /// </summary>
    /// <param name="deltaYaw">ワールド Y 軸まわりの回転量 [ラジアン]</param>
    /// <param name="deltaPitch">カメラの右軸まわりの回転量 [ラジアン]</param>
    void Orbit(float deltaYaw, float deltaPitch);

    /// <summary>
    /// カメラの右/上ベクトル方向にピボットを動かす。値はワールド単位。
    /// </summary>
    /// <param name="deltaRight">右方向への移動量 [m]</param>
    /// <param name="deltaUp">上方向への移動量 [m]</param>
    void Pan(float deltaRight, float deltaUp);

    /// <summary>
    /// ピボットからの距離を変える。正で離れる、負で近づく。
    /// 下限を割った分はピボットを視線の奥へ押し出す（前進して見える）
    /// </summary>
    /// <param name="delta">距離の変化量 [m]</param>
    void Zoom(float delta);

    // 設定
    /// <summary>縦横比を設定する</summary>
    /// <param name="ratio">幅 / 高さ</param>
    void SetAspectRatio(float ratio) { aspectRatio_ = ratio; }
    /// <summary>縦の視野角を設定する</summary>
    /// <param name="fovY">視野角 [ラジアン]</param>
    void SetFovY(float fovY)         { fovY_ = fovY; }
    /// <summary>ニア・ファークリップ距離を設定する（ファーは Update で距離の 2 倍を下限にする）</summary>
    /// <param name="n">ニアクリップ [m]</param>
    /// <param name="f">ファークリップ [m]</param>
    void SetNearFar(float n, float f) { nearClip_ = n; farClip_ = f; }
    /// <summary>周回の中心を設定する</summary>
    /// <param name="p">ピボットのワールド座標 [m]</param>
    void SetPivot(const Vector3& p)  { pivot_ = p; }
    /// <summary>ピボットからの距離を設定する</summary>
    /// <param name="d">距離 [m]（0.1 未満は 0.1）</param>
    void SetDistance(float d)        { distance_ = d < kMinDistance ? kMinDistance : d; }

    /// <summary>
    /// eye(目の位置) / rot(回転行列) / distance から周回状態(pivot,matRot,distance)を逆算してセットする。
    /// 通常カメラのポーズをそのまま引き継いでデバッグカメラを始めたいときに使う。
    /// </summary>
    /// <param name="eye">目の位置（ワールド座標 [m]）</param>
    /// <param name="rot">カメラの回転行列</param>
    /// <param name="distance">目からピボットまでの距離 [m]（0.1 未満は 0.1）</param>
    void SetPose(const Vector3& eye, const Matrix4x4& rot, float distance);

    // 取得
    /// <summary>ビュー行列を返す</summary>
    /// <returns>ビュー行列</returns>
    const Matrix4x4& GetViewMatrix() const           { return viewMatrix_; }
    /// <summary>射影行列を返す</summary>
    /// <returns>射影行列</returns>
    const Matrix4x4& GetProjectionMatrix() const     { return projectionMatrix_; }
    /// <summary>ビュー射影行列を返す</summary>
    /// <returns>ビュー × 射影の行列</returns>
    const Matrix4x4& GetViewProjectionMatrix() const { return viewProjectionMatrix_; }
    /// <summary>目の位置を返す</summary>
    /// <returns>ワールド座標 [m]</returns>
    const Vector3&   GetTranslate() const            { return translate_; }
    /// <summary>周回の中心を返す</summary>
    /// <returns>ピボットのワールド座標 [m]</returns>
    const Vector3&   GetPivot() const                { return pivot_; }
    /// <summary>ピボットからの距離を返す</summary>
    /// <returns>距離 [m]</returns>
    float            GetDistance() const             { return distance_; }
    /// <summary>縦の視野角を返す</summary>
    /// <returns>視野角 [ラジアン]</returns>
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
