#pragma once
// ビルボードモード
// Primitive / CPU Particle / GPU Particle で共有
enum class BillboardMode {
    None,    // ビルボードなし（rotateで指定した向きのまま）
    Full,    // カメラ正面を向く（screen-aligned）
    YAxis,   // ワールドY軸固定で水平面内のみカメラを向く
};
