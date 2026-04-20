#pragma once
#include "Matrix4x4.h"

/// <summary>
/// Skybox用の座標変換行列データ
/// WVPのみ
/// </summary>
struct SkyboxTransformationMatrix {
    Matrix4x4 WVP;
};