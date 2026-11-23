#pragma once
#include "Easing.h"

template<typename T>
class Interpolator {
public:
    Interpolator() = default;
    /// <summary>始点・終点・イージングを決めて作る</summary>
    /// <param name="start">始点の値</param>
    /// <param name="end">終点の値</param>
    /// <param name="type">イージングの種類</param>
    Interpolator(const T& start, const T& end, Easing::Type type = Easing::Type::Linear)
        : start_(start), end_(end), type_(type) {
    }

    /// <summary>イージングを掛けて始点と終点の間を補間する（start + (end - start) * eased）</summary>
    /// <param name="t">進み具合（0.0〜1.0。範囲外はクランプ）</param>
    /// <returns>補間した値</returns>
    T Evaluate(float t) const {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float eased = Easing::Apply(type_, t);
        return start_ + (end_ - start_) * eased;
    }

    /// <summary>始点の値を設定する</summary>
    /// <param name="v">始点の値</param>
    void SetStart(const T& v) { start_ = v; }
    /// <summary>終点の値を設定する</summary>
    /// <param name="v">終点の値</param>
    void SetEnd(const T& v) { end_ = v; }
    /// <summary>イージングの種類を設定する</summary>
    /// <param name="type">イージングの種類</param>
    void SetType(Easing::Type type) { type_ = type; }

    /// <summary>始点の値</summary>
    /// <returns>始点の値</returns>
    const T& GetStart() const { return start_; }
    /// <summary>終点の値</summary>
    /// <returns>終点の値</returns>
    const T& GetEnd()   const { return end_; }
    /// <summary>イージングの種類</summary>
    /// <returns>イージングの種類</returns>
    Easing::Type GetType() const { return type_; }

private:
    T start_{};
    T end_{};
    Easing::Type type_ = Easing::Type::Linear;
};