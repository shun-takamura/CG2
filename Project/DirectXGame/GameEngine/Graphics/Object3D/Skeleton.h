#pragma once
#include "Matrix4x4.h"
#include "QuaternionTransform.h"
#include <vector>
#include <map>
#include <string>
#include <optional>
#include <cstdint>

// 前方宣言
struct Node;
struct Animation;

// 1本の骨
struct Joint {
    QuaternionTransform transform;          // SRT
    Matrix4x4 localMatrix;                  // ローカル行列
    Matrix4x4 skeletonSpaceMatrix;          // Skeleton空間への変換行列
    std::string name;                       // 名前
    std::vector<int32_t> children;          // 子のIndex
    int32_t index;                          // 自身のIndex
    std::optional<int32_t> parent;          // 親のIndex（無ければnullopt）
};

// 骨の集合
struct Skeleton {
    int32_t root;                           // RootJointのIndex
    std::map<std::string, int32_t> jointMap;// 名前→Indexの辞書
    std::vector<Joint> joints;              // 全Joint
};

// Nodeの階層からSkeletonを構築する
Skeleton CreateSkeleton(const Node& rootNode);

// 再帰でJointを作りjointsに登録する。自身のIndexを返す
int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent,
    std::vector<Joint>& joints);

// Skeleton全体のlocalMatrix・skeletonSpaceMatrixを更新する
void UpdateSkeleton(Skeleton& skeleton);

// SkeletonにAnimationを適用する（Joint.transformに値を流し込む）
void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime);