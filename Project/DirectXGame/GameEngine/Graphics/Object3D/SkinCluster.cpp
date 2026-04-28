#include "SkinCluster.h"
#include "Skeleton.h"
#include "ModelInstance.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "MathUtility.h"
#include <algorithm>
#include <cstring>

SkinCluster CreateSkinCluster(
    DirectXCore* dxCore,
    SRVManager* srvManager,
    const Skeleton& skeleton,
    const ModelData& modelData)
{
    SkinCluster skinCluster;

    // ===== palette用Resource =====
    skinCluster.paletteResource = dxCore->CreateBufferResource(
        sizeof(WellForGPU) * skeleton.joints.size());
    WellForGPU* mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
    skinCluster.mappedPalette = { mappedPalette, skeleton.joints.size() };

    // ===== palette用SRV（StructuredBuffer） =====
    skinCluster.paletteSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForStructuredBuffer(
        skinCluster.paletteSrvIndex,
        skinCluster.paletteResource.Get(),
        UINT(skeleton.joints.size()),
        sizeof(WellForGPU));

    // ===== influence用Resource =====
    skinCluster.influenceResource = dxCore->CreateBufferResource(
        sizeof(VertexInfluence) * modelData.vertices.size());
    VertexInfluence* mappedInfluence = nullptr;
    skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
    std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * modelData.vertices.size());
    skinCluster.mappedInfluence = { mappedInfluence, modelData.vertices.size() };

    // ===== influence用VBV =====
    skinCluster.influenceBufferView.BufferLocation =
        skinCluster.influenceResource->GetGPUVirtualAddress();
    skinCluster.influenceBufferView.SizeInBytes =
        UINT(sizeof(VertexInfluence) * modelData.vertices.size());
    skinCluster.influenceBufferView.StrideInBytes = sizeof(VertexInfluence);

    // ===== InverseBindPoseMatrixを単位行列で初期化 =====
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(
        skinCluster.inverseBindPoseMatrices.begin(),
        skinCluster.inverseBindPoseMatrices.end(),
        MakeIdentity4x4);

    // ===== ModelDataのskinClusterDataを解析してInfluenceを埋める =====
    for (const auto& jointWeight : modelData.skinClusterData) {
        auto it = skeleton.jointMap.find(jointWeight.first);
        if (it == skeleton.jointMap.end()) {
            continue; // skeletonに該当Jointなし
        }

        int32_t jointIndex = it->second;
        skinCluster.inverseBindPoseMatrices[jointIndex] = jointWeight.second.inverseBindPoseMatrix;

        for (const auto& vertexWeight : jointWeight.second.vertexWeights) {
            auto& currentInfluence = skinCluster.mappedInfluence[vertexWeight.vertexIndex];
            for (uint32_t index = 0; index < kNumMaxInfluence; ++index) {
                if (currentInfluence.weights[index] == 0.0f) {
                    currentInfluence.weights[index] = vertexWeight.weight;
                    currentInfluence.jointIndices[index] = jointIndex;
                    break;
                }
            }
        }
    }

    return skinCluster;
}

void UpdateSkinCluster(SkinCluster& skinCluster, const Skeleton& skeleton)
{
    for (size_t jointIndex = 0; jointIndex < skeleton.joints.size(); ++jointIndex) {
        // T_i = B_i^-1 * S_i
        skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix = Multiply(
            skinCluster.inverseBindPoseMatrices[jointIndex],
            skeleton.joints[jointIndex].skeletonSpaceMatrix);

        // 法線用：InverseTranspose
        skinCluster.mappedPalette[jointIndex].skeletonSpaceInverseTransposeMatrix =
            Transpose(Inverse(skinCluster.mappedPalette[jointIndex].skeletonSpaceMatrix));
    }
}