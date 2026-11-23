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
	/// <summary>ルートシグネチャ・通常/発光の 2 本の PSO・板の頂点バッファ・定数バッファ（変換×2、Params×2）を作る</summary>
	/// <param name="dxCore">デバイスとコマンドリストの取得元</param>
	void Initialize(DirectXCore* dxCore);

	/// <summary>時間と被弾の光りを進める（実時間でよい）</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void Update(float deltaTime);
	/// <summary>板のワールド行列（原点＝発生装置、XY が板の面、+Z が正面）</summary>
	/// <param name="world">板のワールド行列</param>
	void SetWorld(const Matrix4x4& world) { world_ = world; }
	/// <summary>形・ゆらぎ・色・透け具合を設定する</summary>
	/// <param name="appearance">シールドの見た目</param>
	void SetAppearance(const BeamShieldAppearance& appearance) { appearance_ = appearance; }
	/// <summary>被弾の瞬間に強く光らせる（重なったら強い方）</summary>
	/// <param name="strength">光りの強さ（0〜1 に切り詰める）</param>
	void Flash(float strength = 1.0f);

	/// <summary>シーン RT（本体 DSV）がバインド済みで、不透明物を描いた後に呼ぶ。ルートシグネチャを替える</summary>
	/// <param name="camera">描画に使うカメラ</param>
	void Draw(const Camera& camera);
	/// <summary>発光 RT に光る分だけを加算で描く（シーンの DrawBloomSources から）</summary>
	/// <param name="camera">描画に使うカメラ</param>
	void DrawBloomPass(const Camera& camera);

	/// <summary>調整用の ImGui（Debug だけ）</summary>
	/// <param name="appearance">編集する見た目</param>
	/// <returns>値を変えたら true（Debug 以外は常に false）</returns>
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

	/// <summary>ルートシグネチャを作る（[0]=VS b0 の TransformForGPU、[1]=PS b0 の ParamsForGPU、どちらもルート CBV）</summary>
	void CreateRootSignature();
	/// <summary>板の PSO を作る（両面描画・深度は読むだけ）</summary>
	/// <param name="bloomPass">true で発光 RT（R16G16B16A16_FLOAT・加算）用、false でシーン RT（sRGB・乗算済み α）用</param>
	/// <returns>作った PSO</returns>
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(bool bloomPass);
	/// <summary>板 1 枚（2 三角形）の頂点バッファを作る</summary>
	void CreateMesh();
	/// <summary>板の範囲と Params を CB に書いて 1 回描く</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="bloomPass">true で発光パス用の PSO と CB を使う</param>
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
