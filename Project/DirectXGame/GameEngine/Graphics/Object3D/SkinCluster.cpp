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

    // ===== InverseBindPoseMatrixを単位行列で初期化 =====
    skinCluster.inverseBindPoseMatrices.resize(skeleton.joints.size());
    std::generate(
        skinCluster.inverseBindPoseMatrices.begin(),
        skinCluster.inverseBindPoseMatrices.end(),
        MakeIdentity4x4);

    //==========================================
    // ComputeShader版用のリソースをまとめて作成
    //==========================================
    const uint32_t numVertices = static_cast<uint32_t>(modelData.vertices.size());
    skinCluster.numVertices = numVertices;

    // --- CS入力：頂点用StructuredBuffer（ModelDataの頂点をコピー） ---
    skinCluster.inputVertexResource = dxCore->CreateBufferResource(
        sizeof(VertexData) * numVertices);
    {
        VertexData* mapped = nullptr;
        skinCluster.inputVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, modelData.vertices.data(), sizeof(VertexData) * numVertices);
        skinCluster.inputVertexResource->Unmap(0, nullptr);
    }
    skinCluster.inputVertexSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForStructuredBuffer(
        skinCluster.inputVertexSrvIndex,
        skinCluster.inputVertexResource.Get(),
        numVertices,
        sizeof(VertexData));

    // --- CS入力：influence用SRV（既存のinfluenceResourceを共用） ---
    skinCluster.influenceSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForStructuredBuffer(
        skinCluster.influenceSrvIndex,
        skinCluster.influenceResource.Get(),
        numVertices,
        sizeof(VertexInfluence));

    // --- CS出力：Skinning結果のUAV（DEFAULT heap、ALLOW_UNORDERED_ACCESS） ---
    skinCluster.skinnedVertexResource = dxCore->CreateUavBufferResource(
        sizeof(VertexData) * numVertices);
    skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;

    skinCluster.skinnedVertexUavIndex = srvManager->Allocate();
    srvManager->CreateUAVForStructuredBuffer(
        skinCluster.skinnedVertexUavIndex,
        skinCluster.skinnedVertexResource.Get(),
        numVertices,
        sizeof(VertexData));

    // 描画時にVBVとして使うので設定しておく
    skinCluster.skinnedVertexBufferView.BufferLocation =
        skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
    skinCluster.skinnedVertexBufferView.SizeInBytes =
        UINT(sizeof(VertexData) * numVertices);
    skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(VertexData);

    // --- CS用CBV：SkinningInformation ---
    skinCluster.skinningInformationResource = dxCore->CreateBufferResource(
        sizeof(SkinningInformationForGPU));
    skinCluster.skinningInformationResource->Map(
        0, nullptr, reinterpret_cast<void**>(&skinCluster.mappedSkinningInformation));
    skinCluster.mappedSkinningInformation->numVertices = numVertices;

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