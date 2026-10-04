#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <string>
#include "Vector2.h"
#include "Vector3.h"

class DirectXCore;

/// <summary>
/// 遠景の雲（ノイズ平面加工の亜種。9_CloudRendering.md §3.6 案A）のパラメータと GPU リソース。
///
/// 描画パスは持たない。Skybox と WaterSurface が PS の中で CloudSky.hlsli を評価し、
/// このクラスの CB（b7）とノイズ（t5）を挿して使う。CB は1本を共有するので、空と水面の映り込みで値がずれない。
///
/// パラメータはゼノブレイド2式（補正値＝coverage / 乗算値＝sharpness / 風）の名前で持ち、
/// フェーズ3の近景レイマーチでも同じ値を使い回せるようにしてある。
/// </summary>
class CloudLayer {
public:
	struct Params {
		bool    enabled = true;
		float   height = 300.0f;                          // 雲の平面の高さ [m]。低いほど真上が大きく、水平線へ強く縮む

		// 形：大きい形に小さい形を混ぜる。逆向きに流すので輪郭が変わり続ける
		float   shapeScale = 2600.0f;                     // 大きい形のノイズ1枚が覆う幅 [m]
		Vector2 shapeWind{ 18.0f, 6.0f };                 // [m/s]（XZ）
		float   detailScale = 600.0f;                     // 小さい形 [m]
		Vector2 detailWind{ -10.0f, 14.0f };
		float   detailWeight = 0.3f;                      // 小さい形を混ぜる割合

		// マスク：補正値を場所ごとにずらす。大きな塊が集まる所と、小さなちぎれ雲だけの所ができる（雲の大きさの差）
		float   maskScale = 6000.0f;
		Vector2 maskWind{ 3.0f, -2.0f };
		float   maskStrength = 0.8f;

		// ディゾルブ：補正値を場所ごとに時間で揺らす。雲が場所ごとにバラバラに湧いたり消えたりする
		float   evolveScale = 2200.0f;
		Vector2 evolveWind{ -12.0f, -20.0f };
		float   evolveAmount = 0.22f;

		// ゼノブレ式：透明度 = saturate((ノイズ − 補正値) × 乗算値)。ノイズは 0..1 に一様分布なので、補正値 t でおおよそ (1-t) が雲になる
		float   coverage = 0.6f;                          // 補正値（全体の中心）
		float   sharpness = 3.5f;                         // 乗算値（縁のくっきり度）
		// 全体のしきい値の揺れ。周期が割り切れない2本のサインを足して、全体が同期して「呼吸」しないようにする
		float   swingAmplitude = 0.03f;
		float   swingPeriodA = 23.0f;                     // [s]
		float   swingPeriodB = 37.0f;                     // [s]
		float   opacity = 1.0f;

		// 距離：遠くは空気遠近で空の色に寄せて残し、水平線ぎりぎりだけ消す
		float   hazeStart = 1500.0f;                      // [m]
		float   hazeEnd = 12000.0f;                       // [m]
		float   hazeMax = 0.8f;
		float   fadeStart = 8000.0f;                      // [m]
		float   fadeEnd = 25000.0f;                       // [m]

		// 色・陰影
		float   lightStep = 40.0f;                        // 太陽側へずらして形を見る距離 [m]
		float   lightContrast = 4.0f;                     // 太陽側が濃いときの暗くなりやすさ
		Vector3 litColor{ 1.0f, 1.0f, 1.0f };
		Vector3 shadowColor{ 0.62f, 0.69f, 0.80f };       // 雲の底。空の青を少し拾った灰色
		float   shadowDepth = 0.35f;
		Vector3 edgeColor{ 0.06f, 0.06f, 0.05f };
		float   sunPower = 8.0f;
		float   sunGlow = 0.35f;
	};

	CloudLayer();
	~CloudLayer();

	void Initialize(DirectXCore* dxCore, const std::string& noiseTexturePath);

	/// <summary>スクロールとしきい値の揺れを進め、CB を書き換える。空側のレイの原点（描画カメラの位置）もここで渡す</summary>
	void Update(float deltaTime, const Vector3& eyePosition);

	/// <summary>太陽の向き（光の進む向きではなく、太陽へ向かう向き）。陰影の向きと太陽の方向の明るさに使う</summary>
	void SetSunDirection(const Vector3& towardSun);

	Params& GetParams() { return params_; }

	D3D12_GPU_VIRTUAL_ADDRESS GetConstantBufferAddress() const;
	D3D12_GPU_DESCRIPTOR_HANDLE GetNoiseSrvHandle() const;

	void OnImGui();

	//==============================
	// 雲を描かないシーン用（Skybox / WaterSurface の既定）
	//==============================
	/// <summary>enabled=0 の CB を作る。雲を使わない PSO でもルートパラメータを空けないために挿す</summary>
	static Microsoft::WRL::ComPtr<ID3D12Resource> CreateDisabledConstantBuffer(DirectXCore* dxCore);
	/// <summary>t5 を埋めるダミーテクスチャ（enabled=0 なのでシェーダは読まない）</summary>
	static const char* GetFallbackTexturePath();

private:
	/// <summary>CloudSky.hlsli の CloudSkyParams と 1:1（16 バイト単位）</summary>
	struct ConstantsForGPU {
		Vector3 eyePosition;
		int     enabled;
		Vector3 sunDirection;
		float   height;
		Vector2 shapeOffset;
		Vector2 detailOffset;
		Vector2 maskOffset;
		Vector2 evolveOffset;
		float   shapeTiling;
		float   detailTiling;
		float   maskTiling;
		float   evolveTiling;
		float   detailWeight;
		float   detailContrast;
		float   maskStrength;
		float   evolveAmount;
		float   threshold;
		float   sharpness;
		float   opacity;
		float   fadeStart;
		float   fadeEnd;
		float   hazeStart;
		float   hazeEnd;
		float   hazeMax;
		Vector2 lightOffset;
		float   lightContrast;
		float   sunPower;
		Vector3 litColor;
		float   shadowDepth;
		Vector3 shadowColor;
		float   sunGlow;
		Vector3 edgeColor;
		float   padding;
	};
	static_assert(sizeof(ConstantsForGPU) == 192, "CloudSky.hlsli の CloudSkyParams とサイズを合わせる");

	DirectXCore* dxCore_ = nullptr;
	std::string noiseTexturePath_;

	Microsoft::WRL::ComPtr<ID3D12Resource> constantsResource_;
	ConstantsForGPU* constantsData_ = nullptr;

	Params params_;
	Vector3 sunDirection_{ 0.0f, 1.0f, 0.0f };
	// スクロール量（UV）。0..1 に折り返して持つ（時間を積むと float の精度が落ちるため）
	Vector2 shapeOffset_{};
	Vector2 detailOffset_{};
	Vector2 maskOffset_{};
	Vector2 evolveOffset_{};
	float time_ = 0.0f;
};
