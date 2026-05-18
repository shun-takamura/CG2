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

void ApplyAnimationBlended(Skeleton& skeleton,
    const Animation& a, float timeA,
    const Animation& b, float timeB,
    float weight)
{
    // weight を [0, 1] に拘束
    if (weight < 0.0f) weight = 0.0f;
    if (weight > 1.0f) weight = 1.0f;

    for (Joint& joint : skeleton.joints) {
        // a 側: 無ければ現在の transform をそのまま使う
        QuaternionTransform va = joint.transform;
        if (auto it = a.nodeAnimations.find(joint.name); it != a.nodeAnimations.end()) {
            const NodeAnimation& na = it->second;
            va.translate = CalculateValue(na.translate.keyframes, timeA);
            va.rotate    = CalculateValue(na.rotate.keyframes, timeA);
            va.scale     = CalculateValue(na.scale.keyframes, timeA);
        }

        // b 側
        QuaternionTransform vb = va;  // a を初期値にしておけば、b 側に無いキーは a を維持
        if (auto it = b.nodeAnimations.find(joint.name); it != b.nodeAnimations.end()) {
            const NodeAnimation& nb = it->second;
            vb.translate = CalculateValue(nb.translate.keyframes, timeB);
            vb.rotate    = CalculateValue(nb.rotate.keyframes, timeB);
            vb.scale     = CalculateValue(nb.scale.keyframes, timeB);
        }

        joint.transform.translate = Lerp(va.translate, vb.translate, weight);
        joint.transform.rotate    = Slerp(va.rotate,    vb.rotate,    weight);
        joint.transform.scale     = Lerp(va.scale,     vb.scale,     weight);
    }
}