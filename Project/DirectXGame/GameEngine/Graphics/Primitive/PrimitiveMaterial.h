#pragma once
#include "Vector4.h"
#include "Matrix4x4.h"

// サンプラーモード値：
//   0 = WRAP U  / WRAP V  （タイリング前提のテクスチャ）
//   1 = WRAP U  / CLAMP V （Ring/Cylinder等、中心側で白いリングが出るのを防ぐ）
//   2 = CLAMP U / CLAMP V （まったく繰り返さない）

// Primitive専用のマテリアル構造体
struct PrimitiveMaterial {
    Vector4 color;
    int enableLighting;       // ライティング使用フラグ（現状未使用）
    float alphaReference;     // これ以下のアルファ値はdiscard（0.0でdiscardなし）
    int samplerMode;          // サンプラーモード（上記コメント参照）
    float padding;            // 16バイト境界合わせ
    Matrix4x4 uvTransform;
};
