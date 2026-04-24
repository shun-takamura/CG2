#pragma once
#include "MeshData.h"
#include <vector>

namespace PrimitiveGenerator {

    // XY平面のPlaneを生成（中心が原点、法線はZ+方向）
    MeshData CreatePlane(
        float width = 1.0f,
        float height = 1.0f,
        uint32_t divisionsX = 1,
        uint32_t divisionsY = 1,
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Boxを生成（中心が原点）
    MeshData CreateBox(
        const Vector3& size = { 1.0f, 1.0f, 1.0f },
        const Vector4& color = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // Ringの生成パラメータ
    struct RingParams {
        // 基本形状
        float outerRadius = 1.0f;
        float innerRadius = 0.5f;
        uint32_t divisions = 32;

        // 色（内側・外側）
        Vector4 innerColor = { 1.0f, 1.0f, 1.0f, 1.0f };
        Vector4 outerColor = { 1.0f, 1.0f, 1.0f, 1.0f };

        // 拡張①：角度範囲（ラジアン指定、デフォルトは円全体）
        float startAngle = 0.0f;
        float endAngle = 2.0f * 3.14159265358979323846f;

        bool uvHorizon = true;
        float fadeStart = 0.0f;  // デフォルト：内径からグラデーションを開始
        float fadeEnd = 1.0f;    // デフォルト：外径で終わる
        std::vector<float> outerRadiusPerDivision;
    };

    // Ringを生成（XY平面、Z+方向法線、中心が原点）
    MeshData CreateRing(
        float outerRadius = 1.0f,
        float innerRadius = 0.5f,
        uint32_t divisions = 32,
        const Vector4& innerColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& outerColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

    // RingParams版（拡張機能を利用可能）
    MeshData CreateRing(const RingParams& params);

    // Cylinderを生成（Y軸方向の筒、上下面なし、中心が原点）
    MeshData CreateCylinder(
        float topRadius = 1.0f,
        float bottomRadius = 1.0f,
        float height = 3.0f,
        uint32_t divisions = 32,
        const Vector4& topColor = { 1.0f, 1.0f, 1.0f, 1.0f },
        const Vector4& bottomColor = { 1.0f, 1.0f, 1.0f, 1.0f }
    );

}