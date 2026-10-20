#include "AnimatedModelInstance.h"
#include <filesystem>
#include <cstring>
#include "SkinCluster.h"
#include "AssetLocator.h"
#include "DStorageManager.h"
#include "AnimatedMeshCache.h"
#include "PepperMacros.h"

// テクスチャ無しマテリアル（色のみ PBR 等）用のフォールバック白テクスチャ。
// 無ければ手続き生成して確保し、パスを返す（pack/FS どちらでも確実に存在させる）。
static const std::string& EnsureFallbackWhiteTexture()
{
    static const std::string kWhite = "Resources/Textures/white1x1.dds";
    auto* tm = TextureManager::GetInstance();
    if (!tm->HasTexture(kWhite)) {
        tm->CreateSolidColorTexture(kWhite, 255, 255, 255, 255);
    }
    return kWhite;
}

void AnimatedModelInstance::Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename)
{
    modelCore_ = modelCore;

    // 拡張子で分岐: .mesh は Cooker 出力の 4 分割形式、その他は assimp 互換経路
    std::filesystem::path p(filename);
    if (p.extension() == ".mesh") {
        // 同じメッシュはインスタンスをまたいで共有（初めてならここで読む。ScenePreloader が先に読んでいればすぐ返る）
        AttachSharedMesh(AnimatedMeshCache::GetInstance()->Acquire(directoryPath, filename, modelCore_->GetDXCore()));
        assert(mesh_ && "failed to load .mesh");
    } else {
        LoadModel(directoryPath, filename);
        animation_ = LoadAnimationFile(directoryPath, filename);
    }
    if (!bounds_.IsValid() && !modelData_.vertices.empty()) {
        bounds_ = ComputeBoundingSphere(modelData_.vertices);
    }

    // submesh テーブルが無い場合は、全 index を 1 submesh として扱う（Draw を統一）
    if (submeshes_.empty()) {
        RenderSubmesh sm;
        sm.indexStart = 0;
        sm.indexCount = indexCount_;
        sm.matFilePath = matFilePath_;
        sm.textureFilePath = GetModelData().materialData.textureFilePath;
        sm.normalMapFilePath = GetModelData().materialData.normalMapFilePath;
        submeshes_.push_back(std::move(sm));
    }

    // 頂点・インデックスのバッファ（.mesh 経路は共有のバッファを AttachSharedMesh で受け取り済み）
    if (!mesh_) {
        CreateVertexData(modelCore_->GetDXCore());
        CreateIndexData(modelCore_->GetDXCore());
    }

    // マテリアルデータ作成（submesh 数ぶんの Material CBV を作る）
    CreateMaterialData(modelCore_->GetDXCore());

    // submesh ごとに .mat 値を反映し、テクスチャ／法線マップをロードする
    for (auto& sm : submeshes_) {
        // .mat の値（color / metallic / roughness / shadingModel / useNormalMap 等）を反映
        // （.mesh 経路のみ。assimp 経路は .mat が無いので CreateMaterialData の既定値のまま）
        if (!sm.matFilePath.empty() && sm.material) {
            MaterialData matTmp;
            LoadMatFile(sm.matFilePath, matTmp, sm.material);
            // 視差（POM）はスキンメッシュでは扱わない（ハイトマップを貼らないので必ず切る）
            sm.material->useParallax = 0;
        }

        // テクスチャ無しマテリアル（色のみ PBR 等）や、参照先 DDS が見つからない場合は
        // 白テクスチャで代用し material.color で色付けする（未登録 SRV バインドのクラッシュ回避）
        auto* tm = TextureManager::GetInstance();
        if (sm.textureFilePath.empty()) {
            sm.textureFilePath = EnsureFallbackWhiteTexture();
        }
        tm->LoadTexture(sm.textureFilePath);
        if (!tm->HasTexture(sm.textureFilePath)) {
            sm.textureFilePath = EnsureFallbackWhiteTexture();
            tm->LoadTexture(sm.textureFilePath);
        }

        // 法線マップがあればロード。ロードに失敗したら法線マップ無し扱いにする（不正 SRV 回避）
        if (!sm.normalMapFilePath.empty()) {
            // 法線マップは線形データ。sRGB としてデガンマされると法線がずれるので Linear で読む
            tm->LoadTextureLinear(sm.normalMapFilePath);
            if (!tm->HasTexture(sm.normalMapFilePath)) {
                sm.normalMapFilePath.clear();
            }
        }
        if (sm.material) sm.material->useNormalMap = sm.normalMapFilePath.empty() ? 0 : 1;
    }

    // 後方互換: submesh[0] を既存メンバへ反映（フォールバック適用後の非空パス）
    textureFilePath_ = submeshes_[0].textureFilePath;
    normalMapFilePath_ = submeshes_[0].normalMapFilePath;
    assert(!textureFilePath_.empty() && "textureFilePath is empty!");
}

void AnimatedModelInstance::Draw(DirectXCore* dxCore)
{
    assert(!textureFilePath_.empty() && "textureFilePath is empty in Draw!");
    assert(indexResource_ != nullptr && "indexResource is nullptr!");
    assert(indexBufferView_.BufferLocation != 0 && "indexBufferView is not set!");
    assert(indexCount_ > 0 && "indexCount is 0 in Draw!");

    auto* cmd = dxCore->GetCommandList();

    // VBV / IBV はモデル全体で共通
    cmd->IASetVertexBuffers(0, 1, &vertexBufferView_);
    cmd->IASetIndexBuffer(&indexBufferView_);

    // submesh ごとに Material / テクスチャ / 法線を貼り替えて分割ドロー
    for (const auto& sm : submeshes_) {
        if (sm.indexCount == 0) continue;

        cmd->SetGraphicsRootConstantBufferView(
            0, sm.materialResource->GetGPUVirtualAddress()
        );

        const std::string& texPath = !sm.textureFilePath.empty() ? sm.textureFilePath : textureFilePath_;
        cmd->SetGraphicsRootDescriptorTable(
            2, TextureManager::GetInstance()->GetSrvHandleGPU(texPath)
        );

        const std::string& normalSrvPath = !sm.normalMapFilePath.empty() ? sm.normalMapFilePath : texPath;
        cmd->SetGraphicsRootDescriptorTable(
            10, TextureManager::GetInstance()->GetSrvHandleGPU(normalSrvPath)
        );

        PEPPER_COUNT("DrawCall");
        cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexStart, 0, 0);
    }
}

void AnimatedModelInstance::DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    auto* cmd = dxCore->GetCommandList();

    // Skinning済みの頂点バッファをVBVとして使う（Slot 0のみ）。IBV はモデル全体で共通
    cmd->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    cmd->IASetIndexBuffer(&indexBufferView_);

    // submesh ごとに Object3DManager の RootSignature に合わせて貼り替えて分割ドロー
    for (const auto& sm : submeshes_) {
        if (sm.indexCount == 0) continue;

        // RootParam[0] Material
        cmd->SetGraphicsRootConstantBufferView(
            0, sm.materialResource->GetGPUVirtualAddress()
        );

        // RootParam[2] Texture
        const std::string& texPath = !sm.textureFilePath.empty() ? sm.textureFilePath : textureFilePath_;
        cmd->SetGraphicsRootDescriptorTable(
            2, TextureManager::GetInstance()->GetSrvHandleGPU(texPath)
        );

        // RootParam[10] 法線マップ（無ければベースで埋める。PS は useNormalMap で判定）
        const std::string& normalSrvPath = !sm.normalMapFilePath.empty() ? sm.normalMapFilePath : texPath;
        cmd->SetGraphicsRootDescriptorTable(
            10, TextureManager::GetInstance()->GetSrvHandleGPU(normalSrvPath)
        );

        PEPPER_COUNT("DrawCall");
        cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexStart, 0, 0);
    }
}

void AnimatedModelInstance::DrawIdPass(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
    PEPPER_COUNT("DrawCall");
    dxCore->GetCommandList()->DrawIndexedInstanced(indexCount_, 1, 0, 0, 0);
}

void AnimatedModelInstance::DrawBloomPassSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster,
    const std::vector<uint8_t>& drawSubmesh)
{
    auto* cmd = dxCore->GetCommandList();
    cmd->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    cmd->IASetIndexBuffer(&indexBufferView_);
    for (size_t i = 0; i < submeshes_.size(); ++i) {
        const RenderSubmesh& sm = submeshes_[i];
        if (sm.indexCount == 0) continue;
        const bool dissolve = i < drawSubmesh.size() && drawSubmesh[i] != 0;
        if (!dissolve && !sm.hasEmissive) continue;
        cmd->SetGraphicsRootConstantBufferView(1, sm.materialResource->GetGPUVirtualAddress());
        const std::string& texPath = !sm.textureFilePath.empty() ? sm.textureFilePath : textureFilePath_;
        cmd->SetGraphicsRootDescriptorTable(3, TextureManager::GetInstance()->GetSrvHandleGPU(texPath));
        PEPPER_COUNT("DrawCall");
        cmd->DrawIndexedInstanced(sm.indexCount, 1, sm.indexStart, 0, 0);
    }
}

void AnimatedModelInstance::DrawShadowPass(DirectXCore* dxCore, const SkinCluster& skinCluster)
{
    // スキニング済みVBVで深度のみ描画（PSO/RootSig/カスケードはパス側で設定済み）
    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &skinCluster.skinnedVertexBufferView);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);
    PEPPER_COUNT("DrawCall");
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
    // submesh ごとに Material 定数バッファを作成し、既定値で初期化する
    for (auto& sm : submeshes_) {
        sm.materialResource = dxCore->CreateBufferResource(sizeof(Material));
        sm.materialResource->Map(0, nullptr, reinterpret_cast<void**>(&sm.material));

        Material* m = sm.material;
        m->color = Vector4(1.0f, 1.0f, 1.0f, 1.0f);
        m->enableLighting = true;
        m->uvTransform = MakeIdentity4x4();
        m->shininess = 50.0f;
        m->environmentCoefficient = 1.0f;
        m->useEnvironmentMap = false;
        m->metallic = 0.0f;
        m->roughness = 0.5f;
        m->shadingModel = 0;
        m->useNormalMap = 0;
        m->cloudReflection = 0.0f;
        m->dissolveEnable = 0;
        m->dissolveProgress = 1.0f;
        m->useParallax = 0;
        m->parallaxDepth = 0.0f;
        m->parallaxMinLayers = 8.0f;
        m->parallaxMaxLayers = 32.0f;
        m->emissive = Vector4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 後方互換: submesh[0] を既存メンバへ反映（GetMaterialPointer 等）
    if (!submeshes_.empty()) {
        materialResource_ = submeshes_[0].materialResource;
        material_ = submeshes_[0].material;
    }
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
        aiProcess_FlipWindingOrder | aiProcess_FlipUVs | aiProcess_Triangulate | aiProcess_CalcTangentSpace);
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
            vertex.tangent = { 1.0f, 0.0f, 0.0f, 1.0f };
            if (mesh->HasTangentsAndBitangents()) {
                aiVector3D& tangent = mesh->mTangents[vertexIndex];
                vertex.tangent = { -tangent.x, tangent.y, tangent.z, 1.0f };
            }
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

const ModelData& AnimatedModelInstance::GetModelData() const
{
    return mesh_ ? mesh_->modelData : modelData_;
}

ID3D12Resource* AnimatedModelInstance::GetSharedInfluenceResource() const
{
    return mesh_ ? mesh_->influenceResource.Get() : nullptr;
}

void AnimatedModelInstance::AttachSharedMesh(std::shared_ptr<const AnimatedMeshData> mesh)
{
    mesh_ = std::move(mesh);
    if (!mesh_) return;
    const AnimatedMeshData& m = *mesh_;

    animation_        = m.animation;
    animationPath_    = m.animationPath;
    submeshes_        = m.submeshes;     // マテリアルの CB は Initialize がインスタンスごとに作る
    matFilePath_      = m.matFilePath;
    useDirectStorage_ = m.useDirectStorage;
    meshFilePath_     = m.meshFilePath;
    vertexFileOffset_ = m.vertexFileOffset;
    vertexCount_      = m.vertexCount;
    indexFileOffset_  = m.indexFileOffset;
    indexCount_       = m.indexCount;
    skinFileOffset_   = m.skinFileOffset;
    hasSkinning_      = m.hasSkinning;
    bounds_           = m.bounds;
    jointInverseBindMatrices_ = m.jointInverseBindMatrices;
    jointNames_       = m.jointNames;
    vertexResource_   = m.vertexResource;
    indexResource_    = m.indexResource;
    vertexBufferView_ = m.vertexBufferView;
    indexBufferView_  = m.indexBufferView;
    textureFilePath_  = m.modelData.materialData.textureFilePath;
    normalMapFilePath_ = m.modelData.materialData.normalMapFilePath;
}

void AnimatedModelInstance::ReplaceAnimation(const std::string& animPath)
{
    // クリップはパスでキャッシュする（呼ぶたびにファイルを読むと、姿勢を切り替えるフレームがカクつく）
    auto clip = AnimatedMeshCache::GetInstance()->GetAnimation(animPath);
    if (!clip) return;
    animation_ = *clip;
    animationPath_ = animPath;
}