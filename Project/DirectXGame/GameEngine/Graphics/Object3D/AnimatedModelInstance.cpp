#include "AnimatedModelInstance.h"
#include <filesystem>
#include "SkinCluster.h"
#include "SRVManager.h"

void AnimatedModelInstance::Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename)
{
    modelCore_ = modelCore;

    // モデル読み込み
    LoadModel(directoryPath, filename);

    // アニメーション読み込み（同じファイルをもう一度読む。後でリファクタリング予定）
    animation_ = LoadAnimationFile(directoryPath, filename);

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
    assert(!modelData_.indices.empty() && "indices is empty in Draw!");

    dxCore->GetCommandList()->IASetVertexBuffers(0, 1, &vertexBufferView_);
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, materialResource_->GetGPUVirtualAddress()
    );

    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    dxCore->GetCommandList()->DrawIndexedInstanced(
        UINT(modelData_.indices.size()), 1, 0, 0, 0);
}

void AnimatedModelInstance::DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster, SRVManager* srvManager)
{
    // VBVを2本セット（VertexData + Influence）
    D3D12_VERTEX_BUFFER_VIEW vbvs[2] = {
        vertexBufferView_,
        skinCluster.influenceBufferView
    };
    dxCore->GetCommandList()->IASetVertexBuffers(0, 2, vbvs);

    // インデックスバッファ
    dxCore->GetCommandList()->IASetIndexBuffer(&indexBufferView_);

    // RootParam[0] Material
    dxCore->GetCommandList()->SetGraphicsRootConstantBufferView(
        0, materialResource_->GetGPUVirtualAddress()
    );

    // RootParam[2] Texture
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        2, TextureManager::GetInstance()->GetSrvHandleGPU(textureFilePath_)
    );

    // RootParam[8] MatrixPalette
    dxCore->GetCommandList()->SetGraphicsRootDescriptorTable(
        8, srvManager->GetGPUDescriptorHandle(skinCluster.paletteSrvIndex)
    );

    // ドローコール
    dxCore->GetCommandList()->DrawIndexedInstanced(
        UINT(modelData_.indices.size()), 1, 0, 0, 0);
}

void AnimatedModelInstance::CreateVertexData(DirectXCore* dxCore)
{
    vertexResource_ = dxCore->CreateBufferResource(sizeof(VertexData) * modelData_.vertices.size());
    vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
    vertexBufferView_.SizeInBytes = UINT(sizeof(VertexData) * modelData_.vertices.size());
    vertexBufferView_.StrideInBytes = sizeof(VertexData);
    vertexResource_->Map(0, nullptr, reinterpret_cast<void**>(&vertexData_));
    std::memcpy(vertexData_, modelData_.vertices.data(), sizeof(VertexData) * modelData_.vertices.size());
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
    indexResource_ = dxCore->CreateBufferResource(sizeof(uint32_t) * modelData_.indices.size());
    indexBufferView_.BufferLocation = indexResource_->GetGPUVirtualAddress();
    indexBufferView_.SizeInBytes = UINT(sizeof(uint32_t) * modelData_.indices.size());
    indexBufferView_.Format = DXGI_FORMAT_R32_UINT;

    uint32_t* indexData = nullptr;
    indexResource_->Map(0, nullptr, reinterpret_cast<void**>(&indexData));
    std::memcpy(indexData, modelData_.indices.data(), sizeof(uint32_t) * modelData_.indices.size());
    indexResource_->Unmap(0, nullptr);
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