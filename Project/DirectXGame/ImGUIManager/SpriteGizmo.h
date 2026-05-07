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
    /// ギズモを描画＆操作する
    /// </summary>
    /// <param name="selected">選択中のオブジェクト（Sprite想定）</param>
    /// <param name="imagePos">ゲーム画像のスクリーン上の左上位置</param>
    /// <param name="imageSize">ゲーム画像のスクリーン上のサイズ</param>
    /// <param name="clientWidth">ゲーム解像度の幅（kClientWidth）</param>
    /// <param name="clientHeight">ゲーム解像度の高さ（kClientHeight）</param>
    void Draw(IImGuiEditable* selected,
              ImVec2 imagePos, ImVec2 imageSize,
              float clientWidth, float clientHeight);

private:
    /// <summary>
    /// Sprite座標 → スクリーン座標
    /// </summary>
    ImVec2 SpriteToScreen(const Vector2& spritePos,
                          ImVec2 imagePos, ImVec2 imageSize,
                          float clientW, float clientH);

    /// <summary>
    /// スクリーン座標 → Sprite座標
    /// </summary>
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
