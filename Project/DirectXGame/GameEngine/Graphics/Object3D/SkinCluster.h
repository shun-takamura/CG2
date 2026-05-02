#pragma once
#include "Matrix4x4.h"
#include "VertexData.h"
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

// ComputeShaderに渡すSkinning情報
struct SkinningInformationForGPU {
    uint32_t numVertices;
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

    //=================================
    // ComputeShader版で利用するリソース
    //=================================
    // CS入力：頂点をStructuredBufferとして読むためのSRV
    Microsoft::WRL::ComPtr<ID3D12Resource> inputVertexResource;
    uint32_t inputVertexSrvIndex = 0;

    // CS入力：InfluenceをStructuredBufferとして読むためのSRV（influenceResourceを共用）
    uint32_t influenceSrvIndex = 0;

    // CS出力：Skinning結果のSkinnedVertex（UAV）。描画時はVBVとして使う
    Microsoft::WRL::ComPtr<ID3D12Resource> skinnedVertexResource;
    D3D12_VERTEX_BUFFER_VIEW skinnedVertexBufferView{};
    uint32_t skinnedVertexUavIndex = 0;
    // Barrier管理用：現在のResourceState
    D3D12_RESOURCE_STATES skinnedVertexState = D3D12_RESOURCE_STATE_COMMON;

    // CS用：頂点数情報のCBV
    Microsoft::WRL::ComPtr<ID3D12Resource> skinningInformationResource;
    SkinningInformationForGPU* mappedSkinningInformation = nullptr;

    // 描画用の頂点数（Dispatch計算に利用）
    uint32_t numVertices = 0;
};

// SkinClusterを生成する
SkinCluster CreateSkinCluster(
    DirectXCore* dxCore,
    SRVManager* srvManager,
    const Skeleton& skeleton,
    const ModelData& modelData);

// SkinClusterを毎フレーム更新する
void UpdateSkinCluster(SkinCluster& skinCluster, const Skeleton& skeleton);