#include "VerletSolver.h"
#include "MathUtility.h"

namespace {
// エンジンに Vector3 演算子が無いため、このモジュール内専用のヘルパーを用意する。
// （Dot / Length / Normalize / Lerp は MathUtility 側を利用）
inline Vector3 Add(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
inline Vector3 Sub(const Vector3& a, const Vector3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
inline Vector3 Scale(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
inline Vector3 Cross(const Vector3& a, const Vector3& b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
} // namespace

void VerletSolver::Step(std::vector<VerletParticle>& particles,
                        const VerletParams& params,
                        const std::vector<SphereCollider>& colliders,
                        float dt)
{
    // Phase 2 で実装：
    //   1. Verlet 積分（速度 = (location - prevLocation)/dt → damping → 重力）
    //   2. stiffness pull（poseLocation へバネで引き戻す）
    //   3. コリジョン押し出し（colliders との球判定）
    //   4. 角度制限（params.limitAngle）
    //   5. 長さ復元（boneLength を保つ）
    (void)particles;
    (void)params;
    (void)colliders;
    (void)dt;
}
