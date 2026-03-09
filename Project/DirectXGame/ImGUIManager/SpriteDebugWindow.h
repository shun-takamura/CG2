#pragma once
#include "imgui.h"
#include "SpriteInstance.h"

/// <summary>
/// 確認課題用：スプライト座標をスライダーバーで操作するImGuiウィンドウ
/// 毎フレーム BeginFrame() と EndFrame() の間で呼ぶこと
/// </summary>
class SpriteDebugWindow {
public:

    /// <summary>
    /// ImGuiウィンドウを描画し、スプライトの座標をスライダーで操作する
    /// </summary>
    /// <param name="sprite">操作対象のスプライト</param>
    static void Draw(SpriteInstance* sprite) {
        // ウィンドウサイズを (500, 100) に設定
        ImGui::SetNextWindowSize(ImVec2(500, 100), ImGuiCond_Once);

        ImGui::Begin("SpritePosition");

        // 現在の座標を取得
        Vector2 position = sprite->GetPosition();

        // スライダーバーで座標を操作
        // "%.1f" → 整数部 + 小数部1桁で表示
        ImGui::SliderFloat2("Position", &position.x, 0.0f, 1280.0f, "%4.1f");

        // 変更をスプライトに反映
        sprite->SetPosition(position);

        ImGui::End();
    }
};
