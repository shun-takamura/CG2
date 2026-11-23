#pragma once
#include "imgui.h"
#include "Vector2.h"

// 前方宣言
class IImGuiEditable;

/// <summary>
/// 2D（スクリーン座標）移動ギズモ
/// Sprite等の position_(Vector2) を ViewportWindow 内でドラッグ移動できる
/// </summary>
class SpriteGizmo {
public:
    /// <summary>
    /// 選択中のオブジェクトの 2D 位置に中央・X・Y のハンドルを描き、ドラッグで動かす（2D 位置を持たなければ何もしない）
    /// </summary>
    /// <param name="selected">選択中のオブジェクト（Sprite想定）</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    /// <param name="clientWidth">UI の仮想解像度の幅（kUIWidth）</param>
    /// <param name="clientHeight">UI の仮想解像度の高さ（kUIHeight）</param>
    void Draw(IImGuiEditable* selected,
              ImVec2 imagePos, ImVec2 imageSize,
              float clientWidth, float clientHeight);

private:
    /// <summary>
    /// Sprite の座標（UI の仮想解像度）を、ビューポートに描いたゲーム画像のスクリーン座標にする
    /// </summary>
    /// <param name="spritePos">Sprite の座標</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    /// <param name="clientW">UI の仮想解像度の幅</param>
    /// <param name="clientH">UI の仮想解像度の高さ</param>
    /// <returns>スクリーン座標</returns>
    ImVec2 SpriteToScreen(const Vector2& spritePos,
                          ImVec2 imagePos, ImVec2 imageSize,
                          float clientW, float clientH);

    /// <summary>
    /// ビューポートに描いたゲーム画像のスクリーン座標を、Sprite の座標（UI の仮想解像度）にする
    /// </summary>
    /// <param name="screenPos">スクリーン座標</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    /// <param name="clientW">UI の仮想解像度の幅</param>
    /// <param name="clientH">UI の仮想解像度の高さ</param>
    /// <returns>Sprite の座標</returns>
    Vector2 ScreenToSprite(ImVec2 screenPos,
                           ImVec2 imagePos, ImVec2 imageSize,
                           float clientW, float clientH);

    // ドラッグ状態
    int activeHandle_ = -1;          // 0=中央(両軸), 1=X軸, 2=Y軸, -1=なし
    ImVec2 dragStartMousePos_{};
    Vector2 dragStartPosition_{};

    // ハンドル設定（スクリーンピクセル単位）
    static constexpr float kAxisLength = 50.0f;        // 軸ハンドルの長さ
    static constexpr float kHitThreshold = 8.0f;       // ヒット判定半径
    static constexpr float kCenterRadius = 6.0f;       // 中心ハンドル半径
    static constexpr float kHandleThickness = 3.0f;    // 軸の太さ
};
