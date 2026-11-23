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

/// <summary>Nodeの階層からSkeletonを構築し、名前→Index の辞書を作って一度 UpdateSkeleton する</summary>
/// <param name="rootNode">ルートの Node</param>
/// <returns>構築した Skeleton</returns>
Skeleton CreateSkeleton(const Node& rootNode);

/// <summary>再帰でJointを作りjointsに登録する</summary>
/// <param name="node">Joint にする Node（子も再帰で登録）</param>
/// <param name="parent">親の Index（ルートは nullopt）</param>
/// <param name="joints">登録先</param>
/// <returns>自身のIndex</returns>
int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent,
    std::vector<Joint>& joints);

/// <summary>Skeleton全体のlocalMatrix・skeletonSpaceMatrixを更新する（親が先に並んでいる前提）</summary>
/// <param name="skeleton">更新する Skeleton</param>
void UpdateSkeleton(Skeleton& skeleton);

/// <summary>SkeletonにAnimationを適用する（名前の合う Joint.transform に値を流し込む）</summary>
/// <param name="skeleton">適用先</param>
/// <param name="animation">アニメーション</param>
/// <param name="animationTime">時刻 [秒]</param>
void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime);

/// <summary>
/// 2つのアニメーションをブレンドして適用する（クロスフェード用）。
/// weight: 0.0 で a そのまま、1.0 で b そのまま。translate/scale は Lerp、rotate は Slerp。
/// </summary>
/// <param name="skeleton">適用先</param>
/// <param name="a">フェード元のアニメーション</param>
/// <param name="timeA">a の時刻 [秒]</param>
/// <param name="b">フェード先のアニメーション</param>
/// <param name="timeB">b の時刻 [秒]</param>
/// <param name="weight">b の重み（0〜1）</param>
void ApplyAnimationBlended(Skeleton& skeleton,
    const Animation& a, float timeA,
    const Animation& b, float timeB,
    float weight);