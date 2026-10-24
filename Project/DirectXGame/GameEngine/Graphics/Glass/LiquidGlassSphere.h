#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <string>
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"

class DirectXCore;
class SRVManager;
class RippleSimulation;
class Camera;

/// <summary>
/// 液体の入った密閉のガラスの球。専用 PSO で、不透明物を描いた後に乗算済み α で重ねる。
///
/// 液体はメッシュを持たない。PS でピクセルごとに視線と球（解析解）・液面（平面）の交わりを解き、
/// 液体の中を通る長さで発光と不透明度を決める（芯は白に寄せる）。
///   - 量（SetFill）は体積の割合。球は対称なので、液面の向きによらず「中心から液面までの距離」だけで量が決まる
///   - 液面はワールドで水平を保ち、球の回転（SetContainerRotation）の角速度に引きずられて傾き、ばね＋減衰で戻る
///   - 波紋は RippleSimulation（球の中心まわりの正方形）の高さマップの傾きを液面の法線に足す
/// ガラスはフレネル（Schlick）で IBL の映り込み・平行光源のハイライト・縁の明るさを足す。
/// 光る分は DrawBloomPass で発光 RT にも描く（シーンの DrawBloomSources から呼ぶ）。
///
/// レジスタ：VS b0=変換 / PS b0=Params, b1=平行光源, t0=環境 cubemap, t1=波紋の高さ, s0=ラップ, s1=クランプ。
/// </summary>
class LiquidGlassSphere {
public:
	/// <summary>見た目の調整値（JSON・ImGui で触る物）</summary>
	struct Appearance {
		// ----- ガラス -----
		Vector3 glassTint{ 1.0f, 1.0f, 1.0f };      // 映り込みに掛ける色
		float   fresnelF0 = 0.04f;                   // 垂直入射の反射率（ガラス）
		Vector3 rimColor{ 0.35f, 0.5f, 0.6f };       // 縁の明るさの色（HDR 可）
		float   rimPower = 3.0f;                     // 縁の明るさの鋭さ
		float   reflectionIntensity = 1.0f;          // IBL の映り込みの強さ
		float   specularIntensity = 1.5f;            // 平行光源のハイライト
		float   specularPower = 128.0f;
		float   glassAlpha = 0.3f;                   // ガラスそのものの不透明度の下限
		float   backRimIntensity = 0.5f;             // 奥側の内面の縁（手前の縁に対する比）
		float   lensIor = 1.0f;                      // 液体を見るときの屈折の強さ（1 で曲げない）
		float   rimBloom = 1.3f;                     // 縁を発光 RT にどれだけ出すか
		// ----- 液体 -----
		Vector3 liquidColor{ 0.0f, 0.641f, 1.0f };   // 液体の色（2026-10-24 ユーザーが ImGui で決めた値）
		float   liquidGlow = 0.3f;                    // 発光の強さ
		float   liquidDensity = 4.0f;                // 中を通る長さに対する光の濃さ [1/m]
		float   liquidAbsorption = 4.0f;             // 中を通る長さに対する不透明度 [1/m]
		float   coreWhite = 0.5f;                    // 芯を白へ寄せる量
		float   corePower = 2.0f;                    // 芯の広がり（大きいほど中心だけ）
		Vector3 surfaceColor{ 0.749f, 0.937f, 1.0f };   // 液面の色
		float   surfaceIntensity = 0.6f; 
		float   meniscusWidth = 0.008f;              // ガラスに液面が触れる線の幅 [m]
		float   meniscusIntensity = 1.5f;
		float   bloomScale = 0.8f;                   // 発光 RT に出す倍率
		float   rippleNormalScale = 1.0f;            // 波紋の傾きを液面の法線に足す倍率
	};

	/// <summary>液面の揺れ（ばね＋減衰）と波紋の調整値</summary>
	struct Motion {
		float fillFollow = 3.0f;                     // 量が目標へ寄る速さ [1/s]（指数で寄せる）
		float sloshStiffness = 30.0f;                // 液面の傾きを戻すばね [1/s²]
		float sloshDamping = 3.0f;                   // 減衰 [1/s]
		float sloshDrive = 0.25f;                    // 球の角速度 [rad/s] に対して液面が傾く量 [rad]
		float sloshMaxTilt = 0.5f;                   // 液面の傾きの上限 [rad]
		float rippleCoverage = 0.3f;                 // 波のシミュレーションの範囲に対する球の直径の割合
		float sloshRippleStrength = 0.004f;          // 揺れが変わった量 [rad/s] あたりの波源の深さ [m]
		float sloshRippleRadius = 0.03f;             // 揺れで立つ波源の半径 [m]
		float ambientRippleInterval = 0.6f;          // 静かな揺れ：波源を落とす平均間隔 [s]（0 で無し）
		float ambientRippleStrength = 0.0015f;       // [m]
		float ambientRippleRadius = 0.02f;
		float ambientRippleSpread = 0.0f;            // 静かな揺れを落とす範囲（液面の円に対する割合。0 で中央だけ）
	};

	LiquidGlassSphere();
	~LiquidGlassSphere();

	/// <param name="divisions">球の分割数（輪郭は真球で塗るので、多角形の粗さはほぼ見えない）</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t divisions = 48);

	/// <summary>液面の揺れ・量・波紋の時間を進める</summary>
	void Update(float deltaTime);
	/// <summary>波のシミュレーションを進める。シーンの Draw の最初（SRV ヒープ設定後、球を描く前）に呼ぶ</summary>
	void DispatchSimulation();

	/// <summary>
	/// 描画。シーン RT（本体 DSV）がバインド済みで、不透明物を描いた後に呼ぶ。
	/// ルートシグネチャを替えるので、後で Object3D を描くなら DrawSetting し直すこと。
	/// </summary>
	/// <param name="environmentCubemapPath">映り込みの cubemap（TextureManager で読み込み済みのもの）</param>
	void Draw(const Camera& camera, const std::string& environmentCubemapPath);
	/// <summary>発光 RT に光る分だけを加算で描く（シーンの DrawBloomSources から）</summary>
	void DrawBloomPass(const Camera& camera, const std::string& environmentCubemapPath);

	void SetCenter(const Vector3& center) { center_ = center; }
	const Vector3& GetCenter() const { return center_; }
	void SetRadius(float radius) { radius_ = radius; }
	float GetRadius() const { return radius_; }

	/// <summary>
	/// 球の回転（ワールド、オイラー XYZ [rad]）。球の見た目は回っても変わらないが、
	/// 前のフレームとの差（角速度）で液面が揺れる。X・Z 軸まわりだけが液面を傾ける
	/// </summary>
	void SetContainerRotation(const Vector3& rotation) { containerRotation_ = rotation; }

	/// <summary>量（体積の割合 0..1）。immediate=false なら Motion::fillFollow で寄せる</summary>
	void SetFill(float amount, bool immediate = false);
	float GetFill() const { return fill_; }
	float GetFillTarget() const { return fillTarget_; }

	/// <summary>液面に波源を入れる（XZ はワールド。Y は無視）</summary>
	void AddRipple(const Vector3& position, float radius, float strength);
	/// <summary>液面の傾きと揺れを止めて平らにする</summary>
	void ResetMotion();

	/// <summary>液面の法線（ワールド・上向き）</summary>
	Vector3 GetSurfaceNormal() const;
	/// <summary>中心から液面までの距離（液面の法線の向き）。-R=空 / +R=満杯</summary>
	float GetSurfaceOffset() const;

	Appearance& GetAppearance() { return appearance_; }
	Motion& GetMotion() { return motion_; }
	RippleSimulation* GetRippleSimulation() const { return rippleSimulation_.get(); }

	/// <summary>体積の割合 → 底からの液面の高さ（半径 1 の球で 0..2）</summary>
	static float FillToHeight(float amount);

	void OnImGui();

private:
	// LiquidGlassSphere.PS.hlsl の GlassParams と 1:1（16 バイト境界に注意）
	struct ParamsForGPU {
		Vector3 center{};
		float   radius = 1.0f;
		Vector3 cameraPosition{};
		float   surfaceOffset = 0.0f;
		Vector3 surfaceNormal{ 0.0f, 1.0f, 0.0f };
		float   time = 0.0f;
		Vector3 glassTint{};
		float   fresnelF0 = 0.0f;
		Vector3 rimColor{};
		float   rimPower = 0.0f;
		float   reflectionIntensity = 0.0f;
		float   specularIntensity = 0.0f;
		float   specularPower = 0.0f;
		float   glassAlpha = 0.0f;
		float   backRimIntensity = 0.0f;
		float   lensIor = 1.0f;
		int     bloomPass = 0;
		float   rimBloom = 0.0f;
		Vector3 liquidColor{};
		float   liquidGlow = 0.0f;
		float   liquidDensity = 0.0f;
		float   liquidAbsorption = 0.0f;
		float   coreWhite = 0.0f;
		float   corePower = 1.0f;
		Vector3 surfaceColor{};
		float   surfaceIntensity = 0.0f;
		float   meniscusWidth = 0.0f;
		float   meniscusIntensity = 0.0f;
		float   bloomScale = 0.0f;
		float   rippleNormalScale = 0.0f;
		float   rippleUvScale = 0.0f;
		float   rippleTexel = 0.0f;
		int     rippleEnabled = 0;
		float   padding0 = 0.0f;
	};
	struct TransformForGPU {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(bool bloomPass);
	void CreateMesh(uint32_t divisions);
	void WriteParams(ParamsForGPU& dst, const Camera& camera, bool bloomPass) const;
	void DrawImpl(const Camera& camera, const std::string& environmentCubemapPath, bool bloomPass);
	void UpdateSlosh(float deltaTime);
	void UpdateAmbientRipples(float deltaTime);
	float NextRandom01();

	DirectXCore* dxCore_ = nullptr;
	std::unique_ptr<RippleSimulation> rippleSimulation_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};
	D3D12_INDEX_BUFFER_VIEW indexBufferView_{};
	uint32_t indexCount_ = 0;
	float meshScale_ = 1.0f;   // 多角形の面が真球の外側に来るように広げる倍率

	// 通常の描画と発光パスは同じフレームで描くので、CB を分ける
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	TransformForGPU* transformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	ParamsForGPU* paramsData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> bloomParamsResource_;
	ParamsForGPU* bloomParamsData_ = nullptr;

	Appearance appearance_;
	Motion motion_;

	Vector3 center_{};
	float radius_ = 0.5f;
	float time_ = 0.0f;

	float fill_ = 0.6f;
	float fillTarget_ = 0.6f;

	// 液面の傾き（x = X 軸まわり、y = Z 軸まわり [rad]）
	Vector3 containerRotation_{};
	Vector3 prevContainerRotation_{};
	bool hasPrevRotation_ = false;
	Vector2 prevAngularVelocity_{};
	Vector2 tilt_{};
	Vector2 tiltVelocity_{};
	float sloshRippleCooldown_ = 0.0f;
	float ambientRippleTimer_ = 0.0f;
	uint32_t randomState_ = 0x6C8E9CF5u;

	// ImGui の確認用
	float debugRippleStrength_ = 0.006f;
};
