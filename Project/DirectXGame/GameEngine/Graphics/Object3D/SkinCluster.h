#pragma once
#include "Matrix4x4.h"
#include <wrl.h>
#include <d3d12.h>
#include <span>
#include <vector>
#include <array>
#include <cstdint>

// 前方宣言
struct Skeleton;
struct ModelData;
class DirectXCore;
class SRVManager;

// 1頂点が影響を受けるJointの最大数
const uint32_t kNumMaxInfluence = 4;

// 頂点ごとのSkinning影響度（VBVに送る）
struct VertexInfluence {
    std::array<float, kNumMaxInfluence>   weights;
    std::array<int32_t, kNumMaxInfluence> jointIndices;
};

// GPUに送る1Joint分の行列セット
struct WellForGPU {
    Matrix4x4 skeletonSpaceMatrix;                 // 位置用
    Matrix4x4 skeletonSpaceInverseTransposeMatrix; // 法線用
};

// SkinCluster本体
struct SkinCluster {
    // CPU側で持つInverseBindPoseMatrix配列
    std::vector<Matrix4x4> inverseBindPoseMatrices;

    // VertexInfluence用GPUリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> influenceResource;
    D3D12_VERTEX_BUFFER_VIEW influenceBufferView{};
    std::span<VertexInfluence> mappedInfluence;

    // MatrixPalette用GPUリソース
    Microsoft::WRL::ComPtr<ID3D12Resource> paletteResource;
    std::span<WellForGPU> mappedPalette;
    uint32_t paletteSrvIndex = 0;
};

// SkinClusterを生成する
SkinCluster CreateSkinCluster(
    DirectXCore* dxCore,
    SRVManager* srvManager,
    const Skeleton& skeleton,
    const ModelData& modelData);

// SkinClusterを毎フレーム更新する
void UpdateSkinCluster(SkinCluster& skinCluster, const Skeleton& skeleton);