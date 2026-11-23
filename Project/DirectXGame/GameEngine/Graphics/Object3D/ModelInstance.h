#pragma once
#include"DirectXCore.h"
#include <wrl.h>
#include"ModelCore.h"
#include"VertexData.h"
#include "Material.h"
#include <fstream>
#include <cassert>
#include"TextureManager.h"
#include"MathUtility.h"
#include "QuaternionTransform.h"
#include <map>
#include <vector>

// assimp
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

struct Node{
	QuaternionTransform transform;
	Matrix4x4 localMatrix;
	std::string name;
	std::vector<Node>children;
};

// Skinning用のデータ構造
struct VertexWeightData {
	float weight;
	uint32_t vertexIndex;
};

struct JointWeightData {
	Matrix4x4 inverseBindPoseMatrix;
	std::vector<VertexWeightData> vertexWeights;
};

struct ModelData
{
	std::vector<VertexData>vertices;
	std::vector<uint32_t> indices;
	MaterialData materialData;
	Node rootNode;
	std::map<std::string, JointWeightData> skinClusterData;
};

/// <summary>
/// モデルを包む球（モデル空間）。.mesh v4 はクック時に計算してヘッダに持つ。
/// radius &lt; 0 は「分からない」（v3 の .mesh を pack モードで読んだ時など）。使う側は常に見える扱いにする。
/// </summary>
struct BoundingSphere
{
	Vector3 center{};
	float radius = -1.0f;
	/// <summary>大きさが分かっているか</summary>
	/// <returns>radius が 0 以上なら true</returns>
	bool IsValid() const { return radius >= 0.0f; }
};

/// <summary>頂点を全部包む球（中心 = AABB の中心）。cook_assets.py の _compute_bounding_sphere と同じ作り方</summary>
/// <param name="vertices">頂点</param>
/// <returns>包む球。頂点が無ければ無効（radius &lt; 0）</returns>
BoundingSphere ComputeBoundingSphere(const std::vector<VertexData>& vertices);

// 部位別マテリアル用のランタイム submesh。
// 頂点/インデックスは 1 本のバッファに連結され、submesh は index 範囲でその部分を指す。
struct RenderSubmesh
{
	uint32_t indexStart = 0;
	uint32_t indexCount = 0;
	std::string matFilePath;        // .mat パス（assimp 経路など無い場合は空）
	std::string textureFilePath;    // base color DDS
	std::string normalMapFilePath;  // 法線 DDS（空＝なし）
	std::string heightMapFilePath;  // 視差のハイトマップ DDS（.mat v4。空＝なし）
	std::string terrainColorArrayPath;   // 地形の層の色の配列 DDS（.mat v5。空＝地形でない）
	std::string terrainNormalArrayPath;  // 地形の層の法線の配列 DDS（.mat v5）

	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource;
	Material* material = nullptr;    // materialResource を Map したポインタ

	// material->emissive が 0 でないか（発光パスで描くかの判定。Map 先は書き込み専用なので CPU 側に持つ）
	bool hasEmissive = false;
};

class ModelInstance
{
	//==============================
	// ロード状態
	//==============================
	// GPUPending = BeginGPUAsync でバッファを作って DirectStorage に積んだが、まだ仕上げていない（描かない）
	enum class LoadState { Unloaded, CPUReady, GPUPending, GPUReady };
	LoadState loadState_ = LoadState::Unloaded;

	//==============================
	// メンバ変数
	//==============================
	std::string textureFilePath_;  // テクスチャファイルパスを保持
	std::string normalMapFilePath_;  // 法線マップ DDS パス（空＝なし）
	std::string matFilePath_;        // submesh[0] が参照する .mat パス（後方互換用）

	ModelData modelData_;

	// 部位別マテリアル。.mesh 経路は submesh 数ぶん、assimp 経路は 1 個（全 index）を保持
	std::vector<RenderSubmesh> submeshes_;

	ModelCore* modelCore_;

	// VertexData は CPU 側 modelData_.vertices に保持。GPU バッファは DEFAULT heap で書き換え不可
	Material* material_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> materialResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexResource_;
	// DirectStorage で転送中のバッファ（FinishGPU で vertexResource_ / indexResource_ へ移す。それまでは描かない）
	Microsoft::WRL::ComPtr<ID3D12Resource> pendingVertexResource_;
	Microsoft::WRL::ComPtr<ID3D12Resource> pendingIndexResource_;

	// バッファリソースの使い道を補足するバッファビュー
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};

	// DStorage 経路用: .mesh が pack モード + DStorage 利用可能なら true。
	// ON のときは modelData_.vertices/indices を CPU に持たず、pack から直接 VRAM に転送する。
	bool        useDirectStorage_ = false;
	std::string meshFilePath_;
	uint32_t    vertexFileOffset_ = 0;
	uint32_t    vertexCount_      = 0;
	uint32_t    indexFileOffset_  = 0;
	uint32_t    indexCount_       = 0;

	// モデル空間の包む球（.mesh v4 のヘッダ、無ければ頂点から計算）
	BoundingSphere bounds_;

	//==============================
	// メンバ関数
	//==============================
public:
	/// <summary>既存の同期ロード（CPU + GPU両方）。LoadCPU → InitializeGPU を続けて行う</summary>
	/// <param name="modelManager">DirectXCore の取得元</param>
	/// <param name="directorPath">モデルのあるディレクトリ</param>
	/// <param name="filename">モデルのファイル名（.mesh か assimp で読める形式）</param>
	void Initialize(ModelCore* modelManager, const std::string& directorPath, const std::string& filename);

	/// <summary>CPU フェーズのみ（.mesh 直読みか Assimp パース）。スレッド安全。読み込み済みなら何もしない</summary>
	/// <param name="directoryPath">モデルのあるディレクトリ</param>
	/// <param name="filename">モデルのファイル名</param>
	void LoadCPU(const std::string& directoryPath, const std::string& filename);

	/// <summary>GPU フェーズのみ（リソース作成）メインスレッドのみ。BeginGPUAsync → 待つ → FinishGPU を続けて行う</summary>
	/// <param name="modelCore">DirectXCore の取得元として覚える</param>
	/// <param name="dxCore">バッファを作るのに使う</param>
	void InitializeGPU(ModelCore* modelCore, DirectXCore* dxCore);

	/// <summary>
	/// GPU フェーズの前半（メインスレッドのみ）：頂点・インデックスのバッファを作り、DirectStorage 経路なら読み込みを積むだけ（送らない・待たない）。
	/// true = DirectStorage に積んだ。呼び出し側が DStorageManager::SubmitWithFence で送り、fence が終わってから FinishGPU を呼ぶ。
	/// false = 待つ物は無い（FS 経路・空のモデル・済み）。GPUPending なら続けて FinishGPU を呼んでよい
	/// </summary>
	/// <param name="modelCore">DirectXCore の取得元として覚える</param>
	/// <param name="dxCore">バッファを作るのに使う</param>
	/// <returns>DirectStorage に積んだら true</returns>
	bool BeginGPUAsync(ModelCore* modelCore, DirectXCore* dxCore);
	/// <summary>GPU フェーズの後半（メインスレッドのみ）：バリアを積み、マテリアルとテクスチャを用意して GPUReady にする</summary>
	/// <param name="dxCore">コマンドリストの取得元（GPUPending でなければ何もしない）</param>
	void FinishGPU(DirectXCore* dxCore);

	/// <summary>
	/// 部位ごとに分割ドローする（GPUReady でない・空なら描かない）。Object3DManager のルートの
	/// [0]=PS b0 マテリアル / [2]=PS t0 ベースカラー / [10]=PS t2 法線 / [15]=PS t4 ハイト / [16][17]=PS t6/t7 地形の層 を貼り替える
	/// </summary>
	/// <param name="dxCore">コマンドリストの取得元</param>
	void Draw(DirectXCore* dxCore);

	/// <summary>ID Pass 用：VBV/IBV をバインドして DrawIndexedInstanced するだけの軽量版</summary>
	/// <param name="dxCore">コマンドリストの取得元</param>
	void DrawIdPass(DirectXCore* dxCore);

	/// <summary>シャドウパス用：VBV/IBV をバインドして DrawIndexedInstanced するだけ（深度のみ）</summary>
	/// <param name="dxCore">コマンドリストの取得元</param>
	void DrawShadowPass(DirectXCore* dxCore);

	/// <summary>
	/// 発光パス用：submesh ごとにマテリアル（PS b0 = ルート[1]）とベースカラー（ルート[3]）を貼って描く。
	/// 発光している submesh だけ描く。includeAll なら全部（六角形のディゾルブの発光帯を光らせるとき）
	/// </summary>
	/// <param name="dxCore">コマンドリストの取得元</param>
	/// <param name="includeAll">発光していない部位も描くなら true</param>
	void DrawBloomPass(DirectXCore* dxCore, bool includeAll);

	/// <summary>
	/// 部位の発光。.mat のパスに materialName を含む submesh 全部（空なら全部）に emissive を書く。
	/// textureWeight = 1 で発光にベースカラーのテクスチャを掛ける（文字を描いた板の文字だけを光らせる）
	/// </summary>
	/// <param name="materialName">.mat のパスに含む語</param>
	/// <param name="emissive">発光の RGB</param>
	/// <param name="textureWeight">ベースカラーを掛ける割合（0〜1）</param>
	/// <returns>書いた部位の数</returns>
	int SetEmissiveByMaterialName(const std::string& materialName, const Vector3& emissive, float textureWeight = 0.0f);
	/// <summary>全 submesh の発光を 0 に戻す</summary>
	void ClearEmissive();
	/// <summary>
	/// 部位のベースカラーの差し替え。.mat のパスに materialName を含む submesh 全部に texturePath を貼る（読み込みもする）。
	/// モデルはファイル名で共有なので、使い終わったら元へ戻すこと
	/// </summary>
	/// <param name="materialName">.mat のパスに含む語</param>
	/// <param name="texturePath">貼るテクスチャのパス</param>
	/// <returns>書いた数（GPU の準備前・読めないテクスチャなら 0）</returns>
	int SetTextureByMaterialName(const std::string& materialName, const std::string& texturePath);
	/// <summary>発光している submesh があるか</summary>
	/// <returns>あれば true</returns>
	bool HasEmissive() const;

	/// <summary>モデルデータを返す</summary>
	/// <returns>モデルデータ（DirectStorage 経路では頂点・インデックスは空）</returns>
	const ModelData& GetModelData() const { return modelData_; }

	/// <summary>モデル空間の包む球。IsValid() が false なら大きさ不明</summary>
	/// <returns>包む球</returns>
	const BoundingSphere& GetBoundingSphere() const { return bounds_; }

	// ---- インスタンシング描画（外部の専用レンダラが VBV/IBV とサブメッシュ情報を借りる）----
	// 頂点/インデックスバッファはモデル全体で 1 本。サブメッシュは index 範囲でその部分を指す。
	/// <summary>部位の表を返す（インスタンシング描画用）</summary>
	/// <returns>部位の配列</returns>
	const std::vector<RenderSubmesh>& GetSubmeshes() const { return submeshes_; }
	/// <summary>モデル全体の頂点バッファビューを返す</summary>
	/// <returns>VBV</returns>
	const D3D12_VERTEX_BUFFER_VIEW&   GetVertexBufferView() const { return vertexBufferView_; }
	/// <summary>モデル全体のインデックスバッファビューを返す</summary>
	/// <returns>IBV</returns>
	const D3D12_INDEX_BUFFER_VIEW&    GetIndexBufferView() const  { return indexBufferView_; }

	/// <summary>ImGui/PSO切り替えから Material にアクセスするためのGetter</summary>
	/// <returns>マテリアル（未作成なら nullptr）</returns>
	Material* GetMaterialPointer() const { return material_; }

	/// <summary>Inspector からテクスチャを差し替える際に使う（GPU リソース作成は呼び出し側で行う）</summary>
	/// <param name="filePath">テクスチャのパス</param>
	void SetTextureFilePath(const std::string& filePath) {
		textureFilePath_ = filePath;
		modelData_.materialData.textureFilePath = filePath;
		if (!submeshes_.empty()) submeshes_[0].textureFilePath = filePath;
	}
	/// <summary>先頭の部位のテクスチャのパスを返す</summary>
	/// <returns>パス</returns>
	const std::string& GetTextureFilePath() const { return textureFilePath_; }

	/// <summary>法線マップの差し替え（GPU リソース作成・useNormalMap 反映は呼び出し側）</summary>
	/// <param name="filePath">法線マップのパス</param>
	void SetNormalMapFilePath(const std::string& filePath) {
		normalMapFilePath_ = filePath;
		modelData_.materialData.normalMapFilePath = filePath;
		if (!submeshes_.empty()) submeshes_[0].normalMapFilePath = filePath;
	}
	/// <summary>先頭の部位の法線マップのパスを返す</summary>
	/// <returns>パス（無ければ空）</returns>
	const std::string& GetNormalMapFilePath() const { return normalMapFilePath_; }

	// ロード状態の確認
	/// <summary>CPU フェーズが終わったか</summary>
	/// <returns>CPUReady 以降なら true</returns>
	bool IsCPUReady() const { return loadState_ >= LoadState::CPUReady; }
	/// <summary>描ける状態か</summary>
	/// <returns>GPUReady なら true</returns>
	bool IsGPUReady() const { return loadState_ == LoadState::GPUReady; }
	/// <summary>GPU の転送待ちか</summary>
	/// <returns>GPUPending なら true</returns>
	bool IsGPUPending() const { return loadState_ == LoadState::GPUPending; }

private:

	/// <summary>頂点バッファを作る（DirectStorage 経路は DEFAULT heap に作って読み込みを積むだけ）</summary>
	/// <param name="dxCore">デバイスの取得元</param>
	void CreateVertexData(DirectXCore* dxCore);

	/// <summary>部位ごとのマテリアル CB を作って既定値で初期化する</summary>
	/// <param name="dxCore">デバイスの取得元</param>
	void CreateMaterialData(DirectXCore* dxCore);

	/// <summary>インデックスバッファを作る（DirectStorage 経路は DEFAULT heap に作って読み込みを積むだけ）</summary>
	/// <param name="dxCore">デバイスの取得元</param>
	void CreateIndexData(DirectXCore* dxCore);

	/// <summary>DirectStorage で転送した頂点・インデックスにバリアを積み、ビューを作る（FS 経路は Create* で済んでいるので何もしない）</summary>
	/// <param name="dxCore">コマンドリストの取得元</param>
	void FinishBufferData(DirectXCore* dxCore);

	/// <summary>assimp のノードを再帰的に Node へ変換する</summary>
	/// <param name="node">assimp のノード</param>
	/// <returns>変換した Node</returns>
	Node ReadNode(aiNode* node);

	/// <summary>assimp でモデルを読み、頂点・インデックス・ノード階層を modelData_ に入れる</summary>
	/// <param name="directoryPath">モデルのあるディレクトリ</param>
	/// <param name="filename">モデルのファイル名</param>
	void LoadModel(const std::string& directoryPath, const std::string& filename);

	/// <summary>.mesh バイナリ（v3 / v4。Cooker が生成したもの）を直読みし、部位と .mat のテクスチャのパスを集める</summary>
	/// <param name="directoryPath">.mesh のあるディレクトリ</param>
	/// <param name="filename">.mesh のファイル名</param>
	void LoadMeshBinary(const std::string& directoryPath, const std::string& filename);

	/// <summary>.mtl を読み、map_Kd のテクスチャのパスを取り出す（開けなければ assert）</summary>
	/// <param name="directoryPath">.mtl のあるディレクトリ</param>
	/// <param name="filename">.mtl のファイル名</param>
	/// <returns>テクスチャのパスを入れた MaterialData</returns>
	static MaterialData LoadMaterialTemplateFile(const std::string& directoryPath, const std::string& filename);

};

