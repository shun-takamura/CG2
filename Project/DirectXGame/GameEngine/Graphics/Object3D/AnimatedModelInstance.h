#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include "ModelCore.h"
#include "VertexData.h"
#include "Material.h"
#include <fstream>
#include <cassert>
#include "TextureManager.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include "Animation.h"
#include "SkinCluster.h"

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// Nodeは既存のModelInstance.hで定義されているのでinclude
#include "ModelInstance.h"

struct SkinCluster;
class SRVManager;

class AnimatedModelInstance
{
    //==============================
    // メンバ変数
    //==============================
    std::string textureFilePath_;

    ModelData modelData_;
    Animation animation_;

    ModelCore* modelCore_;

    VertexData* vertexData_ = nullptr;
    Material* material_ = nullptr;

    SkinCluster skinCluster_;
    bool hasSkinCluster_ = false;

    Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
    Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;

    D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
    D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

    //==============================
    // メンバ関数
    //==============================
public:
    void Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename);

    void Draw(DirectXCore* dxCore);

    void DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster, SRVManager* srvManager);

    // ComputeShaderでSkinning済みのVBVを使って描画（Object3DManagerのRootSignatureを使用）
    void DrawSkinningWithCS(DirectXCore* dxCore, const SkinCluster& skinCluster);

    const ModelData& GetModelData() const { return modelData_; }
    const Animation& GetAnimation() const { return animation_; }

    Material* GetMaterialPointer() const { return material_; }

    SkinCluster& GetSkinCluster() { return skinCluster_; }
    bool HasSkinCluster() const { return hasSkinCluster_; }

private:
    void CreateVertexData(DirectXCore* dxCore);
    void CreateMaterialData(DirectXCore* dxCore);
    void CreateIndexData(DirectXCore* dxCore);

    Node ReadNode(aiNode* node);
    void LoadModel(const std::string& directoryPath, const std::string& filename);
};