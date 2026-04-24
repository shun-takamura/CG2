#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"

// Primitive専用のマテリアル構造体
struct PrimitiveMaterial {
    Vector4 color;
    int enableLighting;       // ライティング使用フラグ（現状未使用）
    float alphaReference;     // これ以下のアルファ値はdiscard（0.0でdiscardなし）
    float padding[2];         // 16バイト境界合わせ
    Matrix4x4 uvTransform;
};