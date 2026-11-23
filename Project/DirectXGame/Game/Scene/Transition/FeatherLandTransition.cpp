#include "FeatherLandTransition.h"
#include "TransitionTuning.h"
#include "TransitionManager.h"
#include "SceneManager.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include "PostEffect.h"
#include "TextureManager.h"
#include "MathUtility.h"
#include "Game.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// smoothstep（3t² − 2t³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3t² − 2t³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 波を解く範囲の一辺（着地点のまわりのワールドの長さ）[m]
	constexpr float kRippleSimSize = 14.0f;
	// 画面いっぱいに広がるよう速めにした波の速さ
	constexpr float kRippleWaveSpeed = 2.5f;
	// 弱めにした 1 刻みごとの波の減衰
	constexpr float kRippleDamping = 0.996f;
	// 浮かび上がるゲームオーバー画面の歪み（波の傾き → 画面 UV のずれ）
	constexpr float kRippleRefraction = 0.05f;
	// 定数バッファの大きさを 256 バイト単位にそろえるためのマスク
	constexpr int kConstantBufferAlignMask = 255;
	// 羽根の焼け落ちの縁のオレンジ（PlayerFeatherBurn.json の dissolveEdgeColor とそろえる）
	constexpr Vector3 kBurnEdgeColor = { 1.0f, 0.45f, 0.08f };
	// 羽根の焼け落ちの縁の幅
	constexpr float kBurnEdgeWidth = 0.12f;
	// 各段の長さの下限 [秒]（0 で割らないように）
	constexpr float kMinPhaseSeconds = 0.01f;
	// これより w が小さければ画面に投影せず画面の中心を返す
	constexpr float kMinClipW = 1e-5f;
	// 雨の羽根の揺れの速さのばらつき（倍率）の最小
	constexpr float kSwaySpeedJitterMin = 0.75f;
	// 雨の羽根の揺れの速さのばらつき（倍率）の最大
	constexpr float kSwaySpeedJitterMax = 1.25f;
	// 雨の羽根の揺れ幅のばらつき（倍率）の最小
	constexpr float kSwayAmplitudeJitterMin = 0.6f;
	// 雨の羽根の揺れ幅のばらつき（倍率）の最大
	constexpr float kSwayAmplitudeJitterMax = 1.2f;
	// 雨の羽根の X 軸まわりの傾きのばらつき（±）[rad]
	constexpr float kRainTiltXRange = 0.6f;
	// 雨の羽根の Z 軸まわりの傾きのばらつき（±）[rad]
	constexpr float kRainTiltZRange = 0.8f;
	// 雨の羽根の横の丸みの最小
	constexpr float kRainCupMin = 0.05f;
	// 雨の羽根の横の丸みの最大
	constexpr float kRainCupMax = 0.15f;
	// 一番奥の羽根を暗くする割合
	constexpr float kFarDarkening = 0.25f;
	// 手前と奥の奥行きの差の下限 [m]（0 で割らないように）
	constexpr float kMinDepthRange = 0.01f;
	// 羽根の反りが揺れる速さ [rad/秒]
	constexpr float kFlutterSpeed = 5.0f;
	// 大きい一枚を画面の上端から自分の長さの何倍上に出すか
	constexpr float kHeroSpawnAboveRatio = 0.6f;
	// 大きい一枚の横の丸み
	constexpr float kHeroCup = 0.12f;
	// 大きい一枚のディゾルブの欠け方を決めるノイズのずらし（FeatherRise とそろえる）
	constexpr float kHeroNoiseOffset = 0.37f;
	// 振れの端での落ちる速さ（振れの中央の速さに対する割合）
	constexpr float kHeroFallSpeedMinRatio = 0.3f;
	// 振れの中央へ向かって足す落ちる速さの割合
	constexpr float kHeroFallSpeedSwingRatio = 0.7f;
	// 水平に寝始める高さの下限 [m]（0 で割らないように）
	constexpr float kMinSettleHeight = 0.01f;
	// 着地した大きい一枚が浮く水面からの高さ [m]
	constexpr float kFloatHeight = 0.02f;
	// 水面に浮いた大きい一枚が上下する幅 [m]
	constexpr float kBobAmplitude = 0.01f;
	// 水面に浮いた大きい一枚が上下する速さ [rad/秒]
	constexpr float kBobFrequency = 3.0f;
	// 浮いた羽根の上下で出す小さな波の半径（着地のへこみの半径に対する割合）
	constexpr float kBobPulseRadiusRatio = 0.6f;
	// カメラが大きい一枚を追い始めてから追い切るまで [秒]
	constexpr float kFollowBlendSeconds = 0.5f;
	// 瞼の縁のぼかしの下限
	constexpr float kMinEyelidSoftness = 0.001f;
	// 画面全体を覆う三角形の頂点の数
	constexpr int kFullscreenTriangleVertexCount = 3;

	constexpr const wchar_t* kWaterPixelShaderPath = L"Resources/Shaders/Transition/FeatherWater.PS.hlsl";
	// 浮かび上がりの縁をちぎるノイズ（InkShape.hlsli）。ScreenWaterSurface の t0 / t1 の後ろの t2
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";
	constexpr UINT kRootNoise = ScreenWaterSurface::kRootExtraTextures;
	// kPi と Vector3 の Lerp はエンジンの MathUtility.h のものを使う
	constexpr float kHalfPi = kPi * 0.5f;

	/// <summary>0〜1 に切ってから smoothstep をかける</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 のなめらかな値</returns>
	float SmoothStep01(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * t);
	}
	/// <summary>a から b へ t で線形補間する</summary>
	/// <param name="a">t = 0 の値</param>
	/// <param name="b">t = 1 の値</param>
	/// <param name="t">補間の割合</param>
	/// <returns>補間した値</returns>
	float Lerp(float a, float b, float t) { return a + (b - a) * t; }
}

void FeatherLandTransition::OnInitialize() {

	SRVManager* srvManager = SceneManager::GetInstance()->GetSRVManager();
	water_.Initialize(GetDxCore(), srvManager, GetScreenWidth(), GetScreenHeight());
	// 水面のワールドの長さで解く：着地点のまわり 14 四方。画面いっぱいに広がるよう波は速め・減衰は弱め
	ScreenWaterSurface::Settings ripple = water_.GetSettings();
	ripple.simSize = kRippleSimSize;
	ripple.waveSpeed = kRippleWaveSpeed;
	ripple.damping = kRippleDamping;
	ripple.refraction = kRippleRefraction;   // 浮かび上がるゲームオーバー画面の歪み（波の傾き → 画面 UV のずれ）
	water_.SetSettings(ripple);
	waterRootSignature_ = water_.CreateRootSignature(1);
	waterPipelineState_ = water_.CreatePipelineState(waterRootSignature_.Get(), kWaterPixelShaderPath, false);
	TextureManager::GetInstance()->LoadTexture(kNoisePath);
	waterParamsResource_ = GetDxCore()->CreateBufferResource((sizeof(WaterParamsForGPU) + kConstantBufferAlignMask) & ~kConstantBufferAlignMask);
	waterParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&waterParamsData_));
	*waterParamsData_ = WaterParamsForGPU{};

	renderer_.Initialize(GetDxCore(), srvManager);
	// 縁は死亡演出の羽根の焼け落ちと同じオレンジ（19 番 §1.3。PlayerFeatherBurn.json の dissolveEdgeColor とそろえる）。
	// よみがえり（FeatherRise）は別の renderer_ なので青白のまま
	FeatherRenderer::Settings feather = renderer_.GetSettings();
	feather.edgeColor = kBurnEdgeColor;
	feather.edgeWidth = kBurnEdgeWidth;
	renderer_.SetSettings(feather);
	falling_.reserve(FeatherRenderer::kMaxFeathers);
	drawList_.reserve(FeatherRenderer::kMaxFeathers);
}

void FeatherLandTransition::Finalize() {
	renderer_.Finalize();
	water_.Finalize();
	waterParamsResource_.Reset();
	waterParamsData_ = nullptr;
	falling_.clear();
	drawList_.clear();
}

void FeatherLandTransition::OnStartCover() {
	falling_.clear();
	hero_ = HeroFeather{};
	spawnAccumulator_ = 0.0f;
	time_ = 0.0f;
	followEye_ = { 0.0f, 0.0f, 0.0f };
	previousSwingCos_ = 1.0f;
	atSwingExtreme_ = false;
	waterPlaced_ = false;
	bobPulsesLeft_ = 0;
	nextBobPulseTime_ = 0.0f;
	ambientAccumulator_ = 0.0f;
	water_.Reset();
}

bool FeatherLandTransition::UpdateCover([[maybe_unused]] float dt) {
	// 振れの端（一瞬止まりかける）で覆い終わり、そこでロードする（大きい一枚がまだなら時間だけで）
	if (GetPhaseTime() < settings_.minCoverTime) return false;
	return !hero_.active || atSwingExtreme_;
}

bool FeatherLandTransition::UpdateReveal([[maybe_unused]] float dt) {
	return EmergeProgress() >= 1.0f && DissolveProgress() >= 1.0f;
}

void FeatherLandTransition::SetFullyCovered() {
	// 別の種類が覆った後に開く側だけ使うことは想定しない（覆い切った見た目＝瞼が閉じ切った暗闇）
}

// ===== 進み =====

float FeatherLandTransition::RevealSeconds() const {
	return (GetState() == TransitionState::FadeOut) ? GetPhaseTime() : 0.0f;
}

float FeatherLandTransition::EyelidClosure() const {
	if (GetState() != TransitionState::FadeIn) return 1.0f;
	return SmoothStep01((GetPhaseTime() - settings_.eyelidStart) / (std::max)(settings_.eyelidDuration, kMinPhaseSeconds));
}

float FeatherLandTransition::RainAlpha() const {
	if (GetState() != TransitionState::FadeIn) return 0.0f;
	return 1.0f - SmoothStep01((GetPhaseTime() - settings_.rainFadeStart) / (std::max)(settings_.rainFadeTime, kMinPhaseSeconds));
}

float FeatherLandTransition::WaterVisibility() const {
	return SmoothStep01(RevealSeconds() / (std::max)(settings_.tiltTime, kMinPhaseSeconds));
}

float FeatherLandTransition::EmergeProgress() const {
	if (!hero_.landed) return 0.0f;
	const float t = RevealSeconds() - hero_.landedTime - settings_.emergeDelay;
	return SmoothStep01(t / (std::max)(settings_.emergeTime, kMinPhaseSeconds));
}

float FeatherLandTransition::DissolveProgress() const {
	if (!hero_.landed) return 0.0f;
	const float t = RevealSeconds() - hero_.landedTime - settings_.dissolveDelay;
	return std::clamp(t / (std::max)(settings_.dissolveTime, kMinPhaseSeconds), 0.0f, 1.0f);
}

float FeatherLandTransition::VisibleHalfHeight(float depth) const {
	return depth * std::tan(settings_.fovY * 0.5f);
}

Vector2 FeatherLandTransition::ProjectToScreen(const Vector3& world) const {
	const Matrix4x4& vp = renderer_.GetCamera().GetViewProjectionMatrix();
	const float x = world.x * vp.m[0][0] + world.y * vp.m[1][0] + world.z * vp.m[2][0] + vp.m[3][0];
	const float y = world.x * vp.m[0][1] + world.y * vp.m[1][1] + world.z * vp.m[2][1] + vp.m[3][1];
	const float w = world.x * vp.m[0][3] + world.y * vp.m[1][3] + world.z * vp.m[2][3] + vp.m[3][3];
	if (std::abs(w) < kMinClipW) return { 0.5f, 0.5f };
	return { x / w * 0.5f + 0.5f, 0.5f - y / w * 0.5f };
}

// ===== 雨 =====

void FeatherLandTransition::SpawnFeather() {
	if (falling_.size() + 1 >= FeatherRenderer::kMaxFeathers) return;   // 大きい一枚の分を残す
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const auto range = [&](float a, float b) { return a + (b - a) * unit(random_); };
	const Settings& s = settings_;
	const float aspect = GetScreenWidth() / (std::max)(GetScreenHeight(), 1.0f);

	FallingFeather f{};
	const float depth = range(s.nearDepth, s.farDepth);
	const float halfHeight = VisibleHalfHeight(depth);
	const float halfWidth = halfHeight * aspect;
	f.look.size = range(s.sizeMin, s.sizeMax);
	// 画面上部の中央から：上端の少し上に、中央寄りに散らして出す（カメラは雨の間は原点）
	f.origin = { range(-1.0f, 1.0f) * halfWidth * s.spawnSpread, halfHeight + f.look.size, depth };
	f.fallSpeed = range(s.fallSpeedMin, s.fallSpeedMax);
	f.swayPhase = range(0.0f, 2.0f * kPi);
	f.swaySpeed = s.swayFrequency * range(kSwaySpeedJitterMin, kSwaySpeedJitterMax);
	f.swayAmplitude = s.swayAmplitude * range(kSwayAmplitudeJitterMin, kSwayAmplitudeJitterMax);
	f.baseRotation = { range(-kRainTiltXRange, kRainTiltXRange), range(0.0f, 2.0f * kPi), range(-kRainTiltZRange, kRainTiltZRange) };
	f.spinSpeed = range(-s.spinSpeed, s.spinSpeed);
	f.flutterPhase = range(0.0f, 2.0f * kPi);
	f.baseBend = range(s.bendMin, s.bendMax);
	f.look.bend = f.baseBend;
	f.look.cup = range(kRainCupMin, kRainCupMax);
	f.look.texture = static_cast<uint32_t>(unit(random_) * FeatherRenderer::kTextureCount) % FeatherRenderer::kTextureCount;
	f.look.noiseOffset = unit(random_);
	f.burnDelay = unit(random_) * (std::max)(s.rainBurnStagger, 0.0f);
	// 奥の羽根は少し暗く（奥行きが読めるように）
	f.look.brightness = 1.0f - kFarDarkening * (depth - s.nearDepth) / (std::max)(s.farDepth - s.nearDepth, kMinDepthRange);
	falling_.push_back(f);
}

void FeatherLandTransition::UpdateFeathers(float dt) {
	const Settings& s = settings_;
	const float rainAlpha = RainAlpha();
	for (FallingFeather& f : falling_) {
		// 落ち葉と同じ：振り子のように左右へ揺れ、揺れの向きへ傾きながら、ゆっくり回って落ちる
		f.origin.y -= f.fallSpeed * dt;
		f.swayPhase += f.swaySpeed * dt;
		f.baseRotation.y += f.spinSpeed * dt;
		f.flutterPhase += kFlutterSpeed * dt;
		f.look.position = { f.origin.x + f.swayAmplitude * std::sin(f.swayPhase), f.origin.y, f.origin.z };
		f.look.rotation = { f.baseRotation.x, f.baseRotation.y, f.baseRotation.z + s.swayTilt * std::cos(f.swayPhase) };
		f.look.bend = f.baseBend + s.flutter * std::sin(f.flutterPhase);
		// 薄れる代わりに焼けて消える（死亡演出の羽根と同じオレンジの縁。19 番 §1.3）。
		// 遅れを足した幅で割るので、雨の濃さが 0 になる時にはどの羽根も焼け切っている
		const float stagger = (std::max)(s.rainBurnStagger, 0.0f);
		f.look.alpha = 1.0f;
		f.look.dissolve = std::clamp((1.0f - rainAlpha) * (1.0f + stagger) - f.burnDelay, 0.0f, 1.0f);
	}
	// 一度画面に入ってから外へ出た羽根は捨てる（画面に投影して見る。カメラが追って下がっても傾いても同じ判定）
	bool anyOnScreen = false;
	for (FallingFeather& f : falling_) {
		const bool onScreen = IsOnScreen(f.look);
		if (onScreen) {
			f.seen = true;
			anyOnScreen = true;
		}
	}
	falling_.erase(std::remove_if(falling_.begin(), falling_.end(), [this](const FallingFeather& f) {
		return f.seen && !IsOnScreen(f.look);
	}), falling_.end());

	// 降らせ終わった後、画面に映っている雨が無くなるか、薄れて消え切ったら（大きい一枚だけになったら）残りも捨てる
	const bool rainDone = GetPhaseTime() >= s.rainTime || GetState() != TransitionState::FadeIn;
	if ((rainDone && !anyOnScreen) || rainAlpha <= 0.0f) {
		falling_.clear();
	}
}

bool FeatherLandTransition::IsOnScreen(const FeatherRenderer::Feather& feather) const {
	const Camera& camera = renderer_.GetCamera();
	const Vector3 eye = camera.GetTranslate();
	const Vector3 forward = camera.GetForward();
	const Vector3& p = feather.position;
	const float depth = (p.x - eye.x) * forward.x + (p.y - eye.y) * forward.y + (p.z - eye.z) * forward.z;
	if (depth <= camera.GetNearClip()) return false;
	// 羽根の大きさが画面のどれだけになるか（縦の画角から）を余白にする
	const float margin = feather.size / (2.0f * depth * std::tan(camera.GetFovY() * 0.5f));
	const Vector2 uv = ProjectToScreen(p);
	return uv.x > -margin && uv.x < 1.0f + margin && uv.y > -margin && uv.y < 1.0f + margin;
}

// ===== 大きい一枚 =====

void FeatherLandTransition::SpawnHero() {
	const Settings& s = settings_;
	hero_ = HeroFeather{};
	hero_.active = true;
	// 画面上部の中央、上端の少し上から
	hero_.baseX = 0.0f;
	hero_.position = { 0.0f, VisibleHalfHeight(s.heroDepth) + s.heroSize * kHeroSpawnAboveRatio, s.heroDepth };
	hero_.look.size = s.heroSize;
	hero_.look.bend = s.heroBend;
	hero_.look.cup = kHeroCup;
	hero_.look.texture = 0;
	hero_.look.noiseOffset = kHeroNoiseOffset;
	hero_.look.brightness = 1.0f;
	previousSwingCos_ = 1.0f;
}

void FeatherLandTransition::UpdateHero(float dt) {
	if (!hero_.active) return;
	const Settings& s = settings_;

	if (!hero_.landed) {
		// 落ち葉の落ち方：左右へ振れ、振れの中央で速く・端でふわっと遅くなる。振れの向きへ傾く
		hero_.swingPhase += s.heroSwingFrequency * dt;
		hero_.flutterPhase += kFlutterSpeed * dt;
		const float swingCos = std::cos(hero_.swingPhase);
		const float fall = s.heroFallSpeed * (kHeroFallSpeedMinRatio + kHeroFallSpeedSwingRatio * swingCos * swingCos);
		hero_.position.y -= fall * dt;
		hero_.position.x = hero_.baseX + s.heroSwingAmplitude * std::sin(hero_.swingPhase);

		// 振れの端 = 左右の速さ（cos）の符号が変わった所
		atSwingExtreme_ = (previousSwingCos_ * swingCos <= 0.0f);
		previousSwingCos_ = swingCos;

		// 水面が近づいたら水平に寝ていく（開く側だけ）
		float settle = 0.0f;
		if (GetState() == TransitionState::FadeOut) {
			settle = SmoothStep01(1.0f - (hero_.position.y - waterY_) / (std::max)(s.settleHeight, kMinSettleHeight));
		}
		hero_.look.rotation = {
			Lerp(s.heroFacePitch, kHalfPi, settle),
			0.0f,
			s.heroBank * swingCos * (1.0f - settle),
		};
		hero_.look.bend = s.heroBend + s.flutter * std::sin(hero_.flutterPhase) * (1.0f - settle);

		// 着地：波紋を入れ、波の範囲の中心をここに置く
		if (GetState() == TransitionState::FadeOut && hero_.position.y <= waterY_ + kFloatHeight) {
			hero_.landed = true;
			hero_.landedTime = RevealSeconds();
			hero_.position.y = waterY_ + kFloatHeight;
			landingPoint_ = { hero_.position.x, waterY_, hero_.position.z };
			water_.SetRippleWorldCenter({ landingPoint_.x, landingPoint_.z });
			water_.AddImpulseWorld({ landingPoint_.x, landingPoint_.z }, s.impulseRadius, s.impulseStrength);
			bobPulsesLeft_ = s.bobPulseCount;
			nextBobPulseTime_ = hero_.landedTime + s.bobPulseInterval;
		}
	} else {
		// 水面に浮いて、波に合わせてわずかに上下する
		const float since = RevealSeconds() - hero_.landedTime;
		hero_.position.y = waterY_ + kFloatHeight + kBobAmplitude * std::sin(since * kBobFrequency);
		hero_.look.rotation = { kHalfPi, 0.0f, 0.0f };
		hero_.look.bend = s.heroBend * 0.5f;
	}

	hero_.look.position = hero_.position;
	hero_.look.dissolve = DissolveProgress();
	hero_.look.alpha = 1.0f;
}

void FeatherLandTransition::UpdateWaterDisturbance(float dt) {
	if (!hero_.landed) return;
	const Settings& s = settings_;
	const float now = RevealSeconds();

	// 浮いた羽根が上下するたびに、着地点から小さな輪が出る
	if (bobPulsesLeft_ > 0 && now >= nextBobPulseTime_) {
		water_.AddImpulseWorld({ landingPoint_.x, landingPoint_.z }, s.impulseRadius * kBobPulseRadiusRatio, s.bobPulseStrength);
		--bobPulsesLeft_;
		nextBobPulseTime_ += s.bobPulseInterval;
	}

	// 浮かび上がる間は、着地点のまわりに細かな波をぽつぽつ落として水面全体を揺らし続ける
	const float emerge = EmergeProgress();
	if (emerge > 0.0f && emerge < 1.0f) {
		ambientAccumulator_ += s.ambientRate * dt;
		std::uniform_real_distribution<float> unit(-1.0f, 1.0f);
		while (ambientAccumulator_ >= 1.0f) {
			const Vector2 p = { landingPoint_.x + unit(random_) * s.ambientSpread, landingPoint_.z + unit(random_) * s.ambientSpread };
			water_.AddImpulseWorld(p, s.ambientRadius, s.ambientStrength);
			ambientAccumulator_ -= 1.0f;
		}
	}
}

// ===== カメラ =====

void FeatherLandTransition::UpdateCamera(float dt) {
	const Settings& s = settings_;
	Camera& camera = renderer_.GetCamera();
	camera.SetFovY(s.fovY);

	// 追う位置：大きい一枚の高さ（と左右の振れの一部）へなめらかに追いつく。奥行きは原点のまま
	if (hero_.active && !hero_.landed) {
		const float follow = (GetState() == TransitionState::FadeIn)
			? SmoothStep01((GetPhaseTime() - s.followStartTime) / kFollowBlendSeconds) : 1.0f;
		const Vector3 target = { hero_.position.x * s.followSide * follow, hero_.position.y * follow, 0.0f };
		const float k = 1.0f - std::exp(-s.followStiffness * dt);
		followEye_ = Lerp(followEye_, target, k);
	}

	Vector3 eye = followEye_;
	float pitch = 0.0f;
	if (GetState() == TransitionState::FadeOut) {
		// 開く側：着地点（まだ落ちている間は、大きい一枚の真下の水面）を斜めに見下ろす位置へ寄せる
		const Vector3 lookAt = hero_.landed ? landingPoint_ : Vector3{ hero_.position.x, waterY_, hero_.position.z };
		const Vector3 viewEye = {
			lookAt.x,
			lookAt.y + std::sin(s.viewPitch) * s.viewDistance,
			lookAt.z - std::cos(s.viewPitch) * s.viewDistance,
		};
		const float tilt = WaterVisibility();
		eye = Lerp(followEye_, viewEye, tilt);
		pitch = s.viewPitch * tilt;
	}

	camera.SetTranslate(eye);
	camera.SetRotate({ pitch, 0.0f, 0.0f });
	camera.Update();
}

void FeatherLandTransition::UpdateVisual() {
	if (!IsTransitioning()) return;
	const float dt = GetStepDeltaTime();
	time_ += dt;

	if (GetState() == TransitionState::FadeIn) {
		if (GetPhaseTime() < settings_.rainTime) {
			spawnAccumulator_ += settings_.spawnRate * dt;
			while (spawnAccumulator_ >= 1.0f) {
				SpawnFeather();
				spawnAccumulator_ -= 1.0f;
			}
		}
		if (!hero_.active && GetPhaseTime() >= settings_.heroSpawnTime) {
			SpawnHero();
		}
	}
	// 開く側の始まりで水面の高さを決める（大きい一枚の waterDrop 下）
	if (GetState() == TransitionState::FadeOut && !waterPlaced_) {
		waterY_ = hero_.position.y - settings_.waterDrop;
		waterPlaced_ = true;
	}

	UpdateHero(dt);
	UpdateWaterDisturbance(dt);
	UpdateCamera(dt);
	UpdateFeathers(dt);
	water_.Update(dt);
}

// ===== 描画 =====

FeatherLandTransition::WaterParamsForGPU FeatherLandTransition::WriteWaterParams() {
	static_assert(sizeof(WaterParamsForGPU) == 208, "FeatherWater.PS.hlsl の FeatherWaterParams と大きさを合わせる");
	const Settings& s = settings_;
	Camera& camera = renderer_.GetCamera();
	WaterParamsForGPU p{};
	p.water = water_.GetConstants();
	p.inverseViewProjection = Inverse(camera.GetViewProjectionMatrix());
	p.cameraPosition = camera.GetTranslate();
	p.waterY = waterY_;
	p.eyelidClosure = EyelidClosure();
	p.eyelidCurvature = s.eyelidCurvature;
	p.eyelidSoftness = (std::max)(s.eyelidSoftness, kMinEyelidSoftness);
	p.eyelidCloseY = s.eyelidCloseY;
	p.waterVisibility = WaterVisibility();
	p.emerge = EmergeProgress();
	p.time = time_;
	p.fogDensity = s.fogDensity;
	p.waterColor = s.waterColor;
	p.specular = s.specular;
	p.skyColor = s.skyColor;
	p.waveNormalScale = s.waveNormalScale;
	p.emergeCenter = hero_.landed ? ProjectToScreen(landingPoint_) : Vector2{ 0.5f, 0.5f };
	p.emergeRadialBias = (std::max)(s.emergeRadialBias, 0.0f);
	p.emergeNoise = s.emergeNoise;
	p.lightDirection = Normalize(renderer_.GetSettings().lightDirection);
	p.emergeDistortion = (std::max)(s.emergeDistortion, 0.0f);
	*waterParamsData_ = p;
	return p;
}

void FeatherLandTransition::Draw() {
	if (!IsTransitioning()) return;
	RenderTexture* sceneTarget = Game::GetPostEffect()->GetSceneRenderTarget();
	if (!sceneTarget) return;

	const WaterParamsForGPU params = WriteWaterParams();
	auto* cmd = GetDxCore()->GetCommandList();

	// 1. 瞼で閉じたシーン・水面・浮かび上がり。シーンを読むのは、瞼が開いている間と浮かび上がる間だけ
	const bool readsScene = params.eyelidClosure < 1.0f || params.emerge > 0.0f;
	water_.BeginDraw(cmd, sceneTarget, readsScene);
	cmd->SetGraphicsRootSignature(waterRootSignature_.Get());
	cmd->SetPipelineState(waterPipelineState_.Get());
	cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmd->SetGraphicsRootConstantBufferView(ScreenWaterSurface::kRootParams, waterParamsResource_->GetGPUVirtualAddress());
	water_.BindTextures(cmd);
	cmd->SetGraphicsRootDescriptorTable(kRootNoise, TextureManager::GetInstance()->GetSrvHandleGPU(kNoisePath));
	cmd->DrawInstanced(kFullscreenTriangleVertexCount, 1, 0, 0);

	// 2. その上に羽根（雨＋大きい一枚）
	drawList_.clear();
	for (const FallingFeather& f : falling_) drawList_.push_back(f.look);
	if (hero_.active && hero_.look.dissolve < 1.0f) drawList_.push_back(hero_.look);
	renderer_.Draw(cmd, drawList_, time_);

	water_.EndDraw(cmd, sceneTarget);
}

void FeatherLandTransition::OnImGui() {
#ifdef _DEBUG
	Settings& s = settings_;
	ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)  rain %zu  landed %d  emerge %.2f",
		static_cast<int>(GetState()), falling_.size(), hero_.landed ? 1 : 0, EmergeProgress());
	ImGui::SeparatorText("Cover");
	ImGui::DragFloat("Rain Time [s]", &s.rainTime, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Rain Fade Start [s]", &s.rainFadeStart, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Rain Fade Time [s]", &s.rainFadeTime, 0.01f, 0.01f, 10.0f);
	ImGui::SliderFloat("Rain Burn Stagger", &s.rainBurnStagger, 0.0f, 1.0f);
	ImGui::DragFloat("Hero Spawn Time [s]", &s.heroSpawnTime, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Follow Start Time [s]", &s.followStartTime, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Min Cover Time [s]", &s.minCoverTime, 0.01f, 0.1f, 10.0f);
	{ float hold = GetHoldDuration(); if (ImGui::DragFloat("Hold Duration [s]", &hold, 0.01f, 0.0f, 3.0f)) SetHoldDuration(hold); }
	ImGui::SeparatorText("Eyelid");
	ImGui::DragFloat("Eyelid Start [s]", &s.eyelidStart, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Eyelid Duration [s]", &s.eyelidDuration, 0.01f, 0.05f, 10.0f);
	ImGui::SliderFloat("Eyelid Curvature", &s.eyelidCurvature, 0.0f, 0.95f);
	ImGui::DragFloat("Eyelid Softness", &s.eyelidSoftness, 0.002f, 0.001f, 0.3f);
	ImGui::SliderFloat("Eyelid Close Y", &s.eyelidCloseY, 0.2f, 0.8f);
	ImGui::SeparatorText("Rain");
	ImGui::DragFloat("Spawn Rate [/s]", &s.spawnRate, 1.0f, 0.0f, 400.0f);
	ImGui::DragFloat("Camera FovY [rad]", &s.fovY, 0.01f, 0.2f, 2.0f);
	ImGui::DragFloat("Near Depth", &s.nearDepth, 0.05f, 0.5f, 30.0f);
	ImGui::DragFloat("Far Depth", &s.farDepth, 0.05f, 0.5f, 60.0f);
	ImGui::SliderFloat("Spawn Spread", &s.spawnSpread, 0.0f, 1.5f);
	ImGui::DragFloat("Size Min", &s.sizeMin, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Size Max", &s.sizeMax, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Fall Speed Min", &s.fallSpeedMin, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Fall Speed Max", &s.fallSpeedMax, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Sway Amplitude", &s.swayAmplitude, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Sway Frequency", &s.swayFrequency, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Sway Tilt [rad]", &s.swayTilt, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Spin Speed [rad/s]", &s.spinSpeed, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Bend Min", &s.bendMin, 0.01f, -1.0f, 1.0f);
	ImGui::DragFloat("Bend Max", &s.bendMax, 0.01f, -1.0f, 1.0f);
	ImGui::DragFloat("Flutter", &s.flutter, 0.005f, 0.0f, 1.0f);
	ImGui::SeparatorText("Hero Feather");
	ImGui::DragFloat("Hero Size", &s.heroSize, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Hero Depth", &s.heroDepth, 0.05f, 0.5f, 30.0f);
	ImGui::DragFloat("Hero Fall Speed", &s.heroFallSpeed, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Hero Swing Amplitude", &s.heroSwingAmplitude, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Hero Swing Frequency", &s.heroSwingFrequency, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Hero Bank [rad]", &s.heroBank, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Hero Face Pitch [rad]", &s.heroFacePitch, 0.01f, -1.5f, 1.5f);
	ImGui::DragFloat("Hero Bend", &s.heroBend, 0.01f, -1.0f, 1.0f);
	ImGui::DragFloat("Follow Stiffness", &s.followStiffness, 0.05f, 0.1f, 30.0f);
	ImGui::SliderFloat("Follow Side", &s.followSide, 0.0f, 1.0f);
	ImGui::SeparatorText("Reveal");
	ImGui::DragFloat("Water Drop", &s.waterDrop, 0.01f, 0.1f, 10.0f);
	ImGui::DragFloat("Tilt Time [s]", &s.tiltTime, 0.01f, 0.05f, 10.0f);
	ImGui::SliderAngle("View Pitch", &s.viewPitch, 5.0f, 89.0f);
	ImGui::DragFloat("View Distance", &s.viewDistance, 0.05f, 0.3f, 30.0f);
	ImGui::DragFloat("Settle Height", &s.settleHeight, 0.01f, 0.01f, 5.0f);
	ImGui::DragFloat("Dissolve Delay [s]", &s.dissolveDelay, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Dissolve Time [s]", &s.dissolveTime, 0.01f, 0.05f, 5.0f);
	ImGui::DragFloat("Emerge Delay [s]", &s.emergeDelay, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Emerge Time [s]", &s.emergeTime, 0.01f, 0.05f, 5.0f);
	ImGui::DragFloat("Impulse Radius", &s.impulseRadius, 0.005f, 0.01f, 3.0f);
	ImGui::DragFloat("Impulse Strength", &s.impulseStrength, 0.001f, 0.0f, 1.0f);
	ImGui::DragInt("Bob Pulse Count", &s.bobPulseCount, 0.1f, 0, 20);
	ImGui::DragFloat("Bob Pulse Interval [s]", &s.bobPulseInterval, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Bob Pulse Strength", &s.bobPulseStrength, 0.001f, 0.0f, 1.0f);
	ImGui::DragFloat("Ambient Rate [/s]", &s.ambientRate, 0.1f, 0.0f, 100.0f);
	ImGui::DragFloat("Ambient Radius", &s.ambientRadius, 0.005f, 0.01f, 2.0f);
	ImGui::DragFloat("Ambient Strength", &s.ambientStrength, 0.001f, 0.0f, 1.0f);
	ImGui::DragFloat("Ambient Spread", &s.ambientSpread, 0.05f, 0.0f, 20.0f);
	ImGui::SeparatorText("Water Look");
	ImGui::ColorEdit3("Water Color", &s.waterColor.x);
	ImGui::ColorEdit3("Sky Color", &s.skyColor.x);
	ImGui::DragFloat("Specular", &s.specular, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Fog Density", &s.fogDensity, 0.001f, 0.0f, 1.0f);
	ImGui::DragFloat("Wave Normal Scale", &s.waveNormalScale, 0.01f, 0.0f, 20.0f);
	ImGui::DragFloat("Emerge Radial Bias", &s.emergeRadialBias, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Emerge Noise", &s.emergeNoise, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Emerge Distortion", &s.emergeDistortion, 0.05f, 0.0f, 20.0f);
	ImGui::SeparatorText("Ripple");
	water_.OnImGui();
	ImGui::SeparatorText("Feather Look");
	renderer_.OnImGui();
	// シーンを切り替えずに流す（浮かび上がるのは今のシーン）
	if (ImGui::Button("Preview (no scene change)")) {
		TransitionManager::GetInstance()->StartTransition(TransitionType::FeatherLand, "", "", nullptr);
	}
#endif
}

void FeatherLandTransition::LoadTuning(const JsonValue& json) {
	TransitionTuning::Load(json, settings_);
	ScreenWaterSurface::Settings water = water_.GetSettings();
	TransitionTuning::Load(json["water"], water);
	water_.SetSettings(water);
	FeatherRenderer::Settings feather = renderer_.GetSettings();
	TransitionTuning::Load(json["feather"], feather);
	renderer_.SetSettings(feather);
	SetHoldDuration(static_cast<float>(json["holdDuration"].AsDouble(GetHoldDuration())));
}

void FeatherLandTransition::SaveTuning(JsonValue& json) const {
	json = TransitionTuning::Save(settings_);
	json["water"] = TransitionTuning::Save(water_.GetSettings());
	json["feather"] = TransitionTuning::Save(renderer_.GetSettings());
	json["holdDuration"] = JsonValue(static_cast<double>(GetHoldDuration()));
}
