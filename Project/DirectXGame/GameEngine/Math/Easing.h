#pragma once

namespace Easing {

    // イージングの種類
    enum class Type {
        Linear,
        EaseOutCubic,
    };

    // 基本関数（t は 0.0〜1.0）
    float Linear(float t);
    float EaseOutCubic(float t);

    // enumから対応する関数を呼び出す
    float Apply(Type type, float t);

}