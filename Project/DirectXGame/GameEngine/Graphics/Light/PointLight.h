#pragma once
#include "Vector4.h"
#include "Vector3.h"

// 最大ライト数
static const uint32_t kMaxPointLights = 8;

struct PointLight {
    Vector4 color;
    Vector3 position;
    float intensity;
    float radius;
    float decay;
    float padding[2];
};

// 複数PointLight用の構造体
struct PointLightGroup {
    PointLight lights[kMaxPointLights];
    uint32_t activeCount;    // 有効なライトの数
    float padding[3];        // 16バイトアライメント
};