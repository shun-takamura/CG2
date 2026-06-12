#include "AnimatedModelInstance.h"
#include <filesystem>
#include <cstring>
#include "SkinCluster.h"
#include "AssetLocator.h"
#include "DStorageManager.h"

void AnimatedModelInstance::Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename)
{
    modelCore_ = modelCore;

    // 拡張子で分岐: .mesh は Cooker 出力の 4 分割形式、その他は assimp 互換経路
    std::filesystem::path p(filename);
    if (p.extension() == ".mesh") {
        LoadModelV2(directoryPath, filename);
        // animation_ は LoadModelV2 内で .anim から読み込まれる
    } else {
        LoadModel(directoryPath, filename);
        animation_ = LoadAnimationFile(directoryPath, filename);
    }

    // 頂点データ作成
    CreateVertexData(modelCore_->GetDXCore());

    // インデックスデータ作成
    CreateIndexData(modelCore_->GetDXCore());

    // マテリアルデータ作成
    CreateMaterialData(modelCore_->GetDXCore());

    // テクスチャ読み込み
    textureFilePath_ = modelData_.materialData.textureFilePath;
    assert(!textureFilePath_.empty() && "textureFilePath is empty!");
    TextureManager::GetInstance()->LoadTexture(textureFilePath_);
}

void AnimatedModelInstance::Draw(DirectXCore* dxCore)
{
    assert(!textureFilePath_.empty() && "textureFilePath is empty in Draw!");
    assert(indexResource_ != nullptr && "indexResource is nullptr!");
    assert(indexBufferView_.BufferLocation != 0 && "indexBufferView is not set!");
    assert(indexCount_ > 0 && "indexCount is 0 in Draw!");

    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, materialResource_->GetGPUVirtualAddress()
    );

    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    dxCore->GetCommandList()->DrawIndexedInstanced(
        indexCount_, 1, 0, 0, 0);
}

void AnimatedModelInstance::DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    // Skinning済みの頂点バッファをVBVとして使う（Slot 0のみ）
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

    // Object3DManagerのRootSignatureに合わせる
    // RootParam[0] Material
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, materialResource_->GetGPUVirtualAddress()
    );

    // RootParam[2] Texture
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    // ドローコール
    dxCore->GetCommandList()->DrawIndexedInstanced(
        indexCount_, 1, 0, 0, 0);
}

void AnimatedModelInstance::DrawIdPass(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
    dxCore->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void AnimatedModelInstance::DrawShadowPass(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    // スキニング済みVBVで深度のみ描画（PSO/RootSig/カスケードはパス側で設定済み）
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
    dxCore->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void AnimatedModelInstance::CreateVertexData(DirectXCore* dxCore)
{
    const size_t sizeInBytes = sizeof(VertexData) * vertexCount_;

    if (useDirectStorage_) {
        // DEFAULT heap に COMMON state でバッファ作成。SkinCluster の CS が SRV として
        // 読み、bind-pose Draw 経路では VBV としても使える（暗黙プロモーションで対応）。
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeInBytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&vertexResource_));
        assert(SUCCEEDED(hr));

        uint64_t packOffset = 0, packSize = 0;
        AssetLocator::GetInstance()->GetPackEntryInfo(meshFilePath_, packOffset, packSize);
        auto* ds = DStorageManager::GetInstance();
        ds->EnqueueBufferRead(ds->GetPackFile(),
            packOffset + vertexFileOffset_,
            static_cast<uint32_t>(sizeInBytes),
            vertexResource_.Get(), 0);
        ds->SubmitAndWait();
        // COMMON のままにしておく（CS の SRV 読みは暗黙プロモーション対応）
    } else {
        // 既存パス: UPLOAD heap + Map + memcpy（CPU から書き込む）
        vertexResource_ = dxCore->CreateBufferResource(sizeInBytes);
        vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
        std::memcpy(vertexData_, modelData_.vertices.data(), sizeInBytes);
    }

    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeInBytes);
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
}

void AnimatedModelInstance::CreateMaterialData(DirectXCore* dxCore)
{
    materialResource_ = dxCore->CreateBufferResource(sizeof(Material));
    materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&material_));

    material_->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
    material_->enableLighting = true;
    material_->uvTransform = MakeIdentity4x4();
    material_->shininess = 50.0f;
    material_->environmentCoefficient = 1.0f;
    material_->useEnvironmentMap = false;
}

void AnimatedModelInstance::CreateIndexData(DirectXCore* dxCore)
{
    const size_t sizeInBytes = sizeof(uint32_t) * indexCount_;

    if (useDirectStorage_) {
        D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        desc.Width = sizeInBytes;
        desc.Height = 1; desc.DepthOrArraySize = 1; desc.MipLevels = 1;
        desc.SampleDesc.Count = 1;
        desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        HRESULT hr = dxCore->GetDevice()->CreateCommittedResource(
            &heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_COMMON, nullptr,
            IID_PPV_ARGS(&indexResource_));
        assert(SUCCEEDED(hr));

        uint64_t packOffset = 0, packSize = 0;
        AssetLocator::GetInstance()->GetPackEntryInfo(meshFilePath_, packOffset, packSize);
        auto* ds = DStorageManager::GetInstance();
        ds->EnqueueBufferRead(ds->GetPackFile(),
            packOffset + indexFileOffset_,
            static_cast<uint32_t>(sizeInBytes),
            indexResource_.Get(), 0);
        ds->SubmitAndWait();

        // COMMON → INDEX_BUFFER
        D3D12_RESOURCE_BARRIER barrier{};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource   = indexResource_.Get();
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COMMON;
        barrier.Transition.StateAfter  = D3D12_RESOURCE_STATE_INDEX_BUFFER;
        dxCore->GetCommandList()->ResourceBarrier(1, &barrier);
    } else {
        indexResource_ = dxCore->CreateBufferResource(sizeInBytes);
        uint32_t* indexData = nullptr;
        indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
        std::memcpy(indexData, modelData_.indices.data(), sizeInBytes);
        indexResource_->Unmap(0, nullptr);
    }

    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeInBytes);
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;
}

Node AnimatedModelInstance::ReadNode(aiNode* node)
{
    Node result;

    aiVector3D scale, translate;
    aiQuaternion rotate;
    node->mTransformation.Decompose(scale, rotate, translate);

    result.transform.scale = { scale.x, scale.y, scale.z };
    result.transform.rotate = { rotate.x, -rotate.y, -rotate.z, rotate.w };
    result.transform.translate = { -translate.x, translate.y, translate.z };

    result.localMatrix = MakeAffineMatrix(
        result.transform.scale, result.transform.rotate, result.transform.translate);

    result.name = node->mName.C_Str();
    result.children.resize(node->mNumChildren);
    for (uint32_t childIndex = 0; childIndex < node->mNumChildren; ++childIndex) {
        result.children[childIndex] = ReadNode(node->mChildren[childIndex]);
    }
    return result;
}

void AnimatedModelInstance::LoadModel(const std::string& directoryPath, const std::string& filename)
{
    Assimp::Importer importer;
    std::string filePath = directoryPath + "/" + filename;

    const aiScene* scene = importer.ReadFile(filePath.c_str(),
        aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate);
    assert(scene->HasMeshes());

    for (uint32_t meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
        aiMesh* mesh = scene->mMeshes[meshIndex];
        assert(mesh->HasNormals());
        assert(mesh->HasTextureCoords(0));

        // 複数メッシュ対応：このmeshの頂点が始まる位置を記録
        uint32_t vertexOffset = static_cast<uint32_t>(modelData_.vertices.size());

        // 頂点解析
        for (uint32_t vertexIndex = 0; vertexIndex < mesh->mNumVertices; ++vertexIndex) {
            aiVector3D& position = mesh->mVertices[vertexIndex];
            aiVector3D& normal = mesh->mNormals[vertexIndex];
            aiVector3D& texcoord = mesh->mTextureCoords[0][vertexIndex];

            VertexData vertex;
            vertex.position = { -position.x, position.y, position.z, 1.0f };
            vertex.normal = { -normal.x, normal.y, normal.z };
            vertex.texcoord = { texcoord.x, texcoord.y };
            modelData_.vertices.push_back(vertex);
        }

        // Index解析
        for (uint32_t faceIndex = 0; faceIndex < mesh->mNumFaces; ++faceIndex) {
            aiFace& face = mesh->mFaces[faceIndex];
            assert(face.mNumIndices == 3);

            for (uint32_t element = 0; element < face.mNumIndices; ++element) {
                uint32_t vertexIndex = face.mIndices[element];
                modelData_.indices.push_back(vertexOffset + vertexIndex);
            }
        }

        // Skinning用データの抽出
        for (uint32_t boneIndex = 0; boneIndex < mesh->mNumBones; ++boneIndex) {
            aiBone* bone = mesh->mBones[boneIndex];
            std::string jointName = bone->mName.C_Str();
            JointWeightData& jointWeightData = modelData_.skinClusterData[jointName];

            aiMatrix4x4 bindPoseMatrixAssimp = bone->mOffsetMatrix;
            bindPoseMatrixAssimp.Inverse();

            aiVector3D scale, translate;
            aiQuaternion rotate;
            bindPoseMatrixAssimp.Decompose(scale, rotate, translate);

            Matrix4x4 bindPoseMatrix = MakeAffineMatrix(
                { scale.x, scale.y, scale.z },
                { rotate.x, -rotate.y, -rotate.z, rotate.w },
                { -translate.x, translate.y, translate.z });

            jointWeightData.inverseBindPoseMatrix = Inverse(bindPoseMatrix);

            // Weight情報を取り出す（vertexIdにオフセットを加算）
            for (uint32_t weightIndex = 0; weightIndex < bone->mNumWeights; ++weightIndex) {
                jointWeightData.vertexWeights.push_back({
                    bone->mWeights[weightIndex].mWeight,
                    vertexOffset + bone->mWeights[weightIndex].mVertexId
                    });
            }
        }
    }

    for (uint32_t materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
        aiMaterial* material = scene->mMaterials[materialIndex];
        if (material->GetTextureCount(aiTextureType_DIFFUSE) != 0) {
            aiString textureFilePath;
            material->GetTexture(aiTextureType_DIFFUSE, 0, &textureFilePath);
            std::string modelDirectory = std::filesystem::path(filePath).parent_path().string();
            std::filesystem::path fullPath = std::filesystem::path(modelDirectory) / textureFilePath.C_Str();
            modelData_.materialData.textureFilePath = fullPath.lexically_normal().string();
        }
    }

    modelData_.rootNode = ReadNode(scene->mRootNode);
    assert(!modelData_.indices.empty() && "indices is empty!");
}

// =====================================================================
// .mesh / .skel / .mat / .anim 統合ローダー（Cooker v2 形式）
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
    if (version != 1) return {};
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

} // anonymous namespace

void AnimatedModelInstance::LoadModelV2(const std::string& directoryPath, const std::string& filename)
{
    const std::string meshPath = directoryPath + "/" + filename;
    auto h = AssetLocator::GetInstance()->Open(meshPath);
    assert(h.IsValid() && "failed to open .mesh file");

    char magic[4]{};
    h.Read(magic, 4);
    assert(std::memcmp(magic, "MESH", 4) == 0);

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
    assert(version == 2 && "expected .mesh v2");

    char skeletonPathBuf[256]{};
    h.Read(skeletonPathBuf, 256);
    std::string skeletonPath(skeletonPathBuf);

    const bool hasSkinning = (flags & 0x1) != 0;

    // GPU フェーズ / SkinCluster で使うメタ情報を保持
    meshFilePath_     = meshPath;
    vertexFileOffset_ = vertexOffset;
    vertexCount_      = vertexCount;
    indexFileOffset_  = indexOffset;
    indexCount_       = indexCount;
    skinFileOffset_   = skinOffset;
    hasSkinning_      = hasSkinning;

    // pack モード + DStorage 利用可能なら CPU 経由をスキップして VRAM 直行
    auto* loc = AssetLocator::GetInstance();
    auto* ds  = DStorageManager::GetInstance();
    useDirectStorage_ = loc->IsPackMode() && ds->IsInitialized() && ds->GetPackFile();

    if (!useDirectStorage_) {
        // --- 頂点 ---
        modelData_.vertices.resize(vertexCount);
        h.ReadAt(vertexOffset, modelData_.vertices.data(),
                 static_cast<size_t>(vertexCount) * sizeof(VertexData));

        // --- インデックス ---
        modelData_.indices.resize(indexCount);
        h.ReadAt(indexOffset, modelData_.indices.data(),
                 static_cast<size_t>(indexCount) * sizeof(uint32_t));
    }

    // --- .skel を読んで rootNode を構築 ---
    std::vector<SkelJoint> skelJoints;
    if (!skeletonPath.empty()) {
        skelJoints = ReadSkelFile(skeletonPath);
    }
    modelData_.rootNode = BuildNodeTreeFromSkel(skelJoints);
    if (skelJoints.empty()) {
        modelData_.rootNode.localMatrix = MakeIdentity4x4();
    }

    // .skel の Inverse Bind Pose Matrix と名前を joint-index 順で保持
    // （DStorage 経路では SkinCluster がここから直接拾い、skel-index → skeleton-index
    //   の remap を組み立てる）
    jointInverseBindMatrices_.clear();
    jointInverseBindMatrices_.reserve(skelJoints.size());
    jointNames_.clear();
    jointNames_.reserve(skelJoints.size());
    for (const auto& j : skelJoints) {
        jointInverseBindMatrices_.push_back(j.inverse_bind_matrix);
        jointNames_.emplace_back(j.name);
    }

    // --- スキニングデータを per-vertex から per-joint (skinClusterData) に展開 ---
    // DStorage 経路ではこの中間表現は不要（SkinCluster が pack の skin セクションを直接読む）
    if (!useDirectStorage_ && hasSkinning && !skelJoints.empty()) {
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
                auto& jwd = modelData_.skinClusterData[jointName];
                jwd.inverseBindPoseMatrix = skelJoints[jointIdx].inverse_bind_matrix;
                jwd.vertexWeights.push_back({ w, v });
            }
        }
    }

    // --- submesh[0] の material_path を読んで .mat へアクセス ---
    if (submeshCount > 0) {
        h.Seek(submeshOffset);
        uint32_t idxStart = 0, idxCount = 0;
        h.Read(&idxStart, 4);
        h.Read(&idxCount, 4);
        char matPath[256]{};
        h.Read(matPath, 256);

        std::string baseColor = ReadMatBaseColorPath_V2(std::string(matPath));
        modelData_.materialData.textureFilePath = baseColor;
    }

    // --- 同階層の .anim を探して読み込む（あれば）---
    // 注意: pack モードでの hash 一致のため、forward slash で組み立てる
    // （filesystem::path::string() は Windows でバックスラッシュを返すため使えない）
    std::filesystem::path animPath =
        std::filesystem::path(directoryPath) /
        (std::filesystem::path(filename).stem().string() + ".anim");
    std::string animPathStr = animPath.generic_string();
    if (AssetLocator::GetInstance()->Exists(animPathStr)) {
        animation_ = ReadAnimFile(animPathStr);
        animationPath_ = animPathStr;
    }

    assert(indexCount_ > 0 && "indexCount is 0!");
}

void AnimatedModelInstance::ReplaceAnimation(const std::string& animPath)
{
    if (animPath.empty()) return;
    if (!AssetLocator::GetInstance()->Exists(animPath)) return;
    animation_ = ReadAnimFile(animPath);
    animationPath_ = animPath;
}