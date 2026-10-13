#pragma once
#include "BaseTransition.h"
#include "FeatherRenderer.h"
#include "ScreenWaterSurface.h"
#include "Matrix4x4.h"
#include "Vector2.h"
#include "Vector3.h"
#include <wrl.h>
#include <d3d12.h>
#include <random>
#include <vector>

/// <summary>
/// 羽根の着水（死亡 → ゲームオーバー）。15_SceneTransition.md T4（2026-10-11 作り直し版）。
///   覆う：① 小さい羽根が上から下へ降り、瞼が閉じるようにステージが上下から閉じる
///        ② 雨の中に大きい一枚（曲げた板のモデル）が現れ、他より速く左右に振れながら落ちる。
///           カメラが追って下がるので小さい羽根は上へ流れて消える
///        → 大きい一枚が振れの端で一瞬止まりかけた所でロード（同期ロードで画面が止まっても目立たない）
///   開く：③ カメラが斜めに見下ろす角度へ傾き、暗い水面（3D の平面）が画面全体に広がる。大きい一枚が着地して波紋
///        ④ 大きい一枚はディゾルブで消える
///        ⑤ 画面全体に広がった波紋の揺らぎの中から、ゲームオーバー画面が浮かび上がる
/// 描く順：瞼で閉じたシーン・水面（FeatherWater.PS）→ その上に羽根。
/// </summary>
class FeatherLandTransition : public BaseTransition {
public:
	struct Settings {
		// ===== 覆う =====
		float rainTime = 1.2f;           // 雨を降らせる長さ
		float rainFadeStart = 1.5f;      // 大きい一枚だけにするため、残っている雨を薄くし始める時刻
		float rainFadeTime = 0.6f;       // 雨が消え切るまで（minCoverTime より前に消え切るように）
		float heroSpawnTime = 0.3f;      // 大きい一枚が現れる時刻
		float followStartTime = 0.9f;    // カメラが大きい一枚を追い始める時刻
		float minCoverTime = 2.2f;       // これを過ぎた最初の振れの端で覆い終わる（そこでロード）
		float eyelidStart = 0.2f;        // 瞼が閉じ始める時刻
		float eyelidDuration = 1.4f;     // 瞼が閉じ切るまでの長さ

		// 瞼の形（Eyelid.hlsli）
		float eyelidCurvature = 0.6f;    // 縁の弧の強さ
		float eyelidSoftness = 0.04f;    // 縁のぼかし
		float eyelidCloseY = 0.56f;      // 閉じる点の高さ（画面 UV）

		// 雨（小さい羽根）
		float spawnRate = 60.0f;         // 1 秒に降らせる枚数
		float fovY = 0.9f;               // 仮想カメラの縦の画角（ラジアン）
		float nearDepth = 3.0f;          // 羽根を置く奥行き（カメラからの距離）の手前
		float farDepth = 9.0f;           // 同・奥
		float spawnSpread = 0.6f;        // 上の中央から左右へどこまで散らすか（画面の半幅に対する割合）
		float sizeMin = 0.35f;           // 羽根の長さ
		float sizeMax = 0.6f;
		float fallSpeedMin = 0.5f;       // 落ちる速さ（毎秒）
		float fallSpeedMax = 0.9f;
		float swayAmplitude = 0.35f;     // 左右の揺れ幅
		float swayFrequency = 1.6f;      // 揺れの速さ（ラジアン / 秒）
		float swayTilt = 0.6f;           // 揺れに合わせて傾く量（ラジアン）
		float spinSpeed = 1.2f;          // くるくる回る速さの上限（ラジアン / 秒）
		float bendMin = 0.15f;           // 反り
		float bendMax = 0.35f;
		float flutter = 0.08f;           // 反りが揺れる量（しなり）

		// 大きい一枚（落ち葉のように左右へ振れながら滑り落ちる）
		float heroSize = 0.9f;           // 長さ
		float heroDepth = 4.0f;          // 奥行き（カメラからの距離）
		float heroFallSpeed = 1.6f;      // 振れの中央での落ちる速さ（端では遅くなる）
		float heroSwingAmplitude = 0.55f;// 左右の振れ幅
		float heroSwingFrequency = 2.2f; // 振れの速さ（ラジアン / 秒）
		float heroBank = 0.7f;           // 振れの向きへ傾く量（ラジアン）
		float heroFacePitch = 0.35f;     // 面をこちらへ見せる傾き（ラジアン）
		float heroBend = 0.3f;           // 反り
		float followStiffness = 4.0f;    // カメラが追いつく速さ（大きいほどぴったり）
		float followSide = 0.3f;         // 左右の振れにカメラがどれだけ付いていくか

		// ===== 開く =====
		float waterDrop = 1.8f;          // 開き始めの大きい一枚から水面までの高さ
		float tiltTime = 1.0f;           // カメラが斜めに見下ろす角度へ傾くまで
		float viewPitch = 0.61f;         // 見下ろす角度（ラジアン、約35°）
		float viewDistance = 3.2f;       // 着地点からカメラまでの距離
		float settleHeight = 0.8f;       // 水面からこの高さで大きい一枚が水平に寝始める
		float dissolveDelay = 0.3f;      // 着地からディゾルブが始まるまで
		float dissolveTime = 0.9f;       // ディゾルブで消え切るまで
		float emergeDelay = 0.4f;        // 着地からゲームオーバー画面が浮かび始めるまで
		float emergeTime = 2.0f;         // 浮かび上がり切るまで（じわっと）
		float impulseRadius = 0.3f;      // 着地のへこみの半径（ワールドの長さ）
		float impulseStrength = 0.16f;   // 着地のへこみの深さ
		int bobPulseCount = 4;           // 着地後、浮いた羽根の上下で着地点から出す小さな波の数
		float bobPulseInterval = 0.35f;  // その間隔
		float bobPulseStrength = 0.06f;  // その深さ
		float ambientRate = 8.0f;        // 浮かび上がる間、水面にぽつぽつ落とす小さな波（1 秒あたり）
		float ambientRadius = 0.15f;     // その半径
		float ambientStrength = 0.03f;   // その深さ
		float ambientSpread = 3.5f;      // 着地点からどこまでの範囲に落とすか（ワールドの長さ）

		// 水面の見た目（FeatherWater.PS）
		Vector3 waterColor = { 0.015f, 0.02f, 0.035f }; // 水の色
		Vector3 skyColor = { 0.10f, 0.13f, 0.20f };     // 水面に映る空の色（斜めほど強く映る）
		float specular = 0.8f;           // 照り返しの強さ
		float fogDensity = 0.05f;        // 遠くの水面を暗闇へ溶かす
		float waveNormalScale = 2.0f;    // 波の傾きを法線に足す倍率
		float emergeRadialBias = 0.5f;   // 着地点に近いほど早く現れる度合い（0 = 全体が同時）
		float emergeNoise = 0.3f;        // 現れ方のむら
		float emergeDistortion = 2.5f;   // 出始めの歪みの強さ（屈折の何倍か。現れ切ると 1 倍）

		// JSON との行き来（TransitionTuning.h）。項目を足したらここにも足す
		template <class V>
		void Visit(V& v) {
			v("rainTime", rainTime);
			v("rainFadeStart", rainFadeStart);
			v("rainFadeTime", rainFadeTime);
			v("heroSpawnTime", heroSpawnTime);
			v("followStartTime", followStartTime);
			v("minCoverTime", minCoverTime);
			v("eyelidStart", eyelidStart);
			v("eyelidDuration", eyelidDuration);
			v("eyelidCurvature", eyelidCurvature);
			v("eyelidSoftness", eyelidSoftness);
			v("eyelidCloseY", eyelidCloseY);
			v("spawnRate", spawnRate);
			v("fovY", fovY);
			v("nearDepth", nearDepth);
			v("farDepth", farDepth);
			v("spawnSpread", spawnSpread);
			v("sizeMin", sizeMin);
			v("sizeMax", sizeMax);
			v("fallSpeedMin", fallSpeedMin);
			v("fallSpeedMax", fallSpeedMax);
			v("swayAmplitude", swayAmplitude);
			v("swayFrequency", swayFrequency);
			v("swayTilt", swayTilt);
			v("spinSpeed", spinSpeed);
			v("bendMin", bendMin);
			v("bendMax", bendMax);
			v("flutter", flutter);
			v("heroSize", heroSize);
			v("heroDepth", heroDepth);
			v("heroFallSpeed", heroFallSpeed);
			v("heroSwingAmplitude", heroSwingAmplitude);
			v("heroSwingFrequency", heroSwingFrequency);
			v("heroBank", heroBank);
			v("heroFacePitch", heroFacePitch);
			v("heroBend", heroBend);
			v("followStiffness", followStiffness);
			v("followSide", followSide);
			v("waterDrop", waterDrop);
			v("tiltTime", tiltTime);
			v("viewPitch", viewPitch);
			v("viewDistance", viewDistance);
			v("settleHeight", settleHeight);
			v("dissolveDelay", dissolveDelay);
			v("dissolveTime", dissolveTime);
			v("emergeDelay", emergeDelay);
			v("emergeTime", emergeTime);
			v("impulseRadius", impulseRadius);
			v("impulseStrength", impulseStrength);
			v("bobPulseCount", bobPulseCount);
			v("bobPulseInterval", bobPulseInterval);
			v("bobPulseStrength", bobPulseStrength);
			v("ambientRate", ambientRate);
			v("ambientRadius", ambientRadius);
			v("ambientStrength", ambientStrength);
			v("ambientSpread", ambientSpread);
			v("waterColor", waterColor);
			v("skyColor", skyColor);
			v("specular", specular);
			v("fogDensity", fogDensity);
			v("waveNormalScale", waveNormalScale);
			v("emergeRadialBias", emergeRadialBias);
			v("emergeNoise", emergeNoise);
			v("emergeDistortion", emergeDistortion);
		}
	};

	FeatherLandTransition() = default;
	~FeatherLandTransition() override = default;

	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth, float screenHeight) override;
	void Finalize() override;
	void Draw() override;

	TransitionType GetType() const override { return TransitionType::FeatherLand; }
	std::string GetName() const override { return "FeatherLand"; }

	Settings& GetSettings() { return settings_; }

	/// <summary>ImGui で調整</summary>
	void OnImGui();

	// 調整値（Resources/Json/Tuning/Transition.json の GetName() の欄）
	void LoadTuning(const JsonValue& json) override;
	void SaveTuning(JsonValue& json) const override;

protected:
	void OnStartCover() override;
	bool UpdateCover(float dt) override;
	bool UpdateReveal(float dt) override;
	void SetFullyCovered() override;
	void UpdateVisual() override;

private:
	// 雨の羽根1枚の動き（見た目は FeatherRenderer::Feather に書き出す）
	struct FallingFeather {
		Vector3 origin;          // 揺れの中心（x）と今の高さ・奥行き
		float fallSpeed;
		float swayPhase;
		float swaySpeed;
		float swayAmplitude;
		Vector3 baseRotation;
		float spinSpeed;
		float flutterPhase;
		float baseBend;          // しなりの中心の反り
		bool seen;               // 一度でも画面に入ったか（入ってから外へ出たら捨てる）
		FeatherRenderer::Feather look;
	};

	// 大きい一枚
	struct HeroFeather {
		bool active = false;
		Vector3 position{};
		float baseX = 0.0f;      // 振れの中心
		float swingPhase = 0.0f;
		float flutterPhase = 0.0f;
		bool landed = false;
		float landedTime = 0.0f; // 開く側の何秒目に着地したか
		FeatherRenderer::Feather look;
	};

	// FeatherWater.PS.hlsl の FeatherWaterParams と 1:1（208 バイト）
	struct WaterParamsForGPU {
		ScreenWaterSurface::ConstantsForGPU water;
		Matrix4x4 inverseViewProjection;
		Vector3 cameraPosition;
		float waterY;
		float eyelidClosure;
		float eyelidCurvature;
		float eyelidSoftness;
		float eyelidCloseY;
		float waterVisibility;
		float emerge;
		float time;
		float fogDensity;
		Vector3 waterColor;
		float specular;
		Vector3 skyColor;
		float waveNormalScale;
		Vector2 emergeCenter;
		float emergeRadialBias;
		float emergeNoise;
		Vector3 lightDirection;
		float emergeDistortion;
	};

	void SpawnFeather();
	void SpawnHero();
	void UpdateFeathers(float dt);
	void UpdateHero(float dt);
	void UpdateCamera(float dt);
	// 着地後に水面を揺らし続ける（浮いた羽根の上下で出る波＋浮かび上がる間ぽつぽつ落ちる波）
	void UpdateWaterDisturbance(float dt);
	// 開く側の経過秒（覆う・隠したままの間は 0）
	float RevealSeconds() const;
	float EyelidClosure() const;
	// 雨の濃さ 1→0（rainFadeStart から rainFadeTime かけて消える）
	float RainAlpha() const;
	float WaterVisibility() const;
	float EmergeProgress() const;
	float DissolveProgress() const;
	// 奥行き z の所で画面の上端になる高さ（カメラからの距離 z）
	float VisibleHalfHeight(float depth) const;
	// ワールドの点 → 仮想カメラの画面 UV
	Vector2 ProjectToScreen(const Vector3& world) const;
	// 羽根が仮想カメラの画面に映っているか（羽根の大きさの分だけ画面の外まで見る）
	bool IsOnScreen(const FeatherRenderer::Feather& feather) const;
	WaterParamsForGPU WriteWaterParams();

	FeatherRenderer renderer_;
	ScreenWaterSurface water_;
	Settings settings_;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> waterRootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> waterPipelineState_;
	Microsoft::WRL::ComPtr<ID3D12Resource> waterParamsResource_;
	WaterParamsForGPU* waterParamsData_ = nullptr;

	std::vector<FallingFeather> falling_;
	HeroFeather hero_;
	std::vector<FeatherRenderer::Feather> drawList_;
	std::mt19937 random_{ 20261011u };
	float spawnAccumulator_ = 0.0f;
	float time_ = 0.0f;

	// カメラが追う高さ（なめらかに追いつく）
	Vector3 followEye_{};
	// 振れの端を見つける（cos の符号が変わった所が端）
	float previousSwingCos_ = 1.0f;
	bool atSwingExtreme_ = false;
	// 水面
	float waterY_ = 0.0f;
	bool waterPlaced_ = false;
	Vector3 landingPoint_{};
	// 着地後の小さな波（浮いた羽根の上下・水面にぽつぽつ）
	int bobPulsesLeft_ = 0;
	float nextBobPulseTime_ = 0.0f;
	float ambientAccumulator_ = 0.0f;
};
