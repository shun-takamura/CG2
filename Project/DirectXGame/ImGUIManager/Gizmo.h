#pragma once
#include "imgui.h"
#include "Vector3.h"
#include "Matrix4x4.h"

// 前方宣言
class IImGuiEditable;
class Camera;

/// <summary>
/// 3D移動ギズモ（X/Y/Z軸ハンドル）
/// 選択オブジェクトの位置に色付き軸を描画し、マウスドラッグで移動可能
/// </summary>
class Gizmo {
public:
    /// <summary>
    /// ギズモを描画＆操作する
    /// </summary>
    /// <param name="selected">選択中のオブジェクト（Object3DInstance想定）</param>
    /// <param name="camera">アクティブなカメラ</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    void Draw(IImGuiEditable* selected, const Camera* camera,
              ImVec2 imagePos, ImVec2 imageSize);

    /// <summary>
    /// 軸ハンドルをドラッグ中か。Debugカメラの左ドラッグ回転を抑止する判定に使う。
    /// </summary>
    bool IsDragging() const { return activeAxis_ >= 0; }

private:
    /// <summary>
    /// ワールド座標 → スクリーン座標への変換
    /// </summary>
    /// <returns>スクリーン座標（imagePos基準）。画面外の場合は w<=0 で false</returns>
    bool WorldToScreen(const Vector3& worldPos, const Matrix4x4& viewProj,
                       ImVec2 imagePos, ImVec2 imageSize, ImVec2& outScreen);

    /// <summary>
    /// 点とライン分の最短距離（2D）
    /// </summary>
    float DistancePointToLineSegment(ImVec2 p, ImVec2 a, ImVec2 b);

    // ドラッグ状態
    int activeAxis_ = -1;          // 0=X, 1=Y, 2=Z, 3=Center(自由移動), -1=なし
    ImVec2 dragStartMousePos_{};   // ドラッグ開始時のマウス位置
    Vector3 dragStartTranslate_{}; // ドラッグ開始時のオブジェクト位置

    // 自由移動用：ドラッグ開始時のカメラ右・上ベクトル（ワールド空間）
    Vector3 dragCameraRight_{};
    Vector3 dragCameraUp_{};

    // ハンドル設定
    static constexpr float kHandleLength = 1.5f;        // ワールド単位
    static constexpr float kHandleHitThreshold = 8.0f;  // ピクセル単位（ヒット判定半径）
    static constexpr float kHandleThickness = 4.0f;     // 線の太さ
    static constexpr float kCenterRadius = 6.0f;        // 中央ハンドル（白円）の半径
};
