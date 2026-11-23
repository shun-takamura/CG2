#pragma once

namespace Easing {

    // イージングの種類
    enum class Type {
        Linear,
        EaseOutCubic,
    };

    /// <summary>そのまま返す（等速）</summary>
    /// <param name="t">進み具合（0.0〜1.0）</param>
    /// <returns>t</returns>
    float Linear(float t);
    /// <summary>1 - (1-t)^3。最後にゆっくり止まる</summary>
    /// <param name="t">進み具合（0.0〜1.0）</param>
    /// <returns>イージング後の値（0.0〜1.0）</returns>
    float EaseOutCubic(float t);

    /// <summary>種類に対応するイージングを掛ける</summary>
    /// <param name="type">イージングの種類</param>
    /// <param name="t">進み具合（0.0〜1.0）</param>
    /// <returns>イージング後の値（知らない種類なら t のまま）</returns>
    float Apply(Type type, float t);

}