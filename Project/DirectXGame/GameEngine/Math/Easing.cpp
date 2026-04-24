#include "Easing.h"

namespace Easing {

    float Linear(float t) {
        return t;
    }

    float EaseOutCubic(float t) {
        // 1 - (1-t)^3 で「最後にゆっくり止まる」曲線を作る
        float x = 1.0f - t;
        return 1.0f - x * x * x;
    }

    float Apply(Type type, float t) {
        switch (type) {
        case Type::Linear:       return Linear(t);
        case Type::EaseOutCubic: return EaseOutCubic(t);
        }
        return t; // 未知の型ならLinear扱い
    }

}