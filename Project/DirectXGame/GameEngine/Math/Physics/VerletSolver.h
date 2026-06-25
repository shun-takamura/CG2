#pragma once
#include <vector>
#include "Vector3.h"

// 質点1個（親への距離拘束つき）。Skeleton を一切知らない純粋な点。
// 揺れもの・チェーン・カメラ追従ラグなど、何にでも流用できる粒度に保つ。
struct VerletParticle {
    Vector3 location{ 0.0f, 0.0f, 0.0f };      // 現在位置（シミュ空間）
    Vector3 prevLocation{ 0.0f, 0.0f, 0.0f };  // 前ステップ位置（Verlet 速度算出用）
    Vector3 poseLocation{ 0.0f, 0.0f, 0.0f };  // 本来あるべき位置（stiffness で引き戻す先）
    float   boneLength = 0.0f;                  // 親との距離（長さ復元用）
    int     parentIndex = -1;                   // 親の index（-1 = ルート）
    bool    fixed = false;                      // kinematic 固定（ルート等）
};

// シミュレーション全体に効くパラメータ。1 構造体に集約して UI / セーブを楽にする。
struct VerletParams {
    Vector3 gravity{ 0.0f, -9.8f, 0.0f };
    float   damping    = 0.1f;   // 速度減衰 [0,1]
    float   stiffness  = 0.05f;  // ポーズへの引き戻し強さ [0,1]
    float   radius     = 1.0f;   // 各質点の衝突半径
    float   limitAngle = 0.0f;   // 親からの角度制限（度）。0 = 無制限
};

// 球コライダー（カプセル等は後続フェーズで追加）。
struct SphereCollider {
    Vector3 center{ 0.0f, 0.0f, 0.0f };
    float   radius = 0.0f;
};

// Verlet 積分ベースの汎用ソルバ。Skeleton にも ImGui にも依存しない純数学レイヤー。
class VerletSolver {
public:
    // 1 ステップ進める：積分 → stiffness → コリジョン → 角度制限 → 長さ復元。
    // 中身は Phase 2 で実装する。
    void Step(std::vector<VerletParticle>& particles,
              const VerletParams& params,
              const std::vector<SphereCollider>& colliders,
              float dt);
};
