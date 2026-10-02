#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"

class DirectXCore;
class Object3DManager;
class WaterReflection;
class Camera;

/// <summary>
/// 浅い水面（1枚の水平な板）。専用 PSO で描く。
///
/// 1ピクセルで次を合成する：
///   - 反射：反射 RT（映す物）＋ 反射ベクトルで引いた cubemap（空）
///   - 透過：屈折レイが水底（水面 - depth）に当たった位置の床テクスチャ。平行光源・影で照らし、水の吸収を掛ける
///   - フレネル（Schlick）で反射と透過を混ぜ、最後に距離フォグ
///
/// レジスタは Object3D と取り決めを共有する（b1=平行光源 / b5,t3,s1,s2=シャドウ / b6=フォグ）。
/// t2 は PBR 法線用に空けておく。
/// </summary>
class WaterSurface {
public:
	/// <summary>PS b0。WaterSurface.PS.hlsl の WaterParams と 1:1（16 バイト境界に注意）</summary>
	struct Params {
		Vector4 waterColor{ 0.10f, 0.45f, 0.55f, 1.0f };   // 深い所で寄っていく水の色
		Vector3 absorption{ 3.0f, 1.2f, 0.8f };             // 吸収係数 [1/m]。赤ほど早く減る
		float   depth = 0.1f;                               // 水深 [m]
		Vector3 cameraPosition{};                           // 毎フレーム内部で設定
		float   waterHeight = 0.0f;                         // 水面の高さ（WaterReflection と揃える）
		Vector2 screenSize{};                               // 毎フレーム内部で設定
		float   fresnelF0 = 0.02f;                          // 水の垂直入射反射率
		float   reflectionIntensity = 1.0f;
		float   floorTiling = 0.5f;                         // 床テクスチャの繰り返し [回/m]
		float   ambient = 0.3f;                             // 床の環境光
		float   ior = 1.333f;                               // 屈折率（空気→水）
		float   skyIntensity = 1.0f;
		int     hasReflection = 0;                          // 毎フレーム内部で設定
		int     debugView = 0;                              // 0=合成 / 1=床のみ / 2=反射のみ / 3=フレネル / 4=法線
		float   padding[2]{};
		Matrix4x4 reflectionViewProj{};                     // 毎フレーム内部で設定（反射 RT を引く投影）
		Vector3 shadingEye{};                               // 毎フレーム内部で設定（フレネル・反射方向・屈折の視点）
		float   padding2 = 0.0f;
		// ----- さざ波（同心円）：中心からランダムな間隔で波の束が出て外へ広がる -----
		Vector2 rippleCenter{};                             // 波紋の中心（ワールド XZ）
		float   ringAmplitude = 0.04f;                      // 束の最大の高さ [m] 相当
		float   ringWavelength = 1.6f;                      // 最大の波長 [m]（束ごとに 0.5〜1.0 倍）
		float   ringSpeed = 0.8f;                           // 外向きの伝播速度 [m/s]
		float   ringFalloff = 40.0f;                        // 振幅が 1/e になる距離 [m]
		// ----- 揺らぎ（ノイズ2オクターブを別方向に流す） -----
		float   noiseAmplitude = 0.02f;                     // [m] 相当
		float   noiseScale = 0.8f;                          // [回/m]
		float   noiseSpeed = 0.25f;                         // [m/s]
		float   distortion = 0.03f;                         // 反射 RT の UV をずらす量
		float   time = 0.0f;                                // Update で内部加算
		float   ringInterval = 3.0f;                        // 束が出る平均間隔 [s]
		float   ringJitter = 0.6f;                          // 間隔のばらつき（0..1）。大きいほど出ない回も増える
		float   ringMinAmplitude = 0.2f;                    // 束の高さの下限（最大に対する比）。小さい波の混ざり具合
		float   ringPacketLength = 2.5f;                    // 1つの束の長さ（波長の数）
		float   ringMaxRadius = 30.0f;                      // 波紋が届く距離 [m]
	};

	WaterSurface();
	~WaterSurface();

	void Initialize(DirectXCore* dxCore, Object3DManager* object3DManager, const std::string& floorTexturePath);

	/// <summary>波の時間を進める</summary>
	void Update(float deltaTime) { params_.time += deltaTime; }

	/// <summary>
	/// 描画。シーン RT（本体 DSV）がバインド済みで、不透明物を描いた後に呼ぶ。
	/// reflection が nullptr / 未準備なら空の反射だけになる。
	/// </summary>
	void Draw(const Camera& camera, const std::string& skyCubemapPath, const WaterReflection* reflection);

	Params& GetParams() { return params_; }
	void SetWaterHeight(float height) { params_.waterHeight = height; }
	/// <summary>水面の一辺の長さ [m]。followCamera 時はカメラの真下を中心にする</summary>
	void SetSize(float size) { size_ = size; }
	void SetFollowCamera(bool follow) { followCamera_ = follow; }
	void SetCenter(const Vector3& center) { center_ = center; }
	/// <summary>同心円のさざ波の中心（ワールド座標。Y は無視）</summary>
	void SetRippleCenter(const Vector3& center) { params_.rippleCenter = { center.x, center.z }; }

	void OnImGui();

private:
	struct TransformForGPU {
		Matrix4x4 WVP;
		Matrix4x4 World;
	};

	void CreateRootSignature();
	void CreatePipelineState();
	void CreateVertexBuffer();

	DirectXCore* dxCore_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	std::string floorTexturePath_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vertexBufferView_{};

	Microsoft::WRL::ComPtr<ID3D12Resource> transformResource_;
	TransformForGPU* transformData_ = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> paramsResource_;
	Params* paramsData_ = nullptr;

	Params params_;
	Vector3 center_{};
	float size_ = 400.0f;
	bool followCamera_ = true;
};
