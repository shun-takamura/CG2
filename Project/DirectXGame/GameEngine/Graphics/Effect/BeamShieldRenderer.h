#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "Matrix4x4.h"
#include "Vector3.h"
#include "BeamShieldAppearance.h"

class DirectXCore;
class Camera;

/// <summary>
/// ビームシールド本体（発生装置から出る半透明の板）。メッシュは板 1 枚で、形・縁のゆらぎ・集中線・透け具合はすべて PS で作る。
/// 形は縦長の六角形の距離関数。縁は角度方向のノイズを時間で動かして細かく変えるが、六角形そのものは変えない。
/// 不透明物を描いた後に乗算済み α で重ね、光る分を DrawBloomPass で発光 RT にも描く。
///
/// レジスタ：VS b0=変換 / PS b0=Params。テクスチャは使わない。
/// </summary>
class BeamShieldRenderer {
public:
	void Initialize(DirectXCore* dxCore);

	/// <summary>時間と被弾の光りを進める（実時間でよい）</summary>
	void Update(float deltaTime);
	/// <summary>板のワールド行列（原点＝発生装置、XY が板の面、+Z が正面）</summary>
	void SetWorld(const Matrix4x4& world) { world_ = world; }
	void SetAppearance(const BeamShieldAppearance& appearance) { appearance_ = appearance; }
	/// <summary>被弾の瞬間に強く光らせる（0..1。重なったら強い方）</summary>
	void Flash(float strength = 1.0f);

	/// <summary>シーン RT（本体 DSV）がバインド済みで、不透明物を描いた後に呼ぶ。ルートシグネチャを替える</summary>
	void Draw(const Camera& camera);
	/// <summary>発光 RT に光る分だけを加算で描く（シーンの DrawBloomSources から）</summary>
	void DrawBloomPass(const Camera& camera);

	/// <summary>調整用の ImGui（Debug だけ）。変えたら true</summary>
	static bool EditAppearance(BeamShieldAppearance& appearance);

private:
	// BeamShield.PS.hlsl の BeamShieldParams と 1:1（16 バイト境界に注意）
	struct ParamsForGPU {
		float halfWidth, halfHeight, topY, shoulderY;
		float waistX, waistY, bottomY, time;
		float edgeNoiseAmp, edgeNoiseFreq, edgeNoiseSpeed, streakCount;
		float streakSharpness, streakSpeed, streakIntensity, coreRadius;
		float coreAlpha, edgeWidth, edgeAlpha, midAlpha;
		Vector3 color;
		float intensity;
		Vector3 edgeColor;
		float flash;
		float flashEdgeNoise;
		float bloomScale;
		int   bloomPass;
		float flashIntensity;
		float innerScale;
		float innerAlpha;
		float padding0[2];
	};
	struct TransformForGPU {
		Matrix4x4 WVP;
		float quadRect[4];   // 板の範囲（ローカル。minX, minY, maxX, maxY）。縁のゆらぎの分だけ形より広げる
	};

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(bool bloomPass);
	void CreateMesh();
	void DrawImpl(const Camera& camera, bool bloomPass);

	DirectXCore* dxCore_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> bloomPipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	// 通常の描画と発光パスは同じフレームで描くので、CB を分ける
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	TransformForGPU* transformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> bloomTransformResource_;
	TransformForGPU* bloomTransformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	ParamsForGPU* paramsData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> bloomParamsResource_;
	ParamsForGPU* bloomParamsData_ = nullptr;

	BeamShieldAppearance appearance_;
	Matrix4x4 world_{};
	float time_ = 0.0f;
	float flash_ = 0.0f;
};
