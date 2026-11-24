#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <string>
#include <vector>
#include "Matrix4x4.h"
#include "Quaternion.h"
#include "Vector3.h"
#include "Vector4.h"
#include "VoronoiFracture.h"

class Camera;
class DirectXCore;
class Object3DManager;
class SRVManager;

/// <summary>
/// VoronoiFracture で割った岩を、同じ割れ方のまま何個でも描くレンダラ。
/// 頂点は「割る前の岩」と「全破片」を 1 本のバッファに連結し、岩・破片ごとの行列を StructuredBuffer に詰めて
/// 割る前の岩をまとめて 1 回、割れた岩の全破片をまとめて 1 回（計 2 ドローコール）で描く。
/// ディゾルブ中の岩だけは穴から裏面が見えるので、背面カリングなしの組として別に描く（最大 4 ドローコール）。
/// テクスチャは三平面投影（UV 不要）。外側と断面を頂点の印で切り替える。ディゾルブで岩・破片ごとに消せる。
/// シーンの 3D の描画の途中（RTV と書き込み用 DSV を張った状態）で Draw を呼ぶ。
/// </summary>
class FracturedMeshRenderer {
public:
	/// <summary>1 個の破片の姿勢（割る前の位置からの移動と、重心まわりの回転）</summary>
	struct FragmentPose {
		Vector3 offset{ 0.0f, 0.0f, 0.0f };          // 割る前の岩の空間での移動
		Quaternion rotation{ 0.0f, 0.0f, 0.0f, 1.0f }; // 重心まわりの回転（割る前の岩の空間）
		float visibility = 1.0f;                     // 1 = 全部見える / 0 = 消えた（ディゾルブ）
	};

	/// <summary>描く 1 個の岩</summary>
	struct RockDraw {
		Matrix4x4 world{};          // 岩のワールド行列（拡大縮小・回転・移動）
		float textureScale = 1.0f;  // 岩の大きさの倍率（模様の密度を岩の大きさによらずそろえる。普通は拡大率と同じ）
		float visibility = 1.0f;    // 岩全体の見えている量（破片の visibility と掛け合わせる）
		bool broken = false;        // true なら fragments の姿勢で破片を描く
		const std::vector<FragmentPose>* fragments = nullptr; // broken のとき。足りない破片は割る前の位置に描く
	};

	/// <summary>見た目の調整値</summary>
	struct Appearance {
		float textureTiling = 0.7f;   // 岩の空間 1 あたりの模様の繰り返し数
		float normalStrength = 1.0f;
		float ambient = 0.25f;
		float interiorEmissive = 0.0f;
		float backfaceShade = 0.35f;  // ディゾルブの穴から見える裏面（岩の内側）の明るさ。断面の色に掛ける
		Vector4 exteriorTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		Vector4 interiorTint{ 1.0f, 1.0f, 1.0f, 1.0f };
		float dissolveEdgeWidth = 0.08f;
		float dissolveNoiseScale = 3.0f;
		Vector3 dissolveEdgeColor{ 1.0f, 0.55f, 0.2f };
	};

	/// <summary>テクスチャのパス（外側と断面の色・法線。法線は OpenGL 規約）</summary>
	struct TextureSet {
		std::string exteriorBase = "Resources/Textures/Rocks/RockExterior_BaseColor.dds";
		std::string exteriorNormal = "Resources/Textures/Rocks/RockExterior_NormalMap.dds";
		std::string interiorBase = "Resources/Textures/Rocks/RockInterior_BaseColor.dds";
		std::string interiorNormal = "Resources/Textures/Rocks/RockInterior_NormalMap.dds";
	};

	/// <summary>ルートシグネチャ・PSO・フレームの CB を作り、テクスチャを読む</summary>
	/// <param name="dxCore">デバイスとコマンドリストの取得元</param>
	/// <param name="srvManager">岩・破片ごとの行列の SRV を作るのに使う</param>
	/// <param name="object3DManager">フォグの CB を借りる（nullptr ならフォグなし）</param>
	/// <param name="maxRocks">1 回の Draw で描ける岩の数の上限</param>
	/// <param name="textures">使うテクスチャ</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager, uint32_t maxRocks,
		const TextureSet& textures = TextureSet{});
	/// <summary>GPU リソースと SRV を解放する</summary>
	void Finalize();

	/// <summary>割った結果を頂点・インデックスバッファへ上げる（読み込み時に 1 回。描画中に呼ばない）</summary>
	/// <param name="result">VoronoiFracture::Build の結果</param>
	void SetMesh(const VoronoiFracture::Result& result);

	/// <summary>岩をまとめて描く</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="rocks">描く岩（maxRocks を超えた分は描かない）</param>
	void Draw(const Camera& camera, const std::vector<RockDraw>& rocks);

	/// <summary>破片の数を返す</summary>
	/// <returns>SetMesh した結果の破片の数</returns>
	uint32_t GetFragmentCount() const { return static_cast<uint32_t>(fragments_.size()); }
	/// <summary>破片の重心・体積などを返す（破片を動かす側が使う）</summary>
	/// <returns>SetMesh した結果の破片</returns>
	const std::vector<VoronoiFracture::Fragment>& GetFragments() const { return fragments_; }
	/// <summary>見た目の調整値を返す（書き換えると次の Draw から反映）</summary>
	/// <returns>見た目の調整値</returns>
	Appearance& GetAppearance() { return appearance_; }

	/// <summary>破片の姿勢から、割る前の岩の空間 → ワールドの行列を作る</summary>
	/// <param name="fragment">破片（回転の中心に重心を使う）</param>
	/// <param name="pose">破片の姿勢</param>
	/// <param name="rockWorld">岩のワールド行列</param>
	/// <returns>破片の頂点に掛ける行列</returns>
	static Matrix4x4 MakeFragmentMatrix(const VoronoiFracture::Fragment& fragment, const FragmentPose& pose,
		const Matrix4x4& rockWorld);

#ifdef USE_IMGUI
	/// <summary>見た目の調整値の ImGui を出す</summary>
	void OnImGui();
#endif

private:
	// シェーダの FrameBuffer と同じ並び
	struct FrameCB {
		Matrix4x4 viewProjection;
		Vector3 cameraPosition;
		float textureTiling;
		Vector4 exteriorTint;
		Vector4 interiorTint;
		Vector3 dissolveEdgeColor;
		float normalStrength;
		float ambient;
		float dissolveEdgeWidth;
		float dissolveNoiseScale;
		float interiorEmissive;
		float backfaceShade;
		float pad[3];
	};
	// シェーダの InstanceData と同じ並び
	struct InstanceGPU {
		Matrix4x4 world;
		Vector4 params; // x = 見えている量 / y = 模様の密度の倍率
	};
	// シェーダの DrawConstants（ルート定数）と同じ並び
	struct DrawConstants {
		uint32_t instanceBase;
		uint32_t fragmentMode;
		uint32_t fragmentCount;
	};

	void CreateRootSignature();
	void CreatePipelineState();
	/// <summary>岩・破片ごとの行列のバッファを必要な数ぶん作り直し、SRV を張る</summary>
	/// <param name="count">要素の数</param>
	void EnsureInstanceCapacity(uint32_t count);
	/// <summary>GPU が使っているかもしれないバッファと SRV を、このフレームが終わるまで預かる</summary>
	/// <param name="resource">預けるバッファ（空なら何もしない）</param>
	/// <param name="srvIndex">一緒に返す SRV の番号（無ければ UINT32_MAX）</param>
	void Retire(Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint32_t srvIndex = UINT32_MAX);
	/// <summary>GPU が使い終わった預かり物を解放する</summary>
	void ReleaseRetired();

	// 作り直しで外したバッファ（割り直しは描画の途中ではなく更新で行うが、前のフレームがまだ GPU で動いている）
	struct RetiredResource {
		Microsoft::WRL::ComPtr<ID3D12Resource> resource;
		uint32_t srvIndex = UINT32_MAX;
		uint64_t fenceValue = 0;
	};
	std::vector<RetiredResource> retired_;

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	uint32_t maxRocks_ = 0;
	TextureSet textures_;
	Appearance appearance_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;        // 背面カリングあり（普通の岩）
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineStateNoCull_;  // カリングなし（ディゾルブ中の岩）

	Microsoft::WRL::ComPtr<ID3D12Resource> frameBuffer_;
	FrameCB* frameMapped_ = nullptr;
	// フォグが使えないとき用（enabled = 0 の CB）
	Microsoft::WRL::ComPtr<ID3D12Resource> disabledFogBuffer_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vbView_{};
	D3D12_INDEX_BUFFER_VIEW ibView_{};
	uint32_t intactFirstIndex_ = 0;
	uint32_t intactIndexCount_ = 0;
	uint32_t fragmentFirstIndex_ = 0;
	uint32_t fragmentIndexCount_ = 0;
	std::vector<VoronoiFracture::Fragment> fragments_;

	Microsoft::WRL::ComPtr<ID3D12Resource> instanceBuffer_;
	InstanceGPU* instanceMapped_ = nullptr;
	uint32_t instanceCapacity_ = 0;
	uint32_t instanceSrvIndex_ = UINT32_MAX;
	std::vector<InstanceGPU> instanceScratch_;
};
