#pragma once
#include "Vector4.h"

/// <summary>
/// Skybox用のマテリアルデータ
/// color:   全体着色（テクスチャに乗算）
/// blendT:  2枚のCubemapの補間係数（0=現在スロット, 1=次スロット）
/// </summary>
struct SkyboxMaterial {
    Vector4 color;
    float   blendT;
    float   padding[3];
};
