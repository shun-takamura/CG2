#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
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
	/// <summary>EmitRing で出した波の最大同時数（超えたら一番古いものを上書き）</summary>
	static constexpr int kMaxEmittedRings = 16;

	/// <summary>EmitRing で出した波1つ分。HLSL の EmittedRing と 1:1（32 バイト）</summary>
	struct EmittedRingForGPU {
		Vector2 center{};          // ワールド XZ
		float   emitTime = 0.0f;   // Params::time 基準
		float   amplitude = 0.0f;  // 0 なら未使用の枠
		float   wavelength = 1.0f;
		float   packetLength = 3.0f;
		float   maxRadius = 35.0f;
		float   padding = 0.0f;
	};

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
		float   ambientRingScale = 1.0f;                    // ランダムな波（待機中の波）の振幅倍率。SetAmbientRingScale で補間
		float   padding3[3]{};
		EmittedRingForGPU emittedRings[kMaxEmittedRings]{}; // EmitRing / Burst で出した波
	};

	WaterSurface();
	~WaterSurface();

	void Initialize(DirectXCore* dxCore, Object3DManager* object3DManager, const std::string& floorTexturePath);

	/// <summary>波の時間を進める（待機中の波の倍率の補間、連続発射もここで進む）</summary>
	void Update(float deltaTime);

	//==============================
	// 波の制御（演出から呼ぶ）
	//==============================
	/// <summary>
	/// ランダムに出る波（待機中の波）の振幅倍率を変える。1=既定、0=無し。
	/// blendSeconds > 0 なら現在値から滑らかに補間する。
	/// </summary>
	void SetAmbientRingScale(float scale, float blendSeconds = 0.0f);
	float GetAmbientRingScale() const { return params_.ambientRingScale; }

	/// <summary>
	/// 大きな波を1発出す。中心から外へ広がり、maxRadius に届くと消える。
	/// 速度と距離減衰は待機中の波と共通（Params::ringSpeed / ringFalloff）。
	/// </summary>
	/// <param name="amplitude">高さ [m] 相当（待機中の波の ringAmplitude と同じ単位）</param>
	/// <param name="wavelength">波長 [m]</param>
	/// <param name="packetLength">束の長さ（波長の数）</param>
	/// <param name="maxRadius">届く距離 [m]</param>
	void EmitRing(const Vector3& center, float amplitude, float wavelength,
		float packetLength = 3.0f, float maxRadius = 35.0f);

	/// <summary>
	/// 大きな波を連続で出し始める（開始時に1発、その後 interval 秒ごと）。
	/// 間隔 ±20%・振幅 0.7〜1.0 倍・波長 0.8〜1.0 倍でばらつかせる。
	/// </summary>
	void StartRingBurst(const Vector3& center, float interval, float amplitude, float wavelength);
	/// <summary>新しく出すのをやめる。出た波は外へ広がって自然に消える</summary>
	void StopRingBurst() { bursting_ = false; }
	bool IsRingBursting() const { return bursting_; }

	/// <summary>EmitRing で出した波のうち、まだ水面に残っている数</summary>
	int GetActiveEmittedRingCount() const;

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

	// 0..1 の乱数（演出用。再現性は不要なので簡易な LCG）
	float NextRandom01();

	Params params_;
	Vector3 center_{};
	float size_ = 400.0f;
	bool followCamera_ = true;

	// 待機中の波の倍率の補間
	float ambientFrom_ = 1.0f;
	float ambientTo_ = 1.0f;
	float ambientBlendElapsed_ = 0.0f;
	float ambientBlendDuration_ = 0.0f;

	// EmitRing のリングバッファ（次に書く枠＝一番古い枠）
	int nextRingSlot_ = 0;

	// 連続発射
	bool bursting_ = false;
	Vector3 burstCenter_{};
	float burstInterval_ = 1.0f;
	float burstAmplitude_ = 0.1f;
	float burstWavelength_ = 2.5f;
	float burstTimer_ = 0.0f;   // 次の発射までの残り秒
	uint32_t randomState_ = 0x2545F491u;

	// ImGui の確認用
	float debugEmitAmplitude_ = 0.12f;
	float debugEmitWavelength_ = 2.5f;
	float debugBurstInterval_ = 1.2f;
	float debugAmbientTarget_ = 1.0f;
	float debugAmbientBlend_ = 1.0f;
};
