#include "SpringBone.h"
#include "Skeleton.h"
#include "MathUtility.h"

void SpringBone::Initialize(const Skeleton& skeleton, const std::string& rootBoneName, const VerletParams& params)
{
    params_ = params;

    // Phase 1 で実装：
    //   rootBoneName を起点に Skeleton.joints を再帰収集し、
    //   particles_ / jointIndices_ を構築する（poseLocation は skeletonSpaceMatrix から）。
    (void)skeleton;
    (void)rootBoneName;
    initialized_ = false;
}

void SpringBone::Update(Skeleton& skeleton, float dt)
{
    if (!initialized_) {
        return;
    }

    // Phase 2-3 で実装：
    //   1. skeletonSpaceMatrix から各質点の poseLocation を読む
    //   2. solver_.Step(particles_, params_, colliders_, dt)
    //   3. 揺れた location から各親ジョイントのローカル回転を逆算し書き戻す
    (void)skeleton;
    (void)dt;
}

#ifdef USE_IMGUI
void SpringBone::DrawImGui()
{
    // Phase 7 で実装：params_（gravity / damping / stiffness / radius / limitAngle）と
    //               colliders_ の調整 UI を描く。
}
#endif
