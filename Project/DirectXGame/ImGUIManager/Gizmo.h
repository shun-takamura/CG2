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
    /// 選択中のオブジェクトの位置に X/Y/Z 軸と中央のハンドルを描き、ドラッグで動かす（translate を持たない・カメラが無ければ何もしない）
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
    /// <returns>ドラッグ中なら true</returns>
    bool IsDragging() const { return activeAxis_ >= 0; }

private:
    /// <summary>
    /// ワールド座標を、ビューポートに描いたゲーム画像のスクリーン座標にする
    /// </summary>
    /// <param name="worldPos">ワールド座標</param>
    /// <param name="viewProj">ビュー × プロジェクション行列</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    /// <param name="outScreen">スクリーン座標を書く先</param>
    /// <returns>変換できたら true（カメラの後ろ＝w が 0 以下なら false）</returns>
    bool WorldToScreen(const Vector3& worldPos, const Matrix4x4& viewProj,
                       ImVec2 imagePos, ImVec2 imageSize, ImVec2& outScreen);

    /// <summary>
    /// 点と線分の最短距離（2D）
    /// </summary>
    /// <param name="p">点</param>
    /// <param name="a">線分の始点</param>
    /// <param name="b">線分の終点</param>
    /// <returns>距離 [px]</returns>
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
