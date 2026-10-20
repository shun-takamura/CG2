#include "AnimatedMeshCache.h"
#include "DirectXCore.h"
#include "AssetLocator.h"
#include "DStorageManager.h"
#include "SkinCluster.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include <cassert>
#include <cstring>
#include <filesystem>

// =====================================================================
// .mesh / .skel / .mat / .anim の読み込み（Cooker v2 形式。AnimatedModelInstance::LoadModelV2 から移した）
// =====================================================================

namespace {

// .mat から base_color テクスチャパスを取り出すヘルパー
std::string ReadMatBaseColorPath_V2(const std::string& matPath)
{
    auto h = AssetLocator::GetInstance()->Open(matPath);
    if (!h.IsValid()) return {};
    char magic[4]{};
    h.Read(magic, 4);
    if (std::memcmp(magic, "MATL", 4) != 0) return {};
    uint32_t version = 0;
    h.Read(&version, 4);
    // base_color_path は version 直後で全バージョン共通オフセット。v1〜v5 を許容
    if (version < 1 || version > 5) return {};
    char path[256]{};
    h.Read(path, 256);
    return std::string(path);
}

// .mat v3 から normal_map_path を取り出すヘルパー（固定オフセット 308）
std::string ReadMatNormalMapPath_V2(const std::string& matPath)
{
    auto h = AssetLocator::GetInstance()->Open(matPath);
    if (!h.IsValid()) return {};
    char magic[4]{};
    h.Read(magic, 4);
    if (std::memcmp(magic, "MATL", 4) != 0) return {};
    uint32_t version = 0;
    h.Read(&version, 4);
    if (version < 3) return {};  // v3 から normal_map_path
    h.Seek(308);
    char path[256]{};
    h.Read(path, 256);
    return std::string(path);
}

// .skel の Joint 1 個ぶん（172 バイト）
struct SkelJoint {
    char       name[64];
    int32_t    parent_index;
    Matrix4x4  inverse_bind_matrix;
    Vector3    bind_translation;
    Vector4    bind_rotation;
    Vector3    bind_scale;
};

// .skel を読んでフラットなジョイント配列を返す。失敗時は空。
std::vector<SkelJoint> ReadSkelFile(const std::string& skelPath)
{
    std::vector<SkelJoint> joints;
    auto h = AssetLocator::GetInstance()->Open(skelPath);
    if (!h.IsValid()) return joints;

    char magic[4]{};
    h.Read(magic, 4);
    if (std::memcmp(magic, "SKEL", 4) != 0) return joints;
    uint32_t version = 0, jointCount = 0, reserved = 0;
    h.Read(&version, 4);
    h.Read(&jointCount, 4);
    h.Read(&reserved, 4);
    if (version != 1) return joints;

    joints.resize(jointCount);
    h.Read(joints.data(), static_cast<size_t>(jointCount) * sizeof(SkelJoint));
    return joints;
}

// フラットなジョイント配列から Node 階層を組み立てる。
// .skel はトポロジカル順序（親 index < 自 index）と仮定する。
Node BuildNodeTreeFromSkel(const std::vector<SkelJoint>& joints)
{
    if (joints.empty()) {
        Node empty;
        empty.localMatrix = MakeIdentity4x4();
        return empty;
    }

    // まず全 Node を作って中身を埋める
    std::vector<Node> tempNodes(joints.size());
    for (size_t i = 0; i < joints.size(); ++i) {
        const auto& j = joints[i];
        tempNodes[i].name = std::string(j.name);
        tempNodes[i].transform.translate = j.bind_translation;
        tempNodes[i].transform.rotate = { j.bind_rotation.x, j.bind_rotation.y,
                                          j.bind_rotation.z, j.bind_rotation.w };
        tempNodes[i].transform.scale = j.bind_scale;
        tempNodes[i].localMatrix = MakeAffineMatrix(
            tempNodes[i].transform.scale,
            tempNodes[i].transform.rotate,
            tempNodes[i].transform.translate);
    }

    // 逆順（大きい index = 子側）から親の children へ移す
    // → トポロジカル順序を仮定すれば、ある index を処理するときその子は既に
    //   自分の中に集約済みになっている
    int rootIdx = -1;
    for (int i = static_cast<int>(joints.size()) - 1; i >= 0; --i) {
        int parent = joints[i].parent_index;
        if (parent < 0) {
            rootIdx = i;
        } else {
            tempNodes[parent].children.push_back(std::move(tempNodes[i]));
        }
    }

    if (rootIdx < 0) rootIdx = 0; // フォールバック
    return std::move(tempNodes[rootIdx]);
}

// .anim を読んで Animation を構築する
Animation ReadAnimFile(const std::string& animPath)
{
    Animation anim{};
    anim.duration = 0.0f;
    auto h = AssetLocator::GetInstance()->Open(animPath);
    if (!h.IsValid()) return anim;

    char magic[4]{};
    h.Read(magic, 4);
    if (std::memcmp(magic, "ANIM", 4) != 0) return anim;
    uint32_t version = 0, channelCount = 0, channelsOffset = 0, reserved = 0;
    float duration = 0.0f;
    h.Read(&version, 4);
    h.Read(&duration, 4);
    h.Read(&channelCount, 4);
    h.Read(&channelsOffset, 4);
    h.Read(&reserved, 4);
    if (version != 1) return anim;

    anim.duration = duration;

    // Channel テーブルを全部読み込む
    struct ChannelHeader {
        char     joint_name[64];
        uint32_t t_count, r_count, s_count;
        uint32_t t_offset, r_offset, s_offset;
    };
    std::vector<ChannelHeader> channelHeaders(channelCount);
    h.Seek(channelsOffset);
    h.Read(channelHeaders.data(), static_cast<size_t>(channelCount) * sizeof(ChannelHeader));

    // 各チャンネルのキーフレームを読み込む
    for (const auto& ch : channelHeaders) {
        std::string jointName(ch.joint_name);
        NodeAnimation& na = anim.nodeAnimations[jointName];

        na.translate.keyframes.resize(ch.t_count);
        h.Seek(ch.t_offset);
        for (uint32_t i = 0; i < ch.t_count; ++i) {
            float t; Vector3 v;
            h.Read(&t, 4);
            h.Read(&v, sizeof(Vector3));
            na.translate.keyframes[i] = { t, v };
        }
        na.rotate.keyframes.resize(ch.r_count);
        h.Seek(ch.r_offset);
        for (uint32_t i = 0; i < ch.r_count; ++i) {
            float t; float q[4];
            h.Read(&t, 4);
            h.Read(q, sizeof(q));
            na.rotate.keyframes[i] = { t, Quaternion(q[0], q[1], q[2], q[3]) };
        }
        na.scale.keyframes.resize(ch.s_count);
        h.Seek(ch.s_offset);
        for (uint32_t i = 0; i < ch.s_count; ++i) {
            float t; Vector3 v;
            h.Read(&t, 4);
            h.Read(&v, sizeof(Vector3));
            na.scale.keyframes[i] = { t, v };
        }
    }
    return anim;
}

// DEFAULT heap・COMMON のバッファ（DirectStorage の書き込み先）
Microsoft::WRL::ComPtr<ID3D12Resource> CreateDefaultBuffer(DirectXCore* dxCore, size_t sizeInBytes)
{
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    desc.Width = sizeInBytes;
    desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
    desc.SampleDesc.Count = 1;
    desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    Microsoft::WRL::ComPtr<ID3D12Resource> resource;
    HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
        &heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr,
        IID_PPV_ARGS(&resource));
    assert(SUCCEEDED(hr));
    return resource;
}

} // anonymous namespace

// =====================================================================

AnimatedMeshCache* AnimatedMeshCache::GetInstance()
{
    static AnimatedMeshCache instance;
    return &instance;
}

void AnimatedMeshCache::Finalize()
{
    std::lock_guard<std::mutex> lock(mutex_);
    meshes_.clear();
    animations_.clear();
}

std::shared_ptr<const AnimatedMeshData> AnimatedMeshCache::Acquire(
    const std::string& directoryPath, const std::string& filename, DirectXCore* dxCore)
{
    if (meshObserver_) meshObserver_(directoryPath, filename);

    const std::string key = directoryPath + "/" + filename;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = meshes_.find(key); it != meshes_.end()) return it->second;
    }

    std::shared_ptr<AnimatedMeshData> data = LoadCPU(directoryPath, filename);
    if (!data) return nullptr;
    CreateGPU(*data, dxCore, true);

    std::lock_guard<std::mutex> lock(mutex_);
    return meshes_.emplace(key, std::move(data)).first->second;
}

bool AnimatedMeshCache::Contains(const std::string& directoryPath, const std::string& filename) const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return meshes_.contains(directoryPath + "/" + filename);
}

void AnimatedMeshCache::Insert(std::shared_ptr<AnimatedMeshData> data)
{
    if (!data) return;
    std::lock_guard<std::mutex> lock(mutex_);
    meshes_.emplace(data->key, std::move(data));
}

std::shared_ptr<const Animation> AnimatedMeshCache::GetAnimation(const std::string& animPath)
{
    if (animPath.empty()) return nullptr;
    if (animObserver_) animObserver_(animPath);
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (auto it = animations_.find(animPath); it != animations_.end()) return it->second;
    }
    if (!AssetLocator::GetInstance()->Exists(animPath)) return nullptr;
    auto clip = std::make_shared<const Animation>(ReadAnimFile(animPath));
    std::lock_guard<std::mutex> lock(mutex_);
    return animations_.emplace(animPath, std::move(clip)).first->second;
}

std::shared_ptr<AnimatedMeshData> AnimatedMeshCache::LoadCPU(const std::string& directoryPath, const std::string& filename)
{
    const std::string meshPath = directoryPath + "/" + filename;
    auto h = AssetLocator::GetInstance()->Open(meshPath);
    if (!h.IsValid()) return nullptr;  // 呼び出し側（Acquire の使い手）が assert する。先読みのマニフェストが古いこともある

    char magic[4]{};
    h.Read(magic, 4);
    if (std::memcmp(magic, "MESH", 4) != 0) return nullptr;

    uint32_t version = 0, flags = 0;
    uint32_t vertexCount = 0, indexCount = 0, submeshCount = 0;
    uint32_t vertexOffset = 0, indexOffset = 0, skinOffset = 0, submeshOffset = 0;
    h.Read(&version, 4);
    h.Read(&flags, 4);
    h.Read(&vertexCount, 4);
    h.Read(&indexCount, 4);
    h.Read(&submeshCount, 4);
    h.Read(&vertexOffset, 4);
    h.Read(&indexOffset, 4);
    h.Read(&skinOffset, 4);
    h.Read(&submeshOffset, 4);
    assert((version == 3 || version == 4) && "expected .mesh v3 / v4");

    auto data = std::make_shared<AnimatedMeshData>();
    data->key = meshPath;

    char skeletonPathBuf[256]{};
    h.Read(skeletonPathBuf, 256);
    std::string skeletonPath(skeletonPathBuf);

    // v4 はヘッダ末尾に包む球（バインドポーズ）。データの位置は各 offset で引くので v3 と同じ読み方で済む
    if (version >= 4) {
        float sphere[4]{};
        h.Read(sphere, sizeof(sphere));
        data->bounds.center = { sphere[0], sphere[1], sphere[2] };
        data->bounds.radius = sphere[3];
    }

    const bool hasSkinning = (flags & 0x1) != 0;

    // GPU フェーズ / SkinCluster で使うメタ情報を保持
    data->meshFilePath     = meshPath;
    data->vertexFileOffset = vertexOffset;
    data->vertexCount      = vertexCount;
    data->indexFileOffset  = indexOffset;
    data->indexCount       = indexCount;
    data->skinFileOffset   = skinOffset;
    data->hasSkinning      = hasSkinning;

    // pack モード + DStorage 利用可能なら CPU 経由をスキップして VRAM 直行
    auto* loc = AssetLocator::GetInstance();
    auto* ds  = DStorageManager::GetInstance();
    data->useDirectStorage = loc->IsPackMode() && ds->IsInitialized() && ds->GetPackFile();

    ModelData& modelData = data->modelData;
    if (!data->useDirectStorage) {
        // --- 頂点 ---
        modelData.vertices.resize(vertexCount);
        h.ReadAt(vertexOffset, modelData.vertices.data(),
                 static_cast<size_t>(vertexCount) * sizeof(VertexData));

        // --- インデックス ---
        modelData.indices.resize(indexCount);
        h.ReadAt(indexOffset, modelData.indices.data(),
                 static_cast<size_t>(indexCount) * sizeof(uint32_t));
    }

    // --- .skel を読んで rootNode を構築 ---
    std::vector<SkelJoint> skelJoints;
    if (!skeletonPath.empty()) {
        skelJoints = ReadSkelFile(skeletonPath);
    }
    modelData.rootNode = BuildNodeTreeFromSkel(skelJoints);
    if (skelJoints.empty()) {
        modelData.rootNode.localMatrix = MakeIdentity4x4();
    }

    // .skel の Inverse Bind Pose Matrix と名前を joint-index 順で保持
    // （DStorage 経路では SkinCluster がここから直接拾い、skel-index → skeleton-index
    //   の remap を組み立てる）
    data->jointInverseBindMatrices.reserve(skelJoints.size());
    data->jointNames.reserve(skelJoints.size());
    for (const auto& j : skelJoints) {
        data->jointInverseBindMatrices.push_back(j.inverse_bind_matrix);
        data->jointNames.emplace_back(j.name);
    }

    // --- スキニングデータを per-vertex から per-joint (skinClusterData) に展開 ---
    // DStorage 経路ではこの中間表現は不要（SkinCluster が pack の skin セクションを直接読む）
    if (!data->useDirectStorage && hasSkinning && !skelJoints.empty()) {
        // 一頂点あたり (joint_indices[4], weights[4]) = 32 バイト
        struct SkinVertex {
            uint32_t joint_indices[4];
            float    weights[4];
        };
        std::vector<SkinVertex> skinData(vertexCount);
        h.ReadAt(skinOffset, skinData.data(),
                 static_cast<size_t>(vertexCount) * sizeof(SkinVertex));

        for (uint32_t v = 0; v < vertexCount; ++v) {
            for (int k = 0; k < 4; ++k) {
                float w = skinData[v].weights[k];
                if (w <= 0.0f) continue;
                uint32_t jointIdx = skinData[v].joint_indices[k];
                if (jointIdx >= skelJoints.size()) continue;
                const std::string jointName(skelJoints[jointIdx].name);
                auto& jwd = modelData.skinClusterData[jointName];
                jwd.inverseBindPoseMatrix = skelJoints[jointIdx].inverse_bind_matrix;
                jwd.vertexWeights.push_back({ w, v });
            }
        }
    }

    // --- 全 submesh を読み、部位別マテリアルとして保持する ---
    if (submeshCount > 0) {
        h.Seek(submeshOffset);
        data->submeshes.reserve(submeshCount);
        for (uint32_t i = 0; i < submeshCount; ++i) {
            uint32_t idxStart = 0, idxCount = 0;
            h.Read(&idxStart, 4);
            h.Read(&idxCount, 4);
            char matPath[256]{};
            h.Read(matPath, 256);

            RenderSubmesh sm;
            sm.indexStart = idxStart;
            sm.indexCount = idxCount;
            sm.matFilePath = std::string(matPath);
            sm.textureFilePath = ReadMatBaseColorPath_V2(sm.matFilePath);
            sm.normalMapFilePath = ReadMatNormalMapPath_V2(sm.matFilePath);
            data->submeshes.push_back(std::move(sm));
        }

        // 後方互換: submesh[0] を既存メンバへ反映
        data->matFilePath = data->submeshes[0].matFilePath;
        modelData.materialData.textureFilePath = data->submeshes[0].textureFilePath;
        modelData.materialData.normalMapFilePath = data->submeshes[0].normalMapFilePath;
    }

    // --- 同階層の .anim を探して読み込む（あれば）---
    // 注意: pack モードでの hash 一致のため、forward slash で組み立てる
    // （filesystem::path::string() は Windows でバックスラッシュを返すため使えない）
    std::filesystem::path animPath =
        std::filesystem::path(directoryPath) /
        (std::filesystem::path(filename).stem().string() + ".anim");
    const std::string animPathStr = animPath.generic_string();
    if (auto clip = GetInstance()->GetAnimation(animPathStr)) {
        data->animation = *clip;
        data->animationPath = animPathStr;
    }

    if (!data->bounds.IsValid() && !modelData.vertices.empty()) {
        data->bounds = ComputeBoundingSphere(modelData.vertices);
    }

    assert(indexCount > 0 && "indexCount is 0!");
    return data;
}

bool AnimatedMeshCache::CreateGPU(AnimatedMeshData& data, DirectXCore* dxCore, bool wait)
{
    const size_t vertexBytes = sizeof(VertexData) * data.vertexCount;
    const size_t indexBytes = sizeof(uint32_t) * data.indexCount;

    if (data.useDirectStorage) {
        // DEFAULT heap に COMMON state でバッファ作成。SkinCluster の CS が SRV として
        // 読み、bind-pose Draw 経路では VBV としても使える（暗黙プロモーションで対応）。
        uint64_t packOffset = 0, packSize = 0;
        AssetLocator::GetInstance()->GetPackEntryInfo(data.meshFilePath, packOffset, packSize);
        auto* ds = DStorageManager::GetInstance();

        data.vertexResource = CreateDefaultBuffer(dxCore, vertexBytes);
        ds->EnqueueBufferRead(ds->GetPackFile(), packOffset + data.vertexFileOffset,
            static_cast<uint32_t>(vertexBytes), data.vertexResource.Get(), 0);

        data.indexResource = CreateDefaultBuffer(dxCore, indexBytes);
        ds->EnqueueBufferRead(ds->GetPackFile(), packOffset + data.indexFileOffset,
            static_cast<uint32_t>(indexBytes), data.indexResource.Get(), 0);

        // スキンの影響度（.mesh の skin セクション = VertexInfluence と同じ並び）。骨の並びに依らないので共有できる
        const size_t infBytes = sizeof(VertexInfluence) * data.vertexCount;
        data.influenceResource = CreateDefaultBuffer(dxCore, infBytes);
        if (data.hasSkinning) {
            ds->EnqueueBufferRead(ds->GetPackFile(), packOffset + data.skinFileOffset,
                static_cast<uint32_t>(infBytes), data.influenceResource.Get(), 0);
        }
    } else {
        // 既存パス: UPLOAD heap + Map + memcpy（CPU から書き込む）
        data.vertexResource = dxCore->CreateBufferResource(vertexBytes);
        VertexData* vertexData = nullptr;
        data.vertexResource->Map(0, nullptr, reinterpret_cast<void**>(&vertexData));
        std::memcpy(vertexData, data.modelData.vertices.data(), vertexBytes);
        data.vertexResource->Unmap(0, nullptr);

        data.indexResource = dxCore->CreateBufferResource(indexBytes);
        uint32_t* indexData = nullptr;
        data.indexResource->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        std::memcpy(indexData, data.modelData.indices.data(), indexBytes);
        data.indexResource->Unmap(0, nullptr);
    }

    data.vertexBufferView.BufferLocation = data.vertexResource->GetGPUVirtualAddress();
    data.vertexBufferView.SizeInBytes = UINT(vertexBytes);
    data.vertexBufferView.StrideInBytes = sizeof(VertexData);
    data.indexBufferView.BufferLocation = data.indexResource->GetGPUVirtualAddress();
    data.indexBufferView.SizeInBytes = UINT(indexBytes);
    data.indexBufferView.Format = DXGI_FORMAT_R32_UINT;

    if (!data.useDirectStorage) return false;
    if (!wait) return true;
    // シーンの読み込みのバッチ中は no-op（EndBatchAndWait でまとめて待つ。バリアはその後に実行される）
    DStorageManager::GetInstance()->SubmitAndWait();
    FinishGPU(data, dxCore);
    return false;
}

void AnimatedMeshCache::FinishGPU(AnimatedMeshData& data, DirectXCore* dxCore)
{
    if (!data.useDirectStorage || !data.indexResource) return;
    // COMMON → INDEX_BUFFER（頂点・影響度は暗黙プロモーションで読む）
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource   = data.indexResource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
    barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
    dxCore->GetCommandList()->ResourceBarrier(1, &barrier);
}
