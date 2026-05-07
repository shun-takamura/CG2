#include "Gizmo.h"
#include "IImGuiEditable.h"
#include "Camera.h"
#include "MathUtility.h"
#include <cmath>

namespace {
    // 4x4行列とVector4(行ベクトル)の積（クリップ空間用）
    struct Vec4 { float x, y, z, w; };
    Vec4 MulRowVec4Mat4(const Vector3& v, float w, const Matrix4x4& m) {
        Vec4 r;
        r.x = v.x * m.m[0][0] + v.y * m.m[1][0] + v.z * m.m[2][0] + w * m.m[3][0];
        r.y = v.x * m.m[0][1] + v.y * m.m[1][1] + v.z * m.m[2][1] + w * m.m[3][1];
        r.z = v.x * m.m[0][2] + v.y * m.m[1][2] + v.z * m.m[2][2] + w * m.m[3][2];
        r.w = v.x * m.m[0][3] + v.y * m.m[1][3] + v.z * m.m[2][3] + w * m.m[3][3];
        return r;
    }

    // 外積（プロジェクトに Cross 関数が無いためここで実装）
    Vector3 Cross(const Vector3& a, const Vector3& b) {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }
}

bool Gizmo::WorldToScreen(const Vector3& worldPos, const Matrix4x4& viewProj,
                          ImVec2 imagePos, ImVec2 imageSize, ImVec2& outScreen) {
    Vec4 clip = MulRowVec4Mat4(worldPos, 1.0f, viewProj);
    if (clip.w <= 0.0001f) return false;  // カメラ後方

    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;

    outScreen.x = imagePos.x + (ndcX + 1.0f) * 0.5f * imageSize.x;
    outScreen.y = imagePos.y + (1.0f - ndcY) * 0.5f * imageSize.y;
    return true;
}

float Gizmo::DistancePointToLineSegment(ImVec2 p, ImVec2 a, ImVec2 b) {
    float dx = b.x - a.x;
    float dy = b.y - a.y;
    float lenSq = dx * dx + dy * dy;
    if (lenSq < 0.0001f) {
        float ddx = p.x - a.x, ddy = p.y - a.y;
        return std::sqrt(ddx * ddx + ddy * ddy);
    }
    float t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / lenSq;
    if (t < 0.0f) t = 0.0f;
    else if (t > 1.0f) t = 1.0f;
    float closestX = a.x + t * dx;
    float closestY = a.y + t * dy;
    float ddx = p.x - closestX, ddy = p.y - closestY;
    return std::sqrt(ddx * ddx + ddy * ddy);
}

void Gizmo::Draw(IImGuiEditable* selected, const Camera* camera,
                 ImVec2 imagePos, ImVec2 imageSize) {
    if (!selected || !camera) return;

    // ギズモ対応オブジェクトかチェック（Translate へのポインタを取得）
    Vector3* translatePtr = selected->GetEditableTranslate();
    if (!translatePtr) return;

    Vector3 origin = *translatePtr;
    const Matrix4x4& viewProj = camera->GetViewProjectionMatrix();

    // 各軸の先端ワールド座標
    Vector3 axisEndWorld[3] = {
        { origin.x + kHandleLength, origin.y, origin.z }, // X
        { origin.x, origin.y + kHandleLength, origin.z }, // Y
        { origin.x, origin.y, origin.z + kHandleLength }, // Z
    };

    // スクリーン座標に変換
    ImVec2 originScreen;
    if (!WorldToScreen(origin, viewProj, imagePos, imageSize, originScreen)) return;

    ImVec2 axisEndScreen[3];
    bool axisVisible[3] = { false, false, false };
    for (int i = 0; i < 3; ++i) {
        axisVisible[i] = WorldToScreen(axisEndWorld[i], viewProj, imagePos, imageSize, axisEndScreen[i]);
    }

    // 描画用カラー
    const ImU32 colors[3] = {
        IM_COL32(255, 60, 60, 255),   // X = 赤
        IM_COL32(60, 220, 60, 255),   // Y = 緑
        IM_COL32(60, 130, 255, 255),  // Z = 青
    };
    const ImU32 hoverColor = IM_COL32(255, 255, 60, 255);  // 黄

    // ImGuiの描画リスト（最前面）
    ImDrawList* drawList = ImGui::GetForegroundDrawList();
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;

    // ホバー判定（ドラッグ中は無視）
    // 中央ハンドル（白円）を最優先でヒット判定し、軸ハンドルは外側にある分だけを扱う
    int hoverAxis = -1;
    if (activeAxis_ < 0) {
        // まず中央の白円
        float dxc = mousePos.x - originScreen.x;
        float dyc = mousePos.y - originScreen.y;
        float distCenter = std::sqrt(dxc * dxc + dyc * dyc);
        if (distCenter < kCenterRadius + kHandleHitThreshold * 0.5f) {
            hoverAxis = 3;  // 中央 = 自由移動
        } else {
            float minDist = kHandleHitThreshold;
            for (int i = 0; i < 3; ++i) {
                if (!axisVisible[i]) continue;
                float dist = DistancePointToLineSegment(mousePos, originScreen, axisEndScreen[i]);
                if (dist < minDist) {
                    minDist = dist;
                    hoverAxis = i;
                }
            }
        }
    }

    // 描画
    for (int i = 0; i < 3; ++i) {
        if (!axisVisible[i]) continue;
        ImU32 col = colors[i];
        if (i == hoverAxis || i == activeAxis_) col = hoverColor;
        drawList->AddLine(originScreen, axisEndScreen[i], col, kHandleThickness);
        drawList->AddCircleFilled(axisEndScreen[i], 5.0f, col);
    }
    // 中央の白円（hover/drag 中は黄色）
    {
        ImU32 centerCol = IM_COL32(255, 255, 255, 255);
        if (hoverAxis == 3 || activeAxis_ == 3) centerCol = hoverColor;
        drawList->AddCircleFilled(originScreen, kCenterRadius, centerCol);
    }

    // ドラッグ開始
    if (activeAxis_ < 0 && hoverAxis >= 0 && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        activeAxis_ = hoverAxis;
        dragStartMousePos_ = mousePos;
        dragStartTranslate_ = origin;

        // 自由移動の場合のみカメラ基底をキャッシュ（ドラッグ中固定）
        if (activeAxis_ == 3) {
            Vector3 camForward = camera->GetForward();
            Vector3 camUp = camera->GetUp();
            Vector3 right = Normalize(Cross(camUp, camForward));
            Vector3 up = Cross(camForward, right);
            dragCameraRight_ = right;
            dragCameraUp_ = up;
        }
    }

    // ドラッグ中
    if (activeAxis_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            ImVec2 mouseDelta = ImVec2(
                mousePos.x - dragStartMousePos_.x,
                mousePos.y - dragStartMousePos_.y
            );

            if (activeAxis_ == 3) {
                // ============================================
                // 自由移動：カメラ右・上ベクトルでスクリーンプレーン投影
                // ============================================
                ImVec2 startOriginScreen, rightTipScreen, upTipScreen;
                Vector3 rightTipWorld = {
                    dragStartTranslate_.x + dragCameraRight_.x,
                    dragStartTranslate_.y + dragCameraRight_.y,
                    dragStartTranslate_.z + dragCameraRight_.z
                };
                Vector3 upTipWorld = {
                    dragStartTranslate_.x + dragCameraUp_.x,
                    dragStartTranslate_.y + dragCameraUp_.y,
                    dragStartTranslate_.z + dragCameraUp_.z
                };
                if (WorldToScreen(dragStartTranslate_, viewProj, imagePos, imageSize, startOriginScreen) &&
                    WorldToScreen(rightTipWorld, viewProj, imagePos, imageSize, rightTipScreen) &&
                    WorldToScreen(upTipWorld, viewProj, imagePos, imageSize, upTipScreen)) {

                    ImVec2 rightDir2D = ImVec2(
                        rightTipScreen.x - startOriginScreen.x,
                        rightTipScreen.y - startOriginScreen.y);
                    ImVec2 upDir2D = ImVec2(
                        upTipScreen.x - startOriginScreen.x,
                        upTipScreen.y - startOriginScreen.y);

                    float rightLenSq = rightDir2D.x * rightDir2D.x + rightDir2D.y * rightDir2D.y;
                    float upLenSq = upDir2D.x * upDir2D.x + upDir2D.y * upDir2D.y;

                    float worldDxRight = (rightLenSq > 0.0001f)
                        ? (mouseDelta.x * rightDir2D.x + mouseDelta.y * rightDir2D.y) / rightLenSq
                        : 0.0f;
                    float worldDyUp = (upLenSq > 0.0001f)
                        ? (mouseDelta.x * upDir2D.x + mouseDelta.y * upDir2D.y) / upLenSq
                        : 0.0f;

                    Vector3 newPos = {
                        dragStartTranslate_.x + dragCameraRight_.x * worldDxRight + dragCameraUp_.x * worldDyUp,
                        dragStartTranslate_.y + dragCameraRight_.y * worldDxRight + dragCameraUp_.y * worldDyUp,
                        dragStartTranslate_.z + dragCameraRight_.z * worldDxRight + dragCameraUp_.z * worldDyUp,
                    };
                    *translatePtr = newPos;
                }
            } else {
                // ============================================
                // 軸拘束移動（既存ロジック）
                // ============================================
                Vector3 axisDir = { 0, 0, 0 };
                if (activeAxis_ == 0) axisDir.x = 1.0f;
                else if (activeAxis_ == 1) axisDir.y = 1.0f;
                else axisDir.z = 1.0f;

                ImVec2 startOriginScreen, startAxisEndScreen;
                Vector3 startAxisEndWorld = {
                    dragStartTranslate_.x + axisDir.x,
                    dragStartTranslate_.y + axisDir.y,
                    dragStartTranslate_.z + axisDir.z
                };
                if (WorldToScreen(dragStartTranslate_, viewProj, imagePos, imageSize, startOriginScreen) &&
                    WorldToScreen(startAxisEndWorld, viewProj, imagePos, imageSize, startAxisEndScreen)) {

                    ImVec2 axisDir2D = ImVec2(
                        startAxisEndScreen.x - startOriginScreen.x,
                        startAxisEndScreen.y - startOriginScreen.y
                    );
                    float axisLen2DSq = axisDir2D.x * axisDir2D.x + axisDir2D.y * axisDir2D.y;

                    if (axisLen2DSq > 0.0001f) {
                        float worldDelta = (mouseDelta.x * axisDir2D.x + mouseDelta.y * axisDir2D.y) / axisLen2DSq;

                        Vector3 newPos = {
                            dragStartTranslate_.x + axisDir.x * worldDelta,
                            dragStartTranslate_.y + axisDir.y * worldDelta,
                            dragStartTranslate_.z + axisDir.z * worldDelta
                        };
                        *translatePtr = newPos;
                    }
                }
            }
        } else {
            // ドラッグ終了
            activeAxis_ = -1;
        }
    }
}
