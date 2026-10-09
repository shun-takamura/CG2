#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <memory>
#include <vector>
#include "Matrix4x4.h"
#include "Vector3.h"
#include "Vector4.h"

class DirectXCore;
class SRVManager;
class Object3DManager;
class Object3DInstance;
class AnimatedObject3DInstance;
class Camera;
class RenderTexture;
struct CameraForGPU;

/// <summary>
/// 平面リフレクション（水面・鏡）。
/// 水平な反射面（y = waterHeight）で鏡像にした ViewProj を持ち、登録された物だけを反射 RT へ描く。
///
/// - 鏡像 ViewProj は「鏡像行列 × 本編カメラの ViewProj」なので、反射 RT は画面座標で水面と1:1に対応する
///   （水面のシェーダは自分の画面座標でそのまま引ける）。
/// - CB は鏡側が1個だけ持つ。映る物は自分の World を使い回すので、映る物の数だけ CB が増えることはない。
/// - 水面より下の物は SV_ClipDistance で落とす（斜めニアクリップの代わり）。
/// - 空は反射 RT に描かず、水面のシェーダが反射ベクトルで cubemap を直接引く。
///   反射 RT の α=0 の所が「何も映っていない＝空」になる。
/// </summary>
class WaterReflection {
public:
	WaterReflection();
	~WaterReflection();

	/// <param name="resolutionScale">反射 RT の解像度倍率（本体解像度に対する比。0.25〜1.0）</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
		float resolutionScale = 1.0f);

	//==============================
	// 映す物の登録
	//==============================
	void AddTarget(Object3DInstance* target);
	void RemoveTarget(Object3DInstance* target);
	/// <summary>アニメーションするモデル（自機・敵など）。スキニングは反射のパスの中で済ませる</summary>
	void AddTarget(AnimatedObject3DInstance* target);
	/// <summary>静的・アニメーションの両方を外す</summary>
	void ClearTargets();

	/// <summary>
	/// 反射 RT を更新する。シーン描画の最初（本編の不透明物より前）に呼ぶ。
	/// 呼んだ後は RTV/DSV が反射 RT のままなので、呼び出し側でシーン RT を貼り直すこと
	/// （ビューポート／シザーは本体解像度に戻して返す）。
	/// 描画距離内に映す物が無く、前フレームも空だったならパスごと飛ばす。
	/// </summary>
	void Render(const Camera& camera);

	/// <summary>
	/// 視点を行列で直接渡す版。水面を描くカメラと別の視点で反射を作るとき
	/// （デバッグカメラ中にゲームカメラ基準の反射を確認する等）に使う。
	/// </summary>
	void Render(const Matrix4x4& viewProjection, const Vector3& eyePosition);

	/// <summary>
	/// エフェクト（GPU パーティクル・エフェクトの Primitive）も反射 RT に描くか。既定 false（タイトルは描かない）。
	/// ビルボードに View 行列が要るので、Render(const Camera&) の時だけ描く。
	/// </summary>
	void SetDrawEffects(bool draw) { drawEffects_ = draw; }

	//==============================
	// パラメータ
	//==============================
	void SetWaterHeight(float height) { waterHeight_ = height; }
	float GetWaterHeight() const { return waterHeight_; }
	/// <summary>反射側の描画距離（本編カメラから物を包む球の表面まで）。これより遠い物は映さない</summary>
	void SetFarClip(float distance) { farClip_ = distance; }
	float GetFarClip() const { return farClip_; }

	//==============================
	// 水面シェーダ向け
	//==============================
	/// <summary>反射 RT が SRV として読める状態か（最初の Render 以降 true）</summary>
	bool IsReady() const { return srvReady_; }
	/// <summary>このフレームの反射 RT に何か描かれているか（false なら空だけ映せばよい）</summary>
	bool HasContent() const { return hasContent_; }
	D3D12_GPU_DESCRIPTOR_HANDLE GetSrvHandle() const;

	/// <summary>反射に使った視点の ViewProj（鏡像前）。水面はこれで投影して反射 RT を引く</summary>
	const Matrix4x4& GetSourceViewProjection() const { return sourceViewProjection_; }
	/// <summary>反射に使った視点の位置。フレネル・反射方向・屈折の基準になる</summary>
	const Vector3& GetSourceEyePosition() const { return sourceEyePosition_; }

	/// <summary>このフレームに反射 RT へ描いた物の数（PEPPER/ImGui の確認用）</summary>
	uint32_t GetDrawnCount() const { return drawnCount_; }

	void OnImGui();

private:
	// VS b1。反射用 VS の ReflectionCameraBuffer と 1:1
	struct ReflectionViewProjForGPU {
		Matrix4x4 viewProj;
		Vector4 clipPlane;
	};

	// VS b1（先頭）と PS b2（256 バイト先）を1本のバッファに同居させる
	static constexpr uint32_t kCameraCBOffset = 256;

	void CreateDepthBuffer(uint32_t width, uint32_t height);
	/// <param name="view">本編カメラの View。null ならエフェクトは描かない</param>
	void RenderInternal(const Matrix4x4& viewProjection, const Vector3& eyePosition, const Matrix4x4* view);

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;

	std::unique_ptr<RenderTexture> renderTexture_;
	Microsoft::WRL::ComPtr<ID3D12Resource> depthBuffer_;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> dsvHeap_;

	Microsoft::WRL::ComPtr<ID3D12Resource> cameraResource_;
	ReflectionViewProjForGPU* viewProjData_ = nullptr;
	CameraForGPU* cameraData_ = nullptr;

	std::vector<Object3DInstance*> targets_;
	std::vector<Object3DInstance*> visibleTargets_;
	std::vector<AnimatedObject3DInstance*> animatedTargets_;
	std::vector<AnimatedObject3DInstance*> visibleAnimatedTargets_;

	Matrix4x4 sourceViewProjection_{};
	Vector3 sourceEyePosition_{};

	float waterHeight_ = 0.0f;
	float farClip_ = 500.0f;
	// 水面ぎりぎりの物が接地部で途切れないよう、クリップ平面を少しだけ下げる
	float clipOffset_ = 0.01f;

	bool srvReady_ = false;
	bool hasContent_ = false;
	bool drawEffects_ = false;
	uint32_t drawnCount_ = 0;
};
