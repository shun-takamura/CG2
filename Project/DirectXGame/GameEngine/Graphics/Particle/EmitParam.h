#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include "BillboardMode.h"
#include <cstdint>

// 色の決定モード
enum class ColorMode {
    Fixed,   // baseColorを使う
    Random   // RGB各チャンネルをランダムに決定
};

// パーティクル発生パラメータ
struct EmitParam {
    // 発生数と位置
    uint32_t count = 1;
    Vector3 position = { 0.0f, 0.0f, 0.0f };

    // --- スケール ---
    Vector3 scaleBase = { 1.0f, 1.0f, 1.0f };
    Vector3 scaleRandomMin = { 0.0f, 0.0f, 0.0f };
    Vector3 scaleRandomMax = { 0.0f, 0.0f, 0.0f };

    // --- 回転 ---
    Vector3 rotateBase = { 0.0f, 0.0f, 0.0f };
    Vector3 rotateRandomMin = { 0.0f, 0.0f, 0.0f };
    Vector3 rotateRandomMax = { 0.0f, 0.0f, 0.0f };

    // --- 速度 ---
    Vector3 velocityBase = { 0.0f, 0.0f, 0.0f };
    Vector3 velocityRandomMin = { 0.0f, 0.0f, 0.0f };
    Vector3 velocityRandomMax = { 0.0f, 0.0f, 0.0f };

    // --- 色 ---
    ColorMode colorMode = ColorMode::Fixed;
    Vector4 baseColor = { 1.0f, 1.0f, 1.0f, 1.0f };

    // --- 寿命 ---
    float lifeTime = 1.0f;
    float lifeTimeRandomRange = 0.0f; // ±のゆらぎ

    // --- ビルボード設定 ---
    BillboardMode billboardMode = BillboardMode::Billboard;
};