#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "VerletSolver.h"

// Graphics/Object3D/Skeleton.h
struct Skeleton;

// Skeleton の指定ルート配下を二次モーション（揺れもの）でシミュレートする。
// 純ソルバ(VerletSolver) と Skeleton をつなぐ橋渡し層。
// 呼び出し順: ApplyAnimation -> UpdateSkeleton -> SpringBone::Update -> UpdateSkeleton -> スキニング
class SpringBone {
public:
    // rootBoneName 配下のジョイントを収集して質点列を構築する（Phase 1 で実装）。
    void Initialize(const Skeleton& skeleton, const std::string& rootBoneName, const VerletParams& params);

    // 揺れを計算し、結果を joint.transform.rotate に書き戻す（Phase 2-3 で実装）。
    // UpdateSkeleton() の後・スキニングの前に呼ぶ。
    void Update(Skeleton& skeleton, float dt);

    void AddSphereCollider(const SphereCollider& collider) { colliders_.push_back(collider); }

    VerletParams& Params() { return params_; }
    const VerletParams& Params() const { return params_; }

#ifdef USE_IMGUI
    // 親オブジェクトの Inspector から呼ぶ調整 UI（Phase 7 で実装）。
    // SpringBone はモデルのサブ機能なので IImGuiEditable は継承せず、
    // オーナー側の OnImGuiInspector から本メソッドを呼ぶ形にする。
    void DrawImGui();
#endif

private:
    std::vector<VerletParticle> particles_;     // シミュ対象の質点列
    std::vector<int32_t>        jointIndices_;  // particle index -> Skeleton.joints の index
    std::vector<SphereCollider> colliders_;
    VerletParams                params_;
    VerletSolver                solver_;
    bool                        initialized_ = false;
};
