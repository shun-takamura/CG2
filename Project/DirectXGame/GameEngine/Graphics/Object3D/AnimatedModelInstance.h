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

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

// Nodeは既存のModelInstance.hで定義されているので、そちらをinclude
#include "ModelInstance.h"

class AnimatedModelInstance
{
    //==============================
    // メンバ変数
    //==============================
    std::string textureFilePath_;

    ModelData modelData_;
    Animation animation_;  // ★アニメーションデータを追加

    ModelCore* modelCore_;

    VertexData* vertexData_ = nullptr;
    Material* material_ = nullptr;

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

    const ModelData& GetModelData() const { return modelData_; }
    const Animation& GetAnimation() const { return animation_; }  // ★追加

    Material* GetMaterialPointer() const { return material_; }

private:
    void CreateVertexData(DirectXCore* dxCore);
    void CreateMaterialData(DirectXCore* dxCore);
    void CreateIndexData(DirectXCore* dxCore);

    Node ReadNode(aiNode* node);
    void LoadModel(const std::string& directoryPath, const std::string& filename);
};