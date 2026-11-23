#include "SpriteGizmo.h"
#include "IImGuiEditable.h"
#include <cmath>

namespace {
    // Y 軸のハンドルの番号（0=中心, 1=X の次）
    constexpr int kHandleAxisY = 2;
    // 軸の先端の丸の半径 [px]
    constexpr float kAxisTipRadius = 4.0f;
}

ImVec2 SpriteGizmo::SpriteToScreen(const Vector2& spritePos,
                                   ImVec2 imagePos, ImVec2 imageSize,
                                   float clientW, float clientH) {
    return ImVec2(
        imagePos.x + (spritePos.x / clientW) * imageSize.x,
        imagePos.y + (spritePos.y / clientH) * imageSize.y
    );
}

Vector2 SpriteGizmo::ScreenToSprite(ImVec2 screenPos,
                                    ImVec2 imagePos, ImVec2 imageSize,
                                    float clientW, float clientH) {
    Vector2 r;
    r.x = (screenPos.x - imagePos.x) / imageSize.x * clientW;
    r.y = (screenPos.y - imagePos.y) / imageSize.y * clientH;
    return r;
}

void SpriteGizmo::Draw(IImGuiEditable* selected,
                       ImVec2 imagePos, ImVec2 imageSize,
                       float clientWidth, float clientHeight) {
    if (!selected) return;
    Vector2* posPtr = selected->GetEditable2DPosition();
    if (!posPtr) return;

    Vector2 origin = *posPtr;
    ImVec2 originScreen = SpriteToScreen(origin, imagePos, imageSize, clientWidth, clientHeight);

    // 軸の先端スクリーン座標
    ImVec2 xEnd = ImVec2(originScreen.x + kAxisLength, originScreen.y);
    ImVec2 yEnd = ImVec2(originScreen.x, originScreen.y + kAxisLength);

    // カラー
    const ImU32 colX = IM_COL32(255, 60, 60, 255);
    const ImU32 colY = IM_COL32(60, 220, 60, 255);
    const ImU32 colCenter = IM_COL32(255, 255, 255, 255);
    const ImU32 colHover = IM_COL32(255, 255, 60, 255);

    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;

    // ヒット判定（ドラッグ中は無視）
    int hover = -1;
    if (activeHandle_ < 0) {
        // 中心
        float dx = mouse.x - originScreen.x;
        float dy = mouse.y - originScreen.y;
        float distCenter = std::sqrt(dx * dx + dy * dy);
        if (distCenter < kCenterRadius + kHitThreshold) {
            hover = 0;
        } else {
            // X軸（中心からxEndまで、中心の近傍は除く）
            if (mouse.x >= originScreen.x && mouse.x <= xEnd.x &&
                std::abs(mouse.y - originScreen.y) < kHitThreshold) {
                hover = 1;
            }
            // Y軸
            else if (mouse.y >= originScreen.y && mouse.y <= yEnd.y &&
                     std::abs(mouse.x - originScreen.x) < kHitThreshold) {
                hover = kHandleAxisY;
            }
        }
    }

    // 描画
    ImU32 cx = (hover == 1 || activeHandle_ == 1) ? colHover : colX;
    ImU32 cy = (hover == kHandleAxisY || activeHandle_ == kHandleAxisY) ? colHover : colY;
    ImU32 cc = (hover == 0 || activeHandle_ == 0) ? colHover : colCenter;

    drawList->AddLine(originScreen, xEnd, cx, kHandleThickness);
    drawList->AddCircleFilled(xEnd, kAxisTipRadius, cx);
    drawList->AddLine(originScreen, yEnd, cy, kHandleThickness);
    drawList->AddCircleFilled(yEnd, kAxisTipRadius, cy);
    drawList->AddCircleFilled(originScreen, kCenterRadius, cc);

    // ドラッグ開始
    if (activeHandle_ < 0 && hover >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        activeHandle_ = hover;
        dragStartMousePos_ = mouse;
        dragStartPosition_ = origin;
    }

    // ドラッグ中
    if (activeHandle_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            // マウスのスクリーン上のデルタを Sprite 座標系に変換
            float deltaScreenX = mouse.x - dragStartMousePos_.x;
            float deltaScreenY = mouse.y - dragStartMousePos_.y;
            float deltaSpriteX = deltaScreenX / imageSize.x * clientWidth;
            float deltaSpriteY = deltaScreenY / imageSize.y * clientHeight;

            Vector2 newPos = dragStartPosition_;
            if (activeHandle_ == 0) {
                newPos.x += deltaSpriteX;
                newPos.y += deltaSpriteY;
            } else if (activeHandle_ == 1) {
                newPos.x += deltaSpriteX;
            } else if (activeHandle_ == kHandleAxisY) {
                newPos.y += deltaSpriteY;
            }
            *posPtr = newPos;
        } else {
            activeHandle_ = -1;
        }
    }
}