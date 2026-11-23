#pragma once
#include "DirectXCore.h"
#include <wrl.h>
#include "ModelCore.h"
#include "VertexData.h"
#include "Material.h"
#include <fstream>
#include <memory>
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
struct AnimatedMeshData;
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

    // .mesh 経路は AnimatedMeshCache の共有データ（頂点・インデックス・影響度のバッファ、骨、部位の表）を指す。
    // modelData_ は assimp 経路だけが使う（.mesh 経路は mesh_->modelData）
    std::shared_ptr<const AnimatedMeshData> mesh_;
    ModelData modelData_;
    Animation animation_;

    // 部位別マテリアル。.mesh 経路は submesh 数ぶん、assimp 経路は 1 個（全 index）を保持
    std::vector<RenderSubmesh> submeshes_;

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
    // モデル空間の包む球（バインドポーズ。.mesh v4 のヘッダ、無ければ頂点から計算）
    BoundingSphere bounds_;
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
    /// <summary>
    /// モデルを読み込む（.mesh は AnimatedMeshCache の共有データ、それ以外は assimp）。
    /// 部位ごとのマテリアル CB を作り、.mat の値・テクスチャ・法線マップを反映する（無いテクスチャは白で代用）
    /// </summary>
    /// <param name="modelCore">DirectXCore の取得元</param>
    /// <param name="directoryPath">モデルのあるディレクトリ</param>
    /// <param name="filename">モデルのファイル名（.mesh か assimp で読める形式）</param>
    void Initialize(ModelCore* modelCore, const std::string& directoryPath, const std::string& filename);

    /// <summary>バインドポーズのまま部位ごとに描く（Object3DManager のルートの [0]=PS b0 マテリアル / [2]=PS t0 テクスチャ / [10]=PS t2 法線マップ を貼り替える）</summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    void Draw(DirectXCore* dxCore);

    /// <summary>ComputeShaderでSkinning済みのVBVを使って部位ごとに描画（Object3DManagerのRootSignatureの [0]/[2]/[10] を貼り替える）</summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="skinCluster">スキニング済み頂点を持つ SkinCluster</param>
    void DrawSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster);

    /// <summary>
    /// ID Pass 用：Skinning 済み VBV と IBV をバインドして DrawIndexedInstanced するだけ。
    /// RootSig / PSO / Transformation CB / RootConstant の設定は呼び出し側が行う。
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="skinCluster">スキニング済み頂点を持つ SkinCluster</param>
    void DrawIdPass(DirectXCore* dxCore, const SkinCluster& skinCluster);

    /// <summary>シャドウパス用：Skinning 済み VBV/IBV をバインドして深度のみ描画（PSO/RootSig/カスケードはパス側で設定済み）</summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="skinCluster">スキニング済み頂点を持つ SkinCluster</param>
    void DrawShadowPass(DirectXCore* dxCore, const SkinCluster& skinCluster);

    /// <summary>
    /// 発光パス用（ModelInstance::DrawBloomPass のスキニング版）：Skinning 済み VBV で、発光している部位と
    /// drawSubmesh[i] が 0 でない部位（ディゾルブの縁を光らせる部位）を描く。RootSig / PSO / Transformation CB は呼び出し側。
    /// 発光パスのルートの [1]=PS b0 マテリアル / [3]=PS t0 テクスチャ を貼り替える
    /// </summary>
    /// <param name="dxCore">コマンドリストの取得元</param>
    /// <param name="skinCluster">スキニング済み頂点を持つ SkinCluster</param>
    /// <param name="drawSubmesh">部位ごとに、発光していなくても描くなら 0 以外</param>
    void DrawBloomPassSkinning(DirectXCore* dxCore, const SkinCluster& skinCluster, const std::vector<uint8_t>& drawSubmesh);

    /// <summary>部位ごとのマテリアル（ディゾルブ・発光を部位で変えるため）。material は書き込み専用のメモリなので読まない</summary>
    /// <returns>部位の配列</returns>
    std::vector<RenderSubmesh>& GetSubmeshes() { return submeshes_; }

    /// <summary>モデルデータを返す</summary>
    /// <returns>.mesh 経路は共有データの modelData、assimp 経路は自前の modelData</returns>
    const ModelData& GetModelData() const;
    /// <summary>今のアニメーションを返す</summary>
    /// <returns>アニメーション</returns>
    const Animation& GetAnimation() const { return animation_; }
    /// <summary>モデル空間の包む球（バインドポーズ）。IsValid() が false なら大きさ不明</summary>
    /// <returns>包む球</returns>
    const BoundingSphere& GetBoundingSphere() const { return bounds_; }

    /// <summary>旧来の単一マテリアルのポインタを返す（部位別は GetSubmeshes）</summary>
    /// <returns>マテリアル（未作成なら nullptr）</returns>
    Material* GetMaterialPointer() const { return material_; }

    /// <summary>Inspector からテクスチャを差し替える際に使う（先頭の部位に反映）</summary>
    /// <param name="filePath">テクスチャのパス</param>
    void SetTextureFilePath(const std::string& filePath) {
        textureFilePath_ = filePath;
        modelData_.materialData.textureFilePath = filePath;
        if (!submeshes_.empty()) submeshes_[0].textureFilePath = filePath;
    }
    /// <summary>先頭の部位のテクスチャのパスを返す</summary>
    /// <returns>テクスチャのパス</returns>
    const std::string& GetTextureFilePath() const { return textureFilePath_; }

    /// <summary>法線マップを差し替える（先頭の部位に反映）</summary>
    /// <param name="filePath">法線マップのパス</param>
    void SetNormalMapFilePath(const std::string& filePath) {
        normalMapFilePath_ = filePath;
        modelData_.materialData.normalMapFilePath = filePath;
        if (!submeshes_.empty()) submeshes_[0].normalMapFilePath = filePath;
    }
    /// <summary>先頭の部位の法線マップのパスを返す</summary>
    /// <returns>法線マップのパス（無ければ空）</returns>
    const std::string& GetNormalMapFilePath() const { return normalMapFilePath_; }

    /// <summary>
    /// 別の .anim ファイルでアニメーションを差し替える。Inspector ドロップから呼ばれる。
    /// 失敗時（ファイル無し等）は何もしない。
    /// </summary>
    /// <param name="animPath">.anim のパス</param>
    void ReplaceAnimation(const std::string& animPath);
    /// <summary>今のアニメーションの .anim のパスを返す</summary>
    /// <returns>パス（空ならモデル付属のデフォルト）</returns>
    const std::string& GetAnimationPath() const { return animationPath_; }

    /// <summary>SkinCluster を返す</summary>
    /// <returns>SkinCluster</returns>
    SkinCluster& GetSkinCluster() { return skinCluster_; }
    /// <summary>SkinCluster を持っているか</summary>
    /// <returns>持っていれば true</returns>
    bool HasSkinCluster() const { return hasSkinCluster_; }

    // DStorage 経路用のアクセサ
    /// <summary>DirectStorage 経路で読んだか</summary>
    /// <returns>DirectStorage 経路なら true</returns>
    bool UseDirectStorage() const { return useDirectStorage_; }
    /// <summary>頂点数を返す</summary>
    /// <returns>頂点数</returns>
    uint32_t GetVertexCount() const { return vertexCount_; }
    /// <summary>インデックス数を返す</summary>
    /// <returns>インデックス数</returns>
    uint32_t GetIndexCount()  const { return indexCount_; }
    /// <summary>.mesh のパスを返す</summary>
    /// <returns>パス</returns>
    const std::string& GetMeshFilePath() const { return meshFilePath_; }
    /// <summary>.mesh 内の skin セクションの位置を返す</summary>
    /// <returns>ファイル先頭からのバイト位置</returns>
    uint32_t GetSkinFileOffset() const { return skinFileOffset_; }
    /// <summary>.mesh がスキンの影響度を持つか</summary>
    /// <returns>持っていれば true</returns>
    bool HasSkinning() const { return hasSkinning_; }
    /// <summary>頂点バッファを返す（スキニング CS の入力）</summary>
    /// <returns>頂点バッファ</returns>
    ID3D12Resource* GetVertexResource() const { return vertexResource_.Get(); }
    /// <summary>DirectStorage 経路で AnimatedMeshCache が持つ共有の影響度バッファ（無ければ nullptr＝SkinCluster が作る）</summary>
    /// <returns>共有の影響度バッファ。無ければ nullptr</returns>
    ID3D12Resource* GetSharedInfluenceResource() const;
    /// <summary>.skel の joint-index 順の逆バインド行列を返す</summary>
    /// <returns>逆バインド行列の配列</returns>
    const std::vector<Matrix4x4>& GetJointInverseBindMatrices() const {
        return jointInverseBindMatrices_;
    }
    /// <summary>.skel と同じ順の joint 名を返す</summary>
    /// <returns>joint 名の配列</returns>
    const std::vector<std::string>& GetJointNames() const { return jointNames_; }

private:
    /// <summary>assimp 経路の頂点バッファを作る（DirectStorage 経路なら DEFAULT heap に作って転送）</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateVertexData(DirectXCore* dxCore);
    /// <summary>部位ごとの Material CB を作って既定値で初期化する</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateMaterialData(DirectXCore* dxCore);
    /// <summary>assimp 経路のインデックスバッファを作る</summary>
    /// <param name="dxCore">デバイスの取得元</param>
    void CreateIndexData(DirectXCore* dxCore);

    /// <summary>assimp のノードを再帰的に Node へ変換する（右手→左手に合わせて X を反転）</summary>
    /// <param name="node">assimp のノード</param>
    /// <returns>変換した Node</returns>
    Node ReadNode(aiNode* node);
    /// <summary>assimp でモデルを読み、頂点・インデックス・ノード階層・スキンを modelData_ に入れる</summary>
    /// <param name="directoryPath">モデルのあるディレクトリ</param>
    /// <param name="filename">モデルのファイル名</param>
    void LoadModel(const std::string& directoryPath, const std::string& filename);

    /// <summary>Cooker が出力した .mesh + .skel + .mat + .anim は AnimatedMeshCache が読む。共有データをこのインスタンスへ写す</summary>
    /// <param name="mesh">共有のメッシュデータ（nullptr なら何もしない）</param>
    void AttachSharedMesh(std::shared_ptr<const AnimatedMeshData> mesh);
};