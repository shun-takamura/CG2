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
    // ビーム/雷など「面がカメラに対して斜めだとフェードさせたい」用途で使う角度フェード
    // 0=無効。>0 のとき pow(|dot(viewDir, normal)|, power) を α に乗算する
    Vector3 cameraPos;        // ワールド空間のカメラ位置（PS用）
    float viewAngleFadePower; // 0で無効、推奨2〜4
    // ディゾルブ（オブジェクト単位）。mask.r < threshold を discard。HLSL の Material 末尾と一致させること。
    int   dissolveEnable = 0;        // 0=無効 / 1=有効
    float dissolveThreshold = 0.0f;  // 0..1
    int   dissolveEdgeEnable = 0;    // アウトライン 0=無効 / 1=有効
    float dissolveEdgeWidth = 0.05f; // エッジの太さ（マスク値での幅）
    Vector4 dissolveEdgeColor = { 1.0f, 0.4f, 0.1f, 1.0f }; // エッジの発光色
};
