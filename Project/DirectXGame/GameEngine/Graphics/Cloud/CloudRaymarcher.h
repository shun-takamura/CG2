#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"

class Camera;
class DirectXCore;
class JsonValue;
class RenderTexture;
class SRVManager;

/// <summary>
/// 雲海のレイマーチ（9_CloudRendering.md フェーズ3）。
///
/// 一帯はゼノブレイド2式（2D ノイズ×高さの勾配、当たったら止める）で埋め、降下の線のまわりを円柱にくり抜く。
/// 円柱の壁もゼノブレ式のまま描け、入口（上面との交点）・出口（下面との交点）の近くだけ B'（手前を積み上げる軽量ボリューム）に切り替える。
/// 形は 1 本の濃さの式（CloudRaymarch.hlsli）を共有するので、描き方の境目で形はずれない。
///
/// 1/4 解像度の RT に描いてからシーンへ重ねる。不透明物の深度で止める。水面には映さない。
/// パラメータ名は遠景の雲（CloudLayer）と揃えてある（補正値＝coverage / 乗算値＝sharpness / 風）。
/// </summary>
class CloudRaymarcher {
public:
	struct Params {
		bool    enabled = true;
		float   opacity = 1.0f;                           // 全体の濃さ。L5 が雲を消すときに 0 へ下げる

		// 雲の層（11_LowAltitudeFlight.md §3.0）
		float   top = 850.0f;                             // 最高点 [m]
		float   bottom = 300.0f;                          // 下面 [m]
		float   seaDepth = 160.0f;                        // 雲海の深さ [m]：ゼノブレ式の「濃さ = (最高点 − y) / 深さ」。波の高さの幅
		float   bottomFade = 15.0f;                       // 下面の丸め [m]（厚いと下のふたの雲を食って薄くなる）
		float   noiseFloor = 0.3f;                        // ノイズを noiseFloor..1 に寄せる。補正値より大きければ帯の底で必ず雲（隙間から地上が見えない）

		// 形：大きい形（cloud_noise.dds の R＝Perlin＋Worley）ともこもこ（cloud_moko_height.dds のドーム）を混ぜる
		float   shapeScale = 1800.0f;                     // 大きい形 1 枚の大きさ [m]
		Vector2 shapeWind{ 6.0f, 2.0f };                  // [m/s]（もこもこも同じ風で流す）
		float   mokoScale = 500.0f;                       // もこもこ 1 枚の大きさ [m]
		float   mokoWeight = 0.55f;                       // もこもこを混ぜる割合
		float   maskScale = 9000.0f;
		Vector2 maskWind{ 1.0f, -1.0f };
		float   maskStrength = 0.4f;
		float   coverage = 0.22f;                         // 補正値
		float   sharpness = 3.0f;                         // 乗算値

		// ゼノブレ式の歩き方
		int     steps = 48;                               // 上からは上面の高さ場トレース、層の中（縦穴）は等比で歩く歩数
		float   traceSlope = 0.35f;                       // 上面の最大傾斜の見積もり。小さいと速いが、こぶを飛び越えて欠ける
		float   traceEpsilon = 0.5f;                      // 上面とこの高さ [m] 以内に来たら当たり
		float   firstStep = 4.0f;                         // 層の中の 1 歩目 [m]。以降は等比で伸びる
		float   maxDistance = 20000.0f;                   // [m]
		float   jitter = 0.0f;                            // 歩き始めの揺らし 0..1（TAA が無い間は 0。揺らすとざらつく）

		// 陰影：上面の高さ場の法線 × 太陽側 1 タップの遮り × 谷の暗さ
		float   normalEps = 12.0f;                        // 法線を取る差分の幅 [m]（遠くは自動で広げる）
		float   wrap = 0.3f;                              // 法線の当たりの回り込み（0 = Lambert）
		float   lightStep = 18.0f;                        // 太陽側の点までの距離 [m]
		float   density = 1.2f;                           // 太陽側の雲で遮る強さ
		float   valley = 0.7f;                            // 谷（こぶの高さの範囲の下の方）を暗くする量
		float   valleyPow = 0.6f;
		float   ambient = 0.15f;                          // 太陽が当たらない所の明るさ
		Vector3 litColor{ 1.0f, 0.99f, 0.97f };
		Vector3 shadowColor{ 0.30f, 0.38f, 0.55f };       // 線形。谷の色（青すぎると全体が水色に見える）
		Vector3 rimColor{ 1.0f, 0.95f, 0.85f };
		float   rimIntensity = 0.15f;
		float   rimG = 0.6f;
		float   sunGlow = 0.3f;
		float   hazeStart = 3000.0f;
		float   hazeEnd = 15000.0f;
		float   hazeMax = 0.5f;
		float   fadeStart = 14000.0f;
		float   fadeEnd = 20000.0f;

		// 降下の縦穴（円柱のくり抜き）
		float   shaftRadius = 60.0f;                      // 穴の一番内側の半径 [m]（壁の雲はここから外へ濃くなる）
		float   shaftWallDepth = 80.0f;                   // 壁の雲海の深さ [m]：壁のこぶの膨らみの幅
		float   capDepth = 60.0f;                         // 天井・床の雲海の深さ [m]
		float   shaftNoiseScale = 0.25f;                  // 穴の中のノイズの大きさ（雲海のノイズに対する倍率。小さいほど細かいこぶ）
		float   shaftWallBlend = 25.0f;                   // 壁の陰影（中心線へ向く法線）に切り替える幅 [m]
		float   shaftDarkness = 0.6f;                     // 縦穴の深い所の暗さ（床で最大）
		float   undersideBright = 0.45f;                  // 雲海の下面の明るさ（地面の照り返しだけ）
		float   capTop = 60.0f;                           // 上面からこの深さ [m] までは穴を空けない（上から穴が見えないように）
		float   capBottom = 10.0f;                        // 下面からこの高さ [m] までは詰まった雲（床）。厚いと抜け切る前の白い時間が長い（カメラは毎秒約 75m 落ちる）
		float   floorDepth = 25.0f;                       // 床のこぶの盛り上がりの幅 [m]。capBottom + これ ≒ 白い区間の厚さ（35m で約 0.5 秒）

		// B'（入口・出口）
		float   volumeZoneInner = 40.0f;                  // 入口・出口からこの距離 [m] 以内は B' を全開（縦穴の中にいる間も全開）
		float   volumeZoneOuter = 120.0f;                 // この距離 [m] でゼノブレ式だけに戻る
		float   exitZoneInner = 0.0f;                     // 出口（下面との交点）は別の範囲。広いと床の手前から白いもやで覆われ、白い時間が延びる
		float   exitZoneOuter = 100.0f;                   // 30m だと床に硬い円の縁の穴が開いて見えた。100m なら床が B' のもやとしてほどけて消える（白い時間は延びない）
		float   volumeShaftBlend = 50.0f;                 // 縦穴の半径の外へこの幅 [m] で B' を弱める
		float   volumeShaftWeight = 0.2f;                 // 縦穴の中の B' の重み（範囲 × この値）。小さいほど筒の見通しが良く、床のもやに入るのも遅い
		int     volumeSteps = 48;
		float   volumeRange = 300.0f;                     // 手前の何 m を積み上げるか（500m にすると縦穴の中から床のふたまで積み上げ、画面が白いもやで覆われて見通しが無くなった）
		float   extinction = 0.015f;                      // 濃さ 1 の所の消散係数 [1/m]（小さいほど雲の中で遠くまで透ける）
		float   lightExtinction = 4.0f;                   // 太陽側の消散の倍率（大きいほど塊の奥が暗く、立体に見える）
		float   erosion = 0.5f;                           // 3D ノイズ（0..1）がこれを超える所だけ塊にする（大きいほど隙間が多い）
		float   erosionSharp = 2.0f;                      // 塊の縁のくっきり度（4 だと縁が硬く、床の近くでまだらなノイズに見えた）
		float   erosionScale = 100.0f;                    // 3D ノイズ 1 周期の大きさ [m]（塊の大きさ）
		Vector3 erosionWind{ 2.0f, 1.0f, -1.5f };         // [m/s]
		float   powder = 6.0f;                            // 縁の暗さ（Powder 効果）
		float   ambientStrength = 0.15f;                  // 雲の中の明るさの下限。高いと雲の中が白一色になり、白いプレイヤーが同化した
		float   volumeLightStep = 25.0f;                  // [m]

		float   resolutionScale = 0.5f;                   // RT の大きさ（画面比）。変えたら作り直す
	};

	CloudRaymarcher();
	~CloudRaymarcher();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t screenWidth, uint32_t screenHeight);

	/// <summary>風のスクロールを進める。毎フレーム</summary>
	void Update(float deltaTime);

	/// <summary>
	/// 雲を描いてシーンに重ねる。不透明物（と水面）の後、半透明の前に呼ぶ。
	/// 終わるとシーン RT と書き込み用の DSV・全画面のビューポートを貼り直して返す。
	/// </summary>
	void Draw(const Camera& camera, RenderTexture* sceneTarget, const std::string& skyCubemapPath);

	/// <summary>降下の線（縦穴の中心）。点は 16 個まで（それより多ければ間引く）。空ならくり抜かない</summary>
	void SetShaftPath(const std::vector<Vector3>& points);

	/// <summary>太陽へ向かう向き</summary>
	void SetSunDirection(const Vector3& towardSun);

	Params& GetParams() { return params_; }
	const Params& GetParams() const { return params_; }

	void LoadFromJson(const JsonValue& json);
	void SaveToJson(JsonValue& json) const;
	void OnImGui();

private:
	static constexpr int kShaftPointMax = 16;

	/// <summary>CloudRaymarch.hlsli の CloudRaymarchParams と 1:1（16 バイト単位）</summary>
	struct ConstantsForGPU {
		Matrix4x4 invViewProj;
		Vector3 eye;            int   enabled;
		Vector3 sunDirection;   float opacity;
		float   top;            float bottom;         float seaDepth;       float bottomFade;
		Vector2 shapeOffset;    Vector2 mokoOffset;
		Vector2 maskOffset;     float shapeTiling;    float mokoTiling;
		float   maskTiling;     float mokoWeight;     float normalEps;      float maskStrength;
		float   coverage;       float sharpness;      float lodScale;       float time;
		int     steps;          float firstStep;      float maxDistance;    float floorDepth;
		float   lightStep;      float density;        float fadeStart;      float fadeEnd;
		float   hazeStart;      float hazeEnd;        float hazeMax;        float rimIntensity;
		Vector3 litColor;       float rimG;
		Vector3 shadowColor;    float ambient;
		Vector3 rimColor;       float sunGlow;
		int     shaftCount;     float shaftRadius;    float shaftWallDepth; float capDepth;
		Vector3 shaftMin;       float shaftNoiseTiling;
		Vector3 shaftMax;       float volumeWeight;
		int     volumeSteps;    float volumeRange;    float extinction;     float erosion;
		float   erosionScale;   float powder;         float ambientStrength; float volumeLightStep;
		Vector3 erosionWind;    float noiseFloor;
		Vector2 invTargetSize;  float jitter;         float wrap;
		float   valley;         float valleyPow;      float traceSlope;     float traceEpsilon;
		float   shaftWallBlend; float shaftDarkness;  float undersideBright; float padding4;
		float   capTop;         float capBottom;      float erosionSharp;   float lightExtinction;
		float   shaftPoints[kShaftPointMax][4];
	};
	static_assert(sizeof(ConstantsForGPU) == 688, "CloudRaymarch.hlsli の CloudRaymarchParams とサイズを合わせる");

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(const wchar_t* psPath, DXGI_FORMAT rtvFormat, bool premultipliedBlend);
	void CreateTarget();
	/// <summary>入口・出口（降下の線と上面・下面の交点）からの距離で B' の重みを決める</summary>
	float ComputeVolumeWeight(const Vector3& eye) const;
	void WriteConstants(const Camera& camera);

	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	uint32_t screenWidth_ = 0;
	uint32_t screenHeight_ = 0;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> marchPipeline_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> compositePipeline_;
	std::unique_ptr<RenderTexture> target_;
	float targetScale_ = 0.0f;

	Microsoft::WRL::ComPtr<ID3D12Resource> constantsResource_;
	ConstantsForGPU* constantsData_ = nullptr;

	Params params_;
	Vector3 sunDirection_{ 0.0f, 1.0f, 0.0f };
	std::vector<Vector3> shaftPoints_;
	Vector3 shaftEntry_{};                                // 降下の線が上面を横切る点
	Vector3 shaftExit_{};                                 // 下面を横切る点
	bool hasEntry_ = false;
	bool hasExit_ = false;
	float lastVolumeWeight_ = 0.0f;                       // ImGui 表示用

	Vector2 shapeOffset_{};
	Vector2 mokoOffset_{};
	Vector2 maskOffset_{};
	float time_ = 0.0f;
};
