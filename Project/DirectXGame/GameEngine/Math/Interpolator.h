#pragma once
#include "Easing.h"

template<typename T>
class Interpolator {
public:
    Interpolator() = default;
    Interpolator(const T& start, const T& end, Easing::Type type = Easing::Type::Linear)
        : start_(start), end_(end), type_(type) {
    }

    // t は 0.0〜1.0（範囲外は自動でクランプ）
    T Evaluate(float t) const {
        if (t < 0.0f) t = 0.0f;
        if (t > 1.0f) t = 1.0f;
        float eased = Easing::Apply(type_, t);
        return start_ + (end_ - start_) * eased;
    }

    // セッター
    void SetStart(const T& v) { start_ = v; }
    void SetEnd(const T& v) { end_ = v; }
    void SetType(Easing::Type type) { type_ = type; }

    // ゲッター
    const T& GetStart() const { return start_; }
    const T& GetEnd()   const { return end_; }
    Easing::Type GetType() const { return type_; }

private:
    T start_{};
    T end_{};
    Easing::Type type_ = Easing::Type::Linear;
};