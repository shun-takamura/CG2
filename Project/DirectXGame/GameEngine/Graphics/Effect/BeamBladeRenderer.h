#pragma once
#include <wrl.h>
#include <d3d12.h>
#include "Matrix4x4.h"
#include "Vector3.h"
#include "BeamBladeAppearance.h"

class DirectXCore;
class Camera;

/// <summary>
/// ビームの刃（武器の下から前へ伸びる、実体に近い安定した光の刃）。
/// メッシュは菱形の断面を 3 か所（根元・肩・先）でつないだ立体で、頂点は形の係数だけを持ち、寸法は VS が Params から作る
/// （ImGui で形を変えても頂点を作り直さない）。面は ddx/ddy の法線で角ばらせ、縁と稜線を明るく、斜めの面ほど明るくする。
/// 揺らぎ・ノイズは入れない。不透明物の後に乗算済み α で重ね、光る分を DrawBloomPass で発光 RT にも描く。
/// SetActive で伸び縮みする（根元から先へ）。
///
/// レジスタ：VS b0=変換＋形 / PS b0=色。テクスチャは使わない。
/// </summary>
class BeamBladeRenderer {
public:
	void Initialize(DirectXCore* dxCore);

	/// <summary>伸び縮みを進める（実時間でよい）</summary>
	void Update(float deltaTime);
	/// <summary>刃のワールド行列（原点＝根元、+Z へ伸びる、幅は Y・厚みは X）</summary>
	void SetWorld(const Matrix4x4& world) { world_ = world; }
	void SetAppearance(const BeamBladeAppearance& appearance) { appearance_ = appearance; }
	/// <summary>true で伸びる、false で縮んで消える</summary>
	void SetActive(bool active) { active_ = active; }
	/// <summary>すぐに出す／消す（場面の切り替え用）</summary>
	void SnapExtend(float extend);
	/// <summary>少しでも出ていれば true</summary>
	bool IsVisible() const { return extend_ > 0.0f; }

	/// <summary>シーン RT（本体 DSV）がバインド済みで、不透明物を描いた後に呼ぶ。ルートシグネチャを替える</summary>
	void Draw(const Camera& camera);
	/// <summary>発光 RT に光る分だけを加算で描く（シーンの DrawBloomSources から）</summary>
	void DrawBloomPass(const Camera& camera);

	/// <summary>調整用の ImGui（Debug だけ）。変えたら true</summary>
	static bool EditAppearance(BeamBladeAppearance& appearance);

private:
	// BeamBlade.VS.hlsl の BeamBladeTransform と 1:1（16 バイト境界に注意）
	struct TransformForGPU {
		Matrix4x4 WVP;
		Matrix4x4 World;
		float length, halfWidth, halfThickness, baseWidth;
		float tipLength, tipOffset, extend, ridgeOffset;
		float backAnchor, padding0[3];
	};
	// BeamBlade.PS.hlsl の BeamBladeParams と 1:1
	struct ParamsForGPU {
		Vector3 coreColor;
		float intensity;
		Vector3 edgeColor;
		float bodyAlpha;
		Vector3 cameraPosition;
		float edgeAlpha;
		float edgeSharpness;
		float fresnelPower;
		float fresnelStrength;
		float bloomScale;
		int   bloomPass;
		float padding0[3];
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
	UINT vertexCount_ = 0;

	// 通常の描画と発光パスは同じフレームで描くので、CB を分ける
	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	TransformForGPU* transformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> bloomTransformResource_;
	TransformForGPU* bloomTransformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	ParamsForGPU* paramsData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> bloomParamsResource_;
	ParamsForGPU* bloomParamsData_ = nullptr;

	BeamBladeAppearance appearance_;
	Matrix4x4 world_{};
	bool active_ = false;
	float extend_ = 0.0f; // 0..1（見た目の伸びはこれを smoothstep した物）
};
