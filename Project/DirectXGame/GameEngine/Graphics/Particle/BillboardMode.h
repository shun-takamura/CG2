#pragma once
// ビルボードモード
// 循環参照を避けるためここで定義
enum class BillboardMode {
    None,       // ビルボードなし（rotateで指定した向きのまま）
    Billboard,  // カメラ正面を向く
};