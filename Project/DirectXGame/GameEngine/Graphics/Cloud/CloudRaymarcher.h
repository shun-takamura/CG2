#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <memory>
#include <string>
#include <vector>
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"

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
		float   mokoLodBias = 0.0f;                       // もこもこを何段粗いミップから読むか（0 だと谷で細かい段が増幅されて縞に見えた）
		float   grazingMin = 0.05f;                       // 見下ろす角度でミップを粗くする時の |dir.y| の下限（浅い角度のモアレ対策）
		float   detailFadeStart = 600.0f;                 // この距離 [m] から遠くの細部（もこもこ）を薄め、形を粗くする（浅い角度の遠くで縞に見えた）
		float   detailFadeEnd = 2500.0f;                  // この距離 [m] で細部なし
		float   detailFadeLod = 2.0f;                     // 遠くで形を何段粗いミップから読むか
		float   grazingPower = 1.0f;
		float   occlusionSoft = 25.0f;                    // 雲海の上面の影のやわらかさ [m]（上面の高さの差がこれだけで真っ暗）
		bool    anisoEnabled = true;                      // 雲海の上面を異方性フィルタで読む（浅い角度の縞＝奥行き方向の間引きの対策）
		float   slopeClampNoise = 0.5f;                   // 谷での傾きの増幅をこのノイズ値で頭打ちに（小さいほど増幅が強い。0.3 で無効相当）
		float   shapeLodBias = 3.0f;                      // 形（大きいノイズ）を全距離で何段粗いミップから読むか（細かい段が ÷ノイズ で尖った凹凸になり、指紋の縞になった）
		float   mokoInHeight = 0.0f;                      // もこもこを形に入れる割合（0 = 陰影だけ。形に入れると同じく縞の凹凸になる）
		float   shapeGradLodBias = 0.0f;                  // 形の傾きを shapeLodBias よりさらに何段粗く読むか                      // 浅い角度のぼかしの強さ（1.5 にすると縞は減るが雲の品質が下がった。縞は TAA／複数レイで消す方針）
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

		// ハイトマップ誘導のボリューム雲（上から見下ろす雲海。2026-10-22、CPU 版 preview_cloud_raymarch.py の HV_DEFAULTS と同じ値）
		bool    hvEnabled = false;                        // 2026-10-22 不採用（雲海の上で 2.69ms、従来 0.31ms）。true で比較用に描ける
		float   hvShapeScale = 1500.0f;                   // 上面の形 1 枚の大きさ [m]
		float   hvDetailScale = 400.0f;                   // 細部 [m]
		float   hvDetailWeight = 0.35f;
		float   hvRelief = 60.0f;                         // 上面の起伏の高さ [m]
		float   hvFloorSpread = 1.0f;                     // 補正値のこれだけ下から起伏させる（小さいと平らな床が出て、縁のくっきりした板に見えた）
		float   hvBumpPower = 1.0f;
		float   hvFluff = 12.0f;                          // 綿の層の厚さ [m]
		float   hvMarchDepth = 2.0f;                      // 綿の層を何層ぶん歩くか
		float   hvMaxMarch = 400.0f;                      // 1 回に歩く長さの上限 [m]
		int     hvSteps = 24;                             // 綿の層の歩数（CPU 版は 48。重ければ下げる）
		int     hvSegments = 2;                           // 空中に出たレイが次の上面まで歩き直す回数（CPU 版は 3）
		float   hvExtinction = 0.1f;                      // 消散係数（弱いと斜めに長く歩いた所だけ中の暗い色を拾い、板に見えた）
		float   hvErosion = 0.5f;                         // 3D ノイズで濃さを崩す量
		float   hvErosionScale = 100.0f;                  // 3D ノイズ 1 周期 [m]
		float   hvWarp = 20.0f;                           // ハイトマップを読む位置のずらし [m]（側面の縦の筋を崩す）
		float   hvWarpScale = 50.0f;
		float   hvWarpRamp = 20.0f;                       // 上面からこの深さ [m] でずらしを最大に（上面の上に切れ端が浮かないように）
		Vector4 hvLightSteps{ 6.0f, 18.0f, 40.0f, 90.0f }; // 太陽側の 4 点 [m]
		float   hvLightSigma = 0.4f;                      // 太陽側の雲の影の濃さ
		float   hvPowderMix = 0.6f;                       // 縁の暗さ（Powder）
		float   hvAoBias = 3.0f;                          // 谷の暗さで比べる「まわり」のミップ段数
		float   hvAoRange = 60.0f;                        // [m]
		float   hvAoDepth = 120.0f;                       // [m]
		Vector3 hvLitColor{ 1.0f, 0.97f, 0.92f };
		Vector3 hvShadowColor{ 0.28f, 0.40f, 0.70f };
		Vector3 hvSkyAmbient{ 0.50f, 0.66f, 0.95f };
		float   hvSunScale = 0.7f;                        // 明暗の差を付けたい時はこれと hvLightSigma を上げ、hvAmbientScale を下げる
		float   hvAmbientScale = 0.55f;

		float   resolutionScale = 0.5f;                   // RT の大きさ（画面比）。変えたら作り直す

		// 雲海のボリューム（2026-10-23、上から見下ろす雲海。Nubis 式の 3D ノイズ＋光の透過＋時間方向の積み重ね）。
		// 形の大枠は上の 2D の形（ゼノブレ式の高さ）で決め、その上面の下 volFluff [m] を 3D ノイズで削ってこぶにする
		bool    volumeSea = true;                         // false で従来の面（ゼノブレ式の当たり）に戻す
		bool    temporal = true;                          // 時間方向に積み重ねる（歩き始めを毎フレーム揺らして平均＝歩幅の縞を消す）
		float   temporalBlend = 0.06f;                    // 今のフレームの重み（小さいほどなめらか、動くと残像が出やすい）
		float   volShapeScale = 700.0f;                   // 3D の形（Perlin-Worley）1 周期 [m]
		float   volDetailScale = 140.0f;                  // 3D の細部（Worley）1 周期 [m]
		float   volFluff = 160.0f;                        // なめらかな上面の上下この幅 [m] をこぶの層にする（大きいほどこぶが高い）
		float   volErosion = 0.6f;                        // 細部で縁を削る量（カリフラワー状の縁）
		float   volExtinction = 0.14f;                    // 消散係数 [1/m]（濃さ 1 の所）。大きいほど輪郭がはっきりする
		float   volDensityGain = 2.5f;                    // こぶの濃さの立ち上がり（形のノイズ − 高さのしきい値 に掛ける）
		float   volVerticalStretch = 1.0f;                // 3D の形を縦に引き伸ばす倍率（縦の変化が細かいと雲の面が棚状の段になり横縞に見えた）
		int     volSteps = 40;                            // 歩数（64 で 1.5ms、32 で 0.65ms。見た目は 40 前後から変わらない）
		float   volStepMin = 4.0f;                        // 1 歩の最小 [m]
		float   volStepGrowth = 0.006f;                   // 1 歩 = max(最小, 距離 × これ)
		float   volLightSigma = 2.5f;                     // 太陽側の遮りの強さ（消散係数への倍率）
		float   volMacroShadow = 2.5f;                    // 大きな塊の影（太陽側の上面の高さから）
		float   volMultiScatter = 0.3f;                   // 多重散乱の 2 段目以降の強さ（小さいほど影が締まる）
		float   volSunScale = 1.0f;
		float   volAmbientScale = 1.0f;
		float   volPhaseG = 0.6f;                         // 前方散乱の強さ（半分は等方。太陽を背にしても頭が白い）
		float   volPowder = 0.8f;                         // 縁の暗さ（Powder）
		Vector3 volSkyAmbient{ 0.14f, 0.19f, 0.30f };     // 上からの空の光（線形）。谷の青みはここと Shadow Color で決まる
	};

	CloudRaymarcher();
	~CloudRaymarcher();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t screenWidth, uint32_t screenHeight);

	/// <summary>風のスクロールを進める。毎フレーム</summary>
	void Update(float deltaTime);
	/// <summary>
	/// Params の風に足す風 [m/s]（XZ。保存しない）。大きい形・もこもこ・マスクを同じだけ流す。
	/// 使う側が毎フレーム決める（例：自機の進む向きと逆へ流して飛んでいる感じを出す）
	/// </summary>
	void SetExtraWind(const Vector2& wind) { extraWind_ = wind; }

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
	/// <summary>確認用の表示の切り替え（雲海の面の陰影を項目ごとに映す）。保存しない</summary>
	void OnImGuiDebugView();

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
		float   shaftWallBlend; float shaftDarkness;  float undersideBright; float mokoLodBias;
		float   capTop;         float capBottom;      float erosionSharp;   float lightExtinction;
		float   grazingMin;     Vector2 hvWind;       float grazingPower;
		int     hvEnabled;      float hvRelief;       float hvFloorLow;     float hvBumpPower;
		float   hvFluff;        float hvMarchDepth;   float hvMaxMarch;     int   hvSteps;
		float   hvExtinction;   float hvErosion;      float hvErosionTiling; float hvWarp;
		float   hvWarpTiling;   float hvWarpRamp;     int   hvSegments;     float hvLightSigma;
		float   hvPowderMix;    float hvAoBias;       float hvAoRange;      float hvAoDepth;
		Vector3 hvSkyAmbient;   float hvAmbientScale;
		Vector3 hvLitColor;     float hvSunScale;
		Vector3 hvShadowColor;  float hvShapeTiling;
		float   hvDetailTiling; float hvDetailWeight; float hvDetailContrast; float occlusionSoft;
		float   hvLightSteps[4];
		float   detailFadeStart; float detailFadeEnd;  float detailFadeLod;  float pixelAngle;
		int     anisoEnabled;   float slopeClampNoise; float shapeGradLodBias; int debugView;
		float   shapeLodBias;   float mokoInHeight;   float padding8;       float padding9;
		Matrix4x4 prevViewProj;
		int     frameIndex;     int   historyValid;   int   volumeSea;      float volShapeTiling;
		float   volDetailTiling; float volFluff;      float volErosion;     float volExtinction;
		int     volSteps;       float volStepMin;     float volStepGrowth;  float volLightSigma;
		float   volSunScale;    float volAmbientScale; float volPhaseG;     float volPowder;
		Vector3 volSkyAmbient;  float volMacroShadow;
		Vector3 volWind;        float temporalBlend;
		float   volDensityGain; float volMultiScatter; float volVerticalStretch; float padding10;
		float   shaftPoints[kShaftPointMax][4];
	};
	static_assert(sizeof(ConstantsForGPU) == 1088, "CloudRaymarch.hlsli の CloudRaymarchParams とサイズを合わせる");

	void CreateRootSignature();
	Microsoft::WRL::ComPtr<ID3D12PipelineState> CreatePipelineState(const wchar_t* psPath, DXGI_FORMAT rtvFormat, bool premultipliedBlend,
		DXGI_FORMAT rtvFormat2 = DXGI_FORMAT_UNKNOWN);
	void CreateTarget();
	/// <summary>3D テクスチャ（DDS、ボリューム）を読んで SRV を作る。TextureManager は 2D 専用なのでここで持つ</summary>
	void LoadVolumeTexture(const char* path, Microsoft::WRL::ComPtr<ID3D12Resource>& resource, uint32_t& srvIndex);
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
	Microsoft::WRL::ComPtr<ID3D12PipelineState> temporalPipeline_;
	std::unique_ptr<RenderTexture> target_;
	std::unique_ptr<RenderTexture> depthTarget_;          // 雲の代表の距離 [m]（時間方向の積み重ねで前のフレームの位置を求める）
	std::unique_ptr<RenderTexture> history_[2];           // 積み重ねた結果（交互に読み書き）
	int historyIndex_ = 0;
	bool historyValid_ = false;
	uint32_t frameIndex_ = 0;
	Matrix4x4 prevViewProj_{};
	Vector3 prevEye_{};
	float targetScale_ = 0.0f;

	Microsoft::WRL::ComPtr<ID3D12Resource> shape3D_;
	Microsoft::WRL::ComPtr<ID3D12Resource> detail3D_;
	uint32_t shape3DSrv_ = 0;
	uint32_t detail3DSrv_ = 0;

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
	int debugView_ = 0;                                   // 確認用の表示（0 = 通常）。JSON には保存しない

	Vector2 shapeOffset_{};
	Vector2 mokoOffset_{};
	Vector2 maskOffset_{};
	Vector2 extraWind_{};   // SetExtraWind（保存しない）
	Vector3 volOffset_{};   // 3D ノイズを読む位置のずれ [m]（2D の形と同じ風で流す。y はこぶがゆっくり湧く動き）
	float time_ = 0.0f;
};
