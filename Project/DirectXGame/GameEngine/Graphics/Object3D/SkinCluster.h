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
class AnimatedModelInstance;

// 1頂点が影響を受けるJointの最大数
const uint32_t kNumMaxInfluence = 4;

// 頂点ごとのSkinning影響度。
// レイアウトは .mesh の skin セクションと完全に一致させ、DStorage で直接 GPU に流せるようにする。
// 順番: jointIndices(int32×4) → weights(float×4)。HLSL 側 (Skinning.CS.hlsl) も同じ順。
struct VertexInfluence {
    std::array<int32_t, kNumMaxInfluence> jointIndices;
    std::array<float, kNumMaxInfluence>   weights;
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

    // skel-index → skeleton.joints index の remap テーブル。
    // 非空なら palette / IBM は .skel 順 (= .mesh の影響度 joint インデックスと一致) で
    // インデックスされ、UpdateSkinCluster はこの remap で skeleton 側の matrix を引く。
    // 空なら従来の skeleton.joints 順 indexing。
    std::vector<int32_t> jointSkelToSkeletonRemap;
};

/// <summary>
/// SkinClusterを生成する（パレット・影響度・入力頂点・出力頂点の SRV/UAV と頂点数の CB）。
/// model->UseDirectStorage() が true なら影響度バッファを pack から DStorage で直接ロードし、
/// 入力頂点バッファは AnimatedModelInstance 側のものを共用する。
/// </summary>
/// <param name="dxCore">デバイスの取得元</param>
/// <param name="srvManager">SRV/UAV を確保する先（解放は呼び出し側）</param>
/// <param name="skeleton">骨</param>
/// <param name="model">頂点と影響度の取り出し元</param>
/// <returns>生成した SkinCluster</returns>
SkinCluster CreateSkinCluster(
    DirectXCore* dxCore,
    SRVManager* srvManager,
    const Skeleton& skeleton,
    AnimatedModelInstance* model);

/// <summary>SkinClusterを毎フレーム更新する（骨の行列 × 逆バインド行列でパレットを書く）</summary>
/// <param name="skinCluster">更新する SkinCluster</param>
/// <param name="skeleton">今の姿勢の骨</param>
void UpdateSkinCluster(SkinCluster& skinCluster, const Skeleton& skeleton);