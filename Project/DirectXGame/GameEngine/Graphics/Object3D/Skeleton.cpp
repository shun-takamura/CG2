#include "Skeleton.h"
#include "ModelInstance.h"  // Node の定義
#include "Animation.h"      // Animation, NodeAnimation, CalculateValue
#include "MathUtility.h"
#include "Quaternion.h"

Skeleton CreateSkeleton(const Node& rootNode) {
    Skeleton skeleton;
    skeleton.root = CreateJoint(rootNode, std::nullopt, skeleton.joints);

    // 名前 → Index のマップを作る
    for (const Joint& joint : skeleton.joints) {
        skeleton.jointMap.emplace(joint.name, joint.index);
    }

    // 初期状態を更新しておく
    UpdateSkeleton(skeleton);
    return skeleton;
}

int32_t CreateJoint(const Node& node, const std::optional<int32_t>& parent,
    std::vector<Joint>& joints) {
    Joint joint;
    joint.name = node.name;
    joint.localMatrix = node.localMatrix;
    joint.skeletonSpaceMatrix = MakeIdentity4x4();
    joint.transform = node.transform;
    joint.index = static_cast<int32_t>(joints.size()); // 現在の末尾Index
    joint.parent = parent;
    joints.push_back(joint);

    // 子Jointを再帰的に作る
    for (const Node& child : node.children) {
        int32_t childIndex = CreateJoint(child, joint.index, joints);
        joints[joint.index].children.push_back(childIndex);
    }

    return joint.index;
}

void UpdateSkeleton(Skeleton& skeleton) {
    // 親が必ず先に来るように構築されているため、配列を先頭から舐めるだけでOK
    for (Joint& joint : skeleton.joints) {
        joint.localMatrix = MakeAffineMatrix(
            joint.transform.scale,
            joint.transform.rotate,
            joint.transform.translate);

        if (joint.parent) {
            // 親があれば 自分のlocal × 親のskeletonSpace
            joint.skeletonSpaceMatrix = Multiply(
                joint.localMatrix,
                skeleton.joints[*joint.parent].skeletonSpaceMatrix);
        } else {
            // 親がなければ skeletonSpaceMatrix = localMatrix
            joint.skeletonSpaceMatrix = joint.localMatrix;
        }
    }
}

void ApplyAnimation(Skeleton& skeleton, const Animation& animation, float animationTime) {
    for (Joint& joint : skeleton.joints) {
        // C++17の初期化付きif
        if (auto it = animation.nodeAnimations.find(joint.name);
            it != animation.nodeAnimations.end()) {
            const NodeAnimation& nodeAnim = it->second;
            joint.transform.translate = CalculateValue(nodeAnim.translate.keyframes, animationTime);
            joint.transform.rotate = CalculateValue(nodeAnim.rotate.keyframes, animationTime);
            joint.transform.scale = CalculateValue(nodeAnim.scale.keyframes, animationTime);
        }
    }
}