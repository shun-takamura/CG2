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
    std::string normalMapFilePath_;  // 法線マップ DDS パス（空＝なし）
    std::string matFilePath_;        // submesh が参照する .mat パス（GPU material 反映用）
    std::string animationPath_;   // 現在ロード中の .anim パス（空＝モデル付属のデフォルト）

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

    // DStorage 経路用 (pack モード + DStorage 利用可能なら true)。
    // ON のとき modelData_.vertices / .indices / .skinClusterData は CPU に持たない。
    bool        useDirectStorage_ = false;
    std::string meshFilePath_;
    uint32_t    vertexFileOffset_ = 0;
    uint32_t    vertexCount_      = 0;
    uint32_t    indexFileOffset_  = 0;
    uint32_t    indexCount_       = 0;
    uint32_t    skinFileOffset_   = 0;
    bool        hasSkinning_      = false;
    // .skel から読んだ joint-index 順の Inverse Bind Pose Matrix。
    // CPU 経路では skinClusterData 経由で再構築されるが、DStorage 経路では
    // ここをそのまま SkinCluster に渡す。
    std::vector<Matrix4x4> jointInverseBindMatrices_;
    // .skel と同じ順の joint 名。SkinCluster で skel-index → skeleton-index の
    // remap テーブルを構築するのに使う。
    std::vector<std::string> jointNames_;

    //==============================
    // メンバ関数
    //==============================
public:
    void Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename);

    void Draw(DirectXCore* dxCore);

    // ComputeShaderでSkinning済みのVBVを使って描画（Object3DManagerのRootSignatureを使用）
    void DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster);

    // ID Pass 用：Skinning 済み VBV と IBV をバインドして DrawIndexedInstanced するだけ。
    // RootSig / PSO / Transformation CB / RootConstant の設定は呼び出し側が行う。
    void DrawIdPass(DirectXCore* dxCore, const SkinCluster& skinCluster);

    // シャドウパス用：Skinning 済み VBV/IBV をバインドして深度のみ描画。
    void DrawShadowPass(DirectXCore* dxCore, const SkinCluster& skinCluster);

    const ModelData& GetModelData() const { return modelData_; }
    const Animation& GetAnimation() const { return animation_; }

    Material* GetMaterialPointer() const { return material_; }

    // Inspector からテクスチャを差し替える際に使う
    void SetTextureFilePath(const std::string& filePath) {
        textureFilePath_ = filePath;
        modelData_.materialData.textureFilePath = filePath;
    }
    const std::string& GetTextureFilePath() const { return textureFilePath_; }

    void SetNormalMapFilePath(const std::string& filePath) {
        normalMapFilePath_ = filePath;
        modelData_.materialData.normalMapFilePath = filePath;
    }
    const std::string& GetNormalMapFilePath() const { return normalMapFilePath_; }

    /// <summary>
    /// 別の .anim ファイルでアニメーションを差し替える。Inspector ドロップから呼ばれる。
    /// 失敗時（ファイル無し等）は何もしない。
    /// </summary>
    void ReplaceAnimation(const std::string& animPath);
    const std::string& GetAnimationPath() const { return animationPath_; }

    SkinCluster& GetSkinCluster() { return skinCluster_; }
    bool HasSkinCluster() const { return hasSkinCluster_; }

    // DStorage 経路用のアクセサ
    bool UseDirectStorage() const { return useDirectStorage_; }
    uint32_t GetVertexCount() const { return vertexCount_; }
    uint32_t GetIndexCount()  const { return indexCount_; }
    const std::string& GetMeshFilePath() const { return meshFilePath_; }
    uint32_t GetSkinFileOffset() const { return skinFileOffset_; }
    bool HasSkinning() const { return hasSkinning_; }
    ID3D12Resource* GetVertexResource() const { return vertexResource_.Get(); }
    const std::vector<Matrix4x4>& GetJointInverseBindMatrices() const {
        return jointInverseBindMatrices_;
    }
    const std::vector<std::string>& GetJointNames() const { return jointNames_; }

private:
    void CreateVertexData(DirectXCore* dxCore);
    void CreateMaterialData(DirectXCore* dxCore);
    void CreateIndexData(DirectXCore* dxCore);

    Node ReadNode(aiNode* node);
    void LoadModel(const std::string& directoryPath, const std::string& filename);

    // Cooker が出力した .mesh + .skel + .mat + .anim をまとめて読む新経路
    void LoadModelV2(const std::string& directoryPath, const std::string& filename);
};