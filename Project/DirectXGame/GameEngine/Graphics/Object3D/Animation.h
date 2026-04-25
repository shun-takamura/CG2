#pragma once
#include "MathUtility.h"
#include "Quaternion.h"
#include <vector>
#include <map>
#include <string>

// =====================================
// Keyframe（時刻と値のセット）
// =====================================
template <typename tValue>
struct Keyframe {
    float time;     // キーフレームの時刻（秒）
    tValue value;   // キーフレームの値
};

using KeyframeVector3 = Keyframe<Vector3>;
using KeyframeQuaternion = Keyframe<Quaternion>;

// =====================================
// AnimationCurve（キーフレームの配列）
// =====================================
template <typename tValue>
struct AnimationCurve {
    std::vector<Keyframe<tValue>> keyframes;
};

// =====================================
// NodeAnimation（1つのNodeのアニメーション）
// =====================================
struct NodeAnimation {
    AnimationCurve<Vector3> translate;
    AnimationCurve<Quaternion> rotate;
    AnimationCurve<Vector3> scale;
};

// =====================================
// Animation（アニメーション全体）
// =====================================
struct Animation {
    float duration;  // アニメーション全体の長さ（秒）
    // Node名 → そのNodeのアニメーション
    std::map<std::string, NodeAnimation> nodeAnimations;
};

// =====================================
// 関数宣言
// =====================================

// アニメーションファイルを読み込む
Animation LoadAnimationFile(const std::string& directoryPath, const std::string& filename);

// 任意の時刻のVector3値を計算（線形補間）
Vector3 CalculateValue(const std::vector<KeyframeVector3>& keyframes, float time);

// 任意の時刻のQuaternion値を計算（球面線形補間）
Quaternion CalculateValue(const std::vector<KeyframeQuaternion>& keyframes, float time);