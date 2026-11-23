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
    /// <summary>rootBoneName 配下のジョイントを収集して質点列を構築する（Phase 1 で実装。今は params を覚えるだけ）</summary>
    /// <param name="skeleton">対象の骨</param>
    /// <param name="rootBoneName">揺らす範囲のルートのジョイント名</param>
    /// <param name="params">揺れの調整値</param>
    void Initialize(const Skeleton& skeleton, const std::string& rootBoneName, const VerletParams& params);

    /// <summary>
    /// 揺れを計算し、結果を joint.transform.rotate に書き戻す（Phase 2-3 で実装。今は何もしない）。
    /// UpdateSkeleton() の後・スキニングの前に呼ぶ。
    /// </summary>
    /// <param name="skeleton">書き戻す先の骨</param>
    /// <param name="dt">経過時間 [秒]</param>
    void Update(Skeleton& skeleton, float dt);

    /// <summary>質点が入り込まない球のコライダーを足す</summary>
    /// <param name="collider">球のコライダー</param>
    void AddSphereCollider(const SphereCollider& collider) { colliders_.push_back(collider); }

    /// <summary>揺れの調整値（書き換える時はコピーして SetParams で戻す）</summary>
    /// <returns>今の値</returns>
    const VerletParams& Params() const { return params_; }
    /// <summary>揺れの調整値をまとめて差し替える</summary>
    /// <param name="value">新しい値</param>
    void SetParams(const VerletParams& value) { params_ = value; }

#ifdef USE_IMGUI
    /// <summary>
    /// 親オブジェクトの Inspector から呼ぶ調整 UI（Phase 7 で実装。今は空）。
    /// SpringBone はモデルのサブ機能なので IImGuiEditable は継承せず、
    /// オーナー側の OnImGuiInspector から本メソッドを呼ぶ形にする。
    /// </summary>
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
