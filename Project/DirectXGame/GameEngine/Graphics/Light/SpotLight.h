#pragma once
#include "Vector3.h"
#include "Vector4.h"

// 最大ライト数
static const uint32_t kMaxSpotLights = 8;

struct SpotLight {
    Vector4 color;        // ライトの色
    Vector3 position;     // ライトの位置
    float intensity;      // 輝度
    Vector3 direction;    // スポットライトの方向
    float distance;       // ライトの届く最大距離
    float decay;          // 減衰率
    float cosAngle;       // スポットライトの余弦（cos(θ)）
    float cosFalloffStart; // Falloff開始角度の余弦
    float padding;        // アライメント用
};

// 複数SpotLight用の構造体
struct SpotLightGroup {
    SpotLight lights[kMaxSpotLights];
    uint32_t activeCount;    // 有効なライトの数
    float padding[3];        // 16バイトアライメント
};