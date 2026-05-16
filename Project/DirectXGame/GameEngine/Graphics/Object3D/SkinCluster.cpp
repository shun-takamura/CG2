#include "SkinCluster.h"
#include "Skeleton.h"
#include "ModelInstance.h"
#include "AnimatedModelInstance.h"
#include "DirectXCore.h"
#include "SRVManager.h"
#include "MathUtility.h"
#include "AssetLocator.h"
#include "DStorageManager.h"
#include <algorithm>
#include <cstring>
#include <cassert>

SkinCluster CreateSkinCluster(
    DirectXCore* dxCore,
    SRVManager* srvManager,
    const Skeleton& skeleton,
    AnimatedModelInstance* model)
{
    assert(model);
    SkinCluster skinCluster;

    const ModelData& modelData = model->GetModelData();
    const bool useDS = model->UseDirectStorage();
    const uint32_t numVertices = useDS
        ? model->GetVertexCount()
        : static_cast<uint32_t>(modelData.vertices.size());
    skinCluster.numVertices = numVertices;

    // DStorage 経路では palette / IBM を .skel 順にして影響度バッファ (.mesh の skin
    // セクション = .skel index 参照) と一致させる。skeleton.joints は CreateSkeleton の
    // DFS で別順になるため、skel-index → skeleton-index の remap を作って毎フレームの
    // palette 更新時に lookup する。
    const auto& jointNames = model->GetJointNames();
    if (useDS && !jointNames.empty()) {
        skinCluster.jointSkelToSkeletonRemap.resize(jointNames.size(), 0);
        for (size_t i = 0; i < jointNames.size(); ++i) {
            auto it = skeleton.jointMap.find(jointNames[i]);
            skinCluster.jointSkelToSkeletonRemap[i] =
                (it != skeleton.jointMap.end()) ? it->second : 0;
        }
    }
    const size_t paletteCount = !skinCluster.jointSkelToSkeletonRemap.empty()
        ? skinCluster.jointSkelToSkeletonRemap.size()
        : skeleton.joints.size();

    // ===== palette用Resource (UPLOAD heap, 毎フレーム CPU から書く) =====
    skinCluster.paletteResource = dxCore->CreateBufferResource(
        sizeof(WellForGPU) * paletteCount);
    WellForGPU* mappedPalette = nullptr;
    skinCluster.paletteResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedPalette));
    skinCluster.mappedPalette = { mappedPalette, paletteCount };

    skinCluster.paletteSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForStructuredBuffer(
        skinCluster.paletteSrvIndex,
        skinCluster.paletteResource.Get(),
        UINT(paletteCount),
        sizeof(WellForGPU));

    // ===== InverseBindPoseMatrix を初期化 (palette と同じ size) =====
    skinCluster.inverseBindPoseMatrices.resize(paletteCount);
    std::generate(
        skinCluster.inverseBindPoseMatrices.begin(),
        skinCluster.inverseBindPoseMatrices.end(),
        MakeIdentity4x4);

    // ===== influence Resource =====
    if (useDS) {
        // pack 内 .mesh の skin セクションを DStorage で DEFAULT heap に直接ロード。
        // .mesh の skin レイアウトは VertexInfluence と一致 (jointIndices[4], weights[4])。
        const size_t infBytes = sizeof(VertexInfluence) * numVertices;
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = infBytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&skinCluster.influenceResource));
        assert(SUCCEEDED(hr));

        if (model->HasSkinning()) {
            uint64_t packOffset = 0, packSize = 0;
            AssetLocator::GetInstance()->GetPackEntryInfo(
                model->GetMeshFilePath(), packOffset, packSize);
            auto* ds = DStorageManager::GetInstance();
            ds->EnqueueBufferRead(ds->GetPackFile(),
                packOffset + model->GetSkinFileOffset(),
                static_cast<uint32_t>(infBytes),
                skinCluster.influenceResource.Get(), 0);
            ds->SubmitAndWait();
        }
        // mappedInfluence は DStorage 経路では使えない（DEFAULT heap）
        skinCluster.mappedInfluence = {};
    } else {
        skinCluster.influenceResource = dxCore->CreateBufferResource(
            sizeof(VertexInfluence) * numVertices);
        VertexInfluence* mappedInfluence = nullptr;
        skinCluster.influenceResource->Map(0, nullptr, reinterpret_cast<void**>(&mappedInfluence));
        std::memset(mappedInfluence, 0, sizeof(VertexInfluence) * numVertices);
        skinCluster.mappedInfluence = { mappedInfluence, numVertices };
    }

    skinCluster.influenceSrvIndex = srvManager->Allocate();
    srvManager->CreateSRVForStructuredBuffer(
        skinCluster.influenceSrvIndex,
        skinCluster.influenceResource.Get(),
        numVertices,
        sizeof(VertexInfluence));

    // ===== CS 入力: 頂点 SRV =====
    if (useDS) {
        // AnimatedModelInstance の vertexResource_ をそのまま SRV として共用
        // (DStorage 経路では DEFAULT heap 上にあり、暗黙プロモーションで CS 読み可)
        skinCluster.inputVertexResource = nullptr;  // 所有しない（共用）
        skinCluster.inputVertexSrvIndex = srvManager->Allocate();
        srvManager->CreateSRVForStructuredBuffer(
            skinCluster.inputVertexSrvIndex,
            model->GetVertexResource(),
            numVertices,
            sizeof(VertexData));
    } else {
        skinCluster.inputVertexResource = dxCore->CreateBufferResource(
            sizeof(VertexData) * numVertices);
        VertexData* mapped = nullptr;
        skinCluster.inputVertexResource->Map(0, nullptr, reinterpret_cast<void**>(&mapped));
        std::memcpy(mapped, modelData.vertices.data(), sizeof(VertexData) * numVertices);
        skinCluster.inputVertexResource->Unmap(0, nullptr);

        skinCluster.inputVertexSrvIndex = srvManager->Allocate();
        srvManager->CreateSRVForStructuredBuffer(
            skinCluster.inputVertexSrvIndex,
            skinCluster.inputVertexResource.Get(),
            numVertices,
            sizeof(VertexData));
    }

    // ===== CS 出力: Skinning 結果 UAV =====
    skinCluster.skinnedVertexResource = dxCore->CreateUavBufferResource(
        sizeof(VertexData) * numVertices);
    skinCluster.skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;

    skinCluster.skinnedVertexUavIndex = srvManager->Allocate();
    srvManager->CreateUAVForStructuredBuffer(
        skinCluster.skinnedVertexUavIndex,
        skinCluster.skinnedVertexResource.Get(),
        numVertices,
        sizeof(VertexData));

    skinCluster.skinnedVertexBufferView.BufferLocation =
        skinCluster.skinnedVertexResource->GetGPUVirtualAddress();
    skinCluster.skinnedVertexBufferView.SizeInBytes =
        UINT(sizeof(VertexData) * numVertices);
    skinCluster.skinnedVertexBufferView.StrideInBytes = sizeof(VertexData);

    // ===== CS 用 CBV: SkinningInformation =====
    skinCluster.skinningInformationResource = dxCore->CreateBufferResource(
        sizeof(SkinningInformationForGPU));
    skinCluster.skinningInformationResource->Map(
        0, nullptr, reinterpret_cast<void**>(&skinCluster.mappedSkinningInformation));
    skinCluster.mappedSkinningInformation->numVertices = numVertices;

    // ===== Inverse Bind Pose Matrix の充填 =====
    if (useDS) {
        // .skel から取得済みの IBM 配列を joint-index 順にコピー
        const auto& ibms = model->GetJointInverseBindMatrices();
        const size_t n = std::min(ibms.size(), skinCluster.inverseBindPoseMatrices.size());
        for (size_t i = 0; i < n; ++i) {
            skinCluster.inverseBindPoseMatrices[i] = ibms[i];
        }
    } else {
        // 既存パス: skinClusterData (joint→weights) を per-vertex Influence に展開
        for (const auto& jointWeight : modelData.skinClusterData) {
            auto it = skeleton.jointMap.find(jointWeight.first);
            if (it == skeleton.jointMap.end()) continue;

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
    }

    return skinCluster;
}

void UpdateSkinCluster(SkinCluster& skinCluster, const Skeleton& skeleton)
{
    const auto& remap = skinCluster.jointSkelToSkeletonRemap;
    const size_t count = !remap.empty() ? remap.size() : skeleton.joints.size();

    for (size_t i = 0; i < count; ++i) {
        // remap がある場合は palette[i] が .skel index i に対応し、
        // 対応する skeleton.joints のアニメ済み skeletonSpaceMatrix を引いてくる。
        const size_t skeletonIdx = !remap.empty() ? static_cast<size_t>(remap[i]) : i;
        if (skeletonIdx >= skeleton.joints.size()) continue;

        // T_i = B_i^-1 * S_i
        skinCluster.mappedPalette[i].skeletonSpaceMatrix = Multiply(
            skinCluster.inverseBindPoseMatrices[i],
            skeleton.joints[skeletonIdx].skeletonSpaceMatrix);

        // 法線用：InverseTranspose
        skinCluster.mappedPalette[i].skeletonSpaceInverseTransposeMatrix =
            Transpose(Inverse(skinCluster.mappedPalette[i].skeletonSpaceMatrix));
    }
}