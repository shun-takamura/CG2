#include "FeatherRiseTransition.h"
#include "TransitionTuning.h"
#include "TransitionManager.h"
#include "SceneManager.h"
#include "DirectXCore.h"
#include "RenderTexture.h"
#include "PostEffect.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "MathUtility.h"
#include "TextureManager.h"
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
	// 定数バッファの大きさを 256 バイト単位にそろえるためのマスク
	constexpr int kConstantBufferAlignMask = 255;
	// 画面を羽根で埋めるので明るくした、光の当たらない面の明るさ
	constexpr float kFeatherAmbient = 0.8f;
	// 裏から光が透ける強さ
	constexpr float kFeatherTranslucency = 0.5f;
	// よみがえる一枚の反り
	constexpr float kHeroBaseBend = 0.3f;
	// よみがえる一枚の横の丸み
	constexpr float kHeroCup = 0.12f;
	// よみがえる一枚のディゾルブの欠け方を決めるノイズのずらし（着水とそろえる）
	constexpr float kHeroNoiseOffset = 0.37f;
	// よみがえる一枚の最初の傾き [rad]
	constexpr Vector3 kHeroStartRotation = { 0.35f, 0.0f, 0.2f };
	// 各段の長さの下限 [秒]（0 で割らないように）
	constexpr float kMinPhaseSeconds = 0.01f;
	// 風の向きの長さがこれ以下なら既定の向きにする
	constexpr float kMinWindLength = 1e-4f;
	// 風の向きが 0 のときに使う向き（左上へ）
	constexpr Vector3 kDefaultWindDirection = Vector3{ -0.8f, 0.6f, 0.0f };
	// 羽根の長さのばらつき（倍率）の最小
	constexpr float kSizeJitterMin = 0.8f;
	// 羽根の長さのばらつき（倍率）の最大
	constexpr float kSizeJitterMax = 1.2f;
	// 風の向きの成分がこれより小さければ、その軸では画面に入らない（ずっと中か外）とみなす
	constexpr float kMinWindAxis = 1e-5f;
	// 画面に入る t が決まらないときの十分大きな値
	constexpr float kNeverEnterT = 1e9f;
	// 羽根の反りの最小
	constexpr float kBendMin = 0.15f;
	// 羽根の反りの最大
	constexpr float kBendMax = 0.35f;
	// 羽根の横の丸みの最小
	constexpr float kCupMin = 0.05f;
	// 羽根の横の丸みの最大
	constexpr float kCupMax = 0.15f;
	// 一緒に舞い上がる羽根の長さの倍率
	constexpr float kBurstSizeScale = 0.8f;
	// 一緒に舞い上がる羽根を出す所の左右のばらつき（±）[m]
	constexpr float kBurstSpreadX = 0.3f;
	// 一緒に舞い上がる羽根を出す所の上下のばらつき（±）[m]
	constexpr float kBurstSpreadY = 0.2f;
	// 一緒に舞い上がる羽根を出す所の奥行きのばらつき（±）[m]
	constexpr float kBurstSpreadZ = 0.8f;
	// 一緒に舞い上がる羽根の速さのばらつき（倍率）の最小
	constexpr float kBurstSpeedJitterMin = 0.4f;
	// 一緒に舞い上がる羽根に足す上向きの速さの最小 [m/秒]
	constexpr float kBurstUpSpeedMin = 0.5f;
	// 一緒に舞い上がる羽根に足す上向きの速さの最大 [m/秒]
	constexpr float kBurstUpSpeedMax = 2.0f;
	// 羽根の反りが揺れる速さ [rad/秒]
	constexpr float kFlutterSpeed = 8.0f;
	// 飛ぶ羽根の反りが揺れる量
	constexpr float kFlutterBend = 0.1f;
	// 画面の外へ抜けたとみなす余白（羽根の長さの何倍か）
	constexpr float kCullMarginSizeRatio = 2.0f;
	// 飛び立つ一枚の加速に足す上向き
	constexpr float kHeroLiftUp = 0.3f;
	// 飛び立つ一枚の回る速さ [rad/秒]
	constexpr Vector3 kHeroSpinSpeed = { 2.0f, 3.0f, 4.0f };
	// 飛び立つ一枚の反りが揺れる量
	constexpr float kHeroFlutterBend = 0.12f;
	// よみがえった直後の一枚の Z 軸まわりの傾き [rad]
	constexpr float kHeroRestRoll = 0.2f;
	// よみがえった直後の一枚が水面で揺れる幅 [rad]
	constexpr float kHeroRestSwayAmplitude = 0.05f;
	// よみがえった直後の一枚が水面で揺れる速さ [rad/秒]
	constexpr float kHeroRestSwaySpeed = 3.0f;
	// 画面全体を覆う三角形の頂点の数
	constexpr int kFullscreenTriangleVertexCount = 3;

	constexpr const wchar_t* kRipplePixelShaderPath = L"Resources/Shaders/Transition/ScreenRipple.PS.hlsl";
	// 隙間埋めの濃淡（InkShape.hlsli の InkNoise）。ScreenWaterSurface の t0 / t1 の後ろの t2
	constexpr const char* kNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";
	constexpr UINT kRootNoise = ScreenWaterSurface::kRootExtraTextures;
	// kPi はエンジンの MathUtility.h のものを使う

	/// <summary>0〜1 に切ってから smoothstep をかける</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 のなめらかな値</returns>
	float SmoothStep01(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * t);
	}
	/// <summary>2 つのベクトルを足す（Vector3 には演算子が無いので用意する）</summary>
	/// <param name="a">左辺</param>
	/// <param name="b">右辺</param>
	/// <returns>a + b</returns>
	Vector3 Add3(const Vector3& a, const Vector3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
	/// <summary>ベクトルを s 倍する</summary>
	/// <param name="v">ベクトル</param>
	/// <param name="s">倍率</param>
	/// <returns>v × s</returns>
	Vector3 Scale3(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }
}

FeatherRiseTransition::~FeatherRiseTransition() = default;

void FeatherRiseTransition::OnInitialize() {

	SRVManager* srvManager = SceneManager::GetInstance()->GetSRVManager();
	water_.Initialize(GetDxCore(), srvManager, GetScreenWidth(), GetScreenHeight());
	rippleRootSignature_ = water_.CreateRootSignature(1);
	TextureManager::GetInstance()->LoadTexture(kNoisePath);
	ripplePipelineState_ = water_.CreatePipelineState(rippleRootSignature_.Get(), kRipplePixelShaderPath, false);
	rippleParamsResource_ = GetDxCore()->CreateBufferResource((sizeof(RippleParamsForGPU) + kConstantBufferAlignMask) & ~kConstantBufferAlignMask);
	rippleParamsResource_->Map(0, nullptr, reinterpret_cast<void**>(&rippleParamsData_));
	*rippleParamsData_ = RippleParamsForGPU{};

	renderer_.Initialize(GetDxCore(), srvManager);
	// 画面を羽根で埋め尽くすので、陰を明るくして白く見せる
	FeatherRenderer::Settings feather = renderer_.GetSettings();
	feather.ambient = kFeatherAmbient;
	feather.translucency = kFeatherTranslucency;
	renderer_.SetSettings(feather);

	// 最後に画面を真っ白にする（羽根の上に重ねる）
	whiteSprite_ = std::make_unique<SpriteInstance>();
	whiteSprite_->Initialize(GetSpriteManager(), "Resources/Textures/white1x1.dds", "FeatherRiseWhite");
	whiteSprite_->SetPosition({ 0.0f, 0.0f });
	whiteSprite_->SetSize({ GetScreenWidth(), GetScreenHeight() });
	whiteSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 0.0f });
	whiteSprite_->Update();

	flying_.reserve(FeatherRenderer::kMaxFeathers);
	drawList_.reserve(FeatherRenderer::kMaxFeathers);
}

void FeatherRiseTransition::Finalize() {
	renderer_.Finalize();
	water_.Finalize();
	rippleParamsResource_.Reset();
	rippleParamsData_ = nullptr;
	whiteSprite_.reset();
	flying_.clear();
	drawList_.clear();
}

void FeatherRiseTransition::OnStartCover() {
	flying_.clear();
	spawnAccumulator_ = 0.0f;
	time_ = 0.0f;
	fade_ = 1.0f;
	heroLifted_ = false;
	water_.Reset();

	const Settings& s = settings_;
	hero_ = FlyingFeather{};
	hero_.position = ScreenToWorld(s.originUV, s.heroDepth);
	hero_.baseBend = kHeroBaseBend;
	hero_.look.size = s.heroSize;
	hero_.look.bend = hero_.baseBend;
	hero_.look.cup = kHeroCup;
	hero_.look.texture = 0;
	hero_.look.noiseOffset = kHeroNoiseOffset;   // 着水のディゾルブと同じ欠け方を逆にたどる
	hero_.look.rotation = kHeroStartRotation;
	hero_.look.dissolve = 1.0f;
}

bool FeatherRiseTransition::UpdateCover([[maybe_unused]] float dt) {
	return GetPhaseTime() >= settings_.coverTime;
}

bool FeatherRiseTransition::UpdateReveal([[maybe_unused]] float dt) {
	fade_ = std::clamp(1.0f - GetPhaseTime() / (std::max)(settings_.revealTime, kMinPhaseSeconds), 0.0f, 1.0f);
	// 羽根だけの版は、残りの羽根が画面から抜け切ったら晴れ切り
	return settings_.whiteFade ? fade_ <= 0.0f : flying_.empty();
}

void FeatherRiseTransition::SetFullyCovered() {
	fade_ = 1.0f;
}

float FeatherRiseTransition::Whiteness() const {
	if (!settings_.whiteFade) return 0.0f;
	switch (GetState()) {
	case TransitionState::FadeIn:
		return SmoothStep01((GetPhaseTime() - settings_.whiteStart) / (std::max)(settings_.whiteTime, kMinPhaseSeconds));
	case TransitionState::Hold:
		return 1.0f;
	case TransitionState::FadeOut:
		return fade_;
	default:
		return 0.0f;
	}
}

float FeatherRiseTransition::GapFill() const {
	const Settings& s = settings_;
	if (!s.gapFill) return 0.0f;
	switch (GetState()) {
	case TransitionState::FadeIn:
		return SmoothStep01((GetPhaseTime() - s.fillStart) / (std::max)(s.coverTime - s.fillStart, kMinPhaseSeconds));
	case TransitionState::Hold:
		return 1.0f;
	case TransitionState::FadeOut:
		return 1.0f - SmoothStep01(GetPhaseTime() / (std::max)(s.fillOutTime, kMinPhaseSeconds));
	default:
		return 0.0f;
	}
}

Vector3 FeatherRiseTransition::WindDirection() const {
	const Vector2& w = settings_.windDirection;
	const float len = std::sqrt(w.x * w.x + w.y * w.y);
	return (len > kMinWindLength) ? Vector3{ w.x / len, w.y / len, 0.0f } : kDefaultWindDirection;
}

float FeatherRiseTransition::VisibleHalfHeight(float depth) const {
	return depth * std::tan(settings_.fovY * 0.5f);
}

Vector3 FeatherRiseTransition::ScreenToWorld(const Vector2& uv, float depth) const {
	const float halfHeight = VisibleHalfHeight(depth);
	const float halfWidth = halfHeight * GetScreenWidth() / (std::max)(GetScreenHeight(), 1.0f);
	return { (uv.x - 0.5f) * 2.0f * halfWidth, (0.5f - uv.y) * 2.0f * halfHeight, depth };
}

// ===== 羽根 =====

void FeatherRiseTransition::RandomizeFacing(FlyingFeather& f) {
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const auto range = [&](float a, float b) { return a + (b - a) * unit(random_); };
	const Settings& s = settings_;
	// 面はこちら向きのまま少しだけ傾け（X・Y）、回るのは主に画面の中（Z）
	f.look.rotation = { range(-s.faceTilt, s.faceTilt), range(-s.faceTilt, s.faceTilt), range(0.0f, 2.0f * kPi) };
	f.spin = { range(-s.tumbleSpeed, s.tumbleSpeed), range(-s.tumbleSpeed, s.tumbleSpeed), range(-s.spinSpeed, s.spinSpeed) };
}

void FeatherRiseTransition::SpawnFromUpwind() {
	if (flying_.size() + 1 >= FeatherRenderer::kMaxFeathers) return;   // よみがえった一枚の分を残す
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const auto range = [&](float a, float b) { return a + (b - a) * unit(random_); };
	const Settings& s = settings_;

	FlyingFeather f{};
	// 手前ほど多く・大きく（画面を覆う面積を稼ぐ）
	const float farness = std::pow(unit(random_), (std::max)(s.nearBias, 1.0f));
	const float depth = s.nearDepth + (s.farDepth - s.nearDepth) * farness;
	f.look.size = (s.sizeMax + (s.sizeMin - s.sizeMax) * farness) * range(kSizeJitterMin, kSizeJitterMax);
	const float halfHeight = VisibleHalfHeight(depth);
	const float halfWidth = halfHeight * GetScreenWidth() / (std::max)(GetScreenHeight(), 1.0f);
	// 通り道（風向きの直線）を、風と直角の方向に均等に並べ、その線が画面（＋羽根の大きさ）に入る所で出す。
	// 縁から均等に出すと対角線の両脇に偏って中央が、画面の点を均等に選ぶと線が短い角（右上・左下）が
	// スカスカになる。線を均等に並べればどの線も画面を端から端まで横切るので、角も中央も同じ濃さで覆える
	const Vector3 wind = WindDirection();
	const Vector2 across = { -wind.y, wind.x };
	const float halfW = halfWidth + f.look.size;
	const float halfH = halfHeight + f.look.size;
	// 画面の四隅を直角の方向へ投影した範囲 = 線を並べる範囲
	const float reach = std::abs(across.x) * halfW + std::abs(across.y) * halfH;
	const float offset = range(-reach, reach);
	// 線 p(t) = offset·across + t·wind が画面の長方形に入る t（左右・上下それぞれの入る t の大きい方）
	const auto entry = [](float origin, float dir, float half) {
		if (std::abs(dir) < kMinWindAxis) return (std::abs(origin) <= half) ? -kNeverEnterT : kNeverEnterT;
		const float t1 = (-half - origin) / dir;
		const float t2 = (half - origin) / dir;
		return (std::min)(t1, t2);
	};
	const float tEnter = (std::max)(entry(offset * across.x, wind.x, halfW), entry(offset * across.y, wind.y, halfH));
	f.position = { offset * across.x + tEnter * wind.x, offset * across.y + tEnter * wind.y, depth };
	f.velocity = Scale3(wind, range(s.windSpeedMin, s.windSpeedMax));
	RandomizeFacing(f);
	f.turbulencePhase = range(0.0f, 2.0f * kPi);
	f.flutterPhase = range(0.0f, 2.0f * kPi);
	f.baseBend = range(kBendMin, kBendMax);
	f.look.bend = f.baseBend;
	f.look.cup = range(kCupMin, kCupMax);
	f.look.texture = static_cast<uint32_t>(unit(random_) * FeatherRenderer::kTextureCount) % FeatherRenderer::kTextureCount;
	f.look.noiseOffset = unit(random_);
	f.look.brightness = 1.0f;
	flying_.push_back(f);
}

void FeatherRiseTransition::SpawnBurst() {
	// 飛び立つ瞬間、よみがえった一枚のまわりから一緒に舞い上がる
	std::uniform_real_distribution<float> unit(0.0f, 1.0f);
	const auto range = [&](float a, float b) { return a + (b - a) * unit(random_); };
	const Settings& s = settings_;
	const Vector3 wind = WindDirection();
	// 出す所：よみがえった一枚の画面の位置から burstOffsetX だけ左右へずらした所（奥行きは一枚と同じ）
	const Vector3 burstCenter = ScreenToWorld({ s.originUV.x + s.burstOffsetX, s.originUV.y }, hero_.position.z);
	for (int i = 0; i < s.burstCount && flying_.size() + 1 < FeatherRenderer::kMaxFeathers; ++i) {
		FlyingFeather f{};
		f.look.size = range(s.sizeMin, s.sizeMax) * kBurstSizeScale;
		f.position = Add3(burstCenter, { range(-kBurstSpreadX, kBurstSpreadX), range(-kBurstSpreadY, kBurstSpreadY), range(-kBurstSpreadZ, kBurstSpreadZ) });
		// 風の向き＋上向きに、ばらばらの速さで散る
		const float speed = range(s.windSpeedMin, s.windSpeedMax) * range(kBurstSpeedJitterMin, 1.0f);
		f.velocity = { wind.x * speed + range(-1.0f, 1.0f), wind.y * speed + range(kBurstUpSpeedMin, kBurstUpSpeedMax), range(-1.0f, 1.0f) };
		RandomizeFacing(f);
		f.turbulencePhase = range(0.0f, 2.0f * kPi);
		f.flutterPhase = range(0.0f, 2.0f * kPi);
		f.baseBend = range(kBendMin, kBendMax);
		f.look.bend = f.baseBend;
		f.look.cup = range(kCupMin, kCupMax);
		f.look.texture = static_cast<uint32_t>(unit(random_) * FeatherRenderer::kTextureCount) % FeatherRenderer::kTextureCount;
		f.look.noiseOffset = unit(random_);
		f.look.brightness = 1.0f;
		flying_.push_back(f);
	}
}

void FeatherRiseTransition::UpdateFlying(float dt) {
	const Settings& s = settings_;
	const Vector3 wind = WindDirection();
	// 風に直交する向き（画面の中で）。ここへ揺らして、まっすぐ飛ばない風の乱れを出す
	const Vector3 across = { -wind.y, wind.x, 0.0f };
	for (FlyingFeather& f : flying_) {
		f.turbulencePhase += s.turbulenceFrequency * dt;
		f.flutterPhase += kFlutterSpeed * dt;
		const float sway = s.turbulence * std::sin(f.turbulencePhase);
		f.position = Add3(f.position, Scale3(Add3(f.velocity, Scale3(across, sway)), dt));
		f.look.rotation = Add3(f.look.rotation, Scale3(f.spin, dt));
		// 傾き（X・Y）は上限の中で行ったり来たりさせる（回り続けると横を向いて細く見えるため）
		// （上限の外で毎フレーム反転して震えないよう、外へ向かって動いているときだけ折り返す）
		if (f.look.rotation.x * f.spin.x > 0.0f && std::abs(f.look.rotation.x) > s.faceTilt) f.spin.x = -f.spin.x;
		if (f.look.rotation.y * f.spin.y > 0.0f && std::abs(f.look.rotation.y) > s.faceTilt) f.spin.y = -f.spin.y;
		f.look.bend = f.baseBend + kFlutterBend * std::sin(f.flutterPhase);
		f.look.position = f.position;
		f.look.alpha = 1.0f;
	}
	// 風下（画面の左上）の外へ抜けたら捨てる
	flying_.erase(std::remove_if(flying_.begin(), flying_.end(), [this](const FlyingFeather& f) {
		const float halfHeight = VisibleHalfHeight(f.position.z);
		const float halfWidth = halfHeight * GetScreenWidth() / (std::max)(GetScreenHeight(), 1.0f);
		const float margin = f.look.size * kCullMarginSizeRatio;
		return f.position.x < -halfWidth - margin || f.position.y > halfHeight + margin;
	}), flying_.end());
}

void FeatherRiseTransition::UpdateHero(float dt) {
	const Settings& s = settings_;
	if (GetState() == TransitionState::FadeIn) {
		// よみがえる：着水のディゾルブを逆にたどる
		hero_.look.dissolve = 1.0f - SmoothStep01(GetPhaseTime() / (std::max)(s.reformTime, kMinPhaseSeconds));

		if (!heroLifted_ && GetPhaseTime() >= s.liftStart) {
			heroLifted_ = true;
			SpawnBurst();
		}
	}

	if (heroLifted_) {
		// 風に乗って加速しながら飛び立つ（少し上向きを足す）
		const Vector3 wind = WindDirection();
		hero_.velocity = Add3(hero_.velocity, Scale3({ wind.x, wind.y + kHeroLiftUp, 0.0f }, s.heroLiftAcceleration * dt));
		hero_.position = Add3(hero_.position, Scale3(hero_.velocity, dt));
		hero_.look.rotation = Add3(hero_.look.rotation, Scale3(kHeroSpinSpeed, dt));
		hero_.flutterPhase += kFlutterSpeed * dt;
		hero_.look.bend = hero_.baseBend + kHeroFlutterBend * std::sin(hero_.flutterPhase);
	} else {
		// よみがえった直後は水面でかすかに揺れる
		hero_.look.rotation.z = kHeroRestRoll + kHeroRestSwayAmplitude * std::sin(time_ * kHeroRestSwaySpeed);
	}
	hero_.look.position = hero_.position;
	hero_.look.alpha = 1.0f;
}

void FeatherRiseTransition::UpdateVisual() {
	if (!IsTransitioning()) return;
	const Settings& s = settings_;
	const float dt = GetStepDeltaTime();
	time_ += dt;

	// 突風：吹き始めから覆い終わりまで吹き込む枚数を増やし、ロードの間（Hold）も吹き込み続けて埋めたままにする。
	// 開く側では止める（残りが抜けると晴れる）
	const bool blowing = (GetState() == TransitionState::FadeIn && GetPhaseTime() >= s.gustStart) || GetState() == TransitionState::Hold;
	if (blowing) {
		const float gust = (GetState() == TransitionState::Hold) ? 1.0f
			: (GetPhaseTime() - s.gustStart) / (std::max)(s.coverTime - s.gustStart, kMinPhaseSeconds);
		const float rate = s.spawnRateStart + (s.spawnRateEnd - s.spawnRateStart) * SmoothStep01(gust);
		spawnAccumulator_ += rate * dt;
		while (spawnAccumulator_ >= 1.0f) {
			SpawnFromUpwind();
			spawnAccumulator_ -= 1.0f;
		}
	}

	UpdateHero(dt);
	UpdateFlying(dt);
	water_.Update(dt);

	Camera& camera = renderer_.GetCamera();
	camera.SetFovY(s.fovY);
	camera.SetTranslate({ 0.0f, 0.0f, 0.0f });
	camera.SetRotate({ 0.0f, 0.0f, 0.0f });
	camera.Update();
}

// ===== 描画 =====

void FeatherRiseTransition::Draw() {
	if (!IsTransitioning()) return;
	RenderTexture* sceneTarget = Game::GetPostEffect()->GetSceneRenderTarget();
	if (!sceneTarget) return;

	const float whiteness = Whiteness();
	auto* cmd = GetDxCore()->GetCommandList();

	// 真っ白の間はシーンも羽根も見えないので白だけ
	if (whiteness < 1.0f) {
		static_assert(sizeof(RippleParamsForGPU) == 48, "ScreenRipple.PS.hlsl の ScreenRippleParams と大きさを合わせる");
		RippleParamsForGPU params{};
		params.water = water_.GetConstants();
		params.fill = GapFill();
		params.fillNoiseScale = settings_.fillNoiseScale;
		params.time = time_;
		*rippleParamsData_ = params;

		// 1. 波紋で揺らしたシーン
		water_.BeginDraw(cmd, sceneTarget, true);
		cmd->SetGraphicsRootSignature(rippleRootSignature_.Get());
		cmd->SetPipelineState(ripplePipelineState_.Get());
		cmd->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		cmd->SetGraphicsRootConstantBufferView(ScreenWaterSurface::kRootParams, rippleParamsResource_->GetGPUVirtualAddress());
		water_.BindTextures(cmd);
		cmd->SetGraphicsRootDescriptorTable(kRootNoise, TextureManager::GetInstance()->GetSrvHandleGPU(kNoisePath));
		cmd->DrawInstanced(kFullscreenTriangleVertexCount, 1, 0, 0);

		// 2. 羽根（吹き込む羽根＋よみがえった一枚）
		drawList_.clear();
		for (const FlyingFeather& f : flying_) drawList_.push_back(f.look);
		drawList_.push_back(hero_.look);   // 飛び立った後は風下へ抜けていくので、ロードをまたいでも描き続ける
		renderer_.Draw(cmd, drawList_, time_);
		water_.EndDraw(cmd, sceneTarget);
	}

	// 3. 白
	if (whiteness > 0.0f) {
		whiteSprite_->SetColor({ 1.0f, 1.0f, 1.0f, whiteness });
		whiteSprite_->Update();
		GetSpriteManager()->DrawSetting();
		whiteSprite_->Draw();
	}
}

void FeatherRiseTransition::OnImGui() {
#ifdef _DEBUG
	Settings& s = settings_;
	ImGui::Text("State: %d (0=None, 1=FadeIn, 2=Hold, 3=FadeOut)  feathers %zu  white %.2f",
		static_cast<int>(GetState()), flying_.size(), Whiteness());
	ImGui::SeparatorText("Timing");
	ImGui::DragFloat("Reform Time [s]", &s.reformTime, 0.01f, 0.01f, 5.0f);
	ImGui::DragFloat("Gust Start [s]", &s.gustStart, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Lift Start [s]", &s.liftStart, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Cover Time [s]", &s.coverTime, 0.01f, 0.1f, 10.0f);
	ImGui::Checkbox("White Fade", &s.whiteFade);
	ImGui::BeginDisabled(!s.whiteFade);
	ImGui::DragFloat("White Start [s]", &s.whiteStart, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("White Time [s]", &s.whiteTime, 0.01f, 0.01f, 5.0f);
	ImGui::DragFloat("Reveal Time [s]", &s.revealTime, 0.01f, 0.01f, 5.0f);
	ImGui::EndDisabled();
	{ float hold = GetHoldDuration(); if (ImGui::DragFloat("Hold Duration [s]", &hold, 0.01f, 0.0f, 3.0f)) SetHoldDuration(hold); }
	ImGui::SeparatorText("Wind");
	ImGui::DragFloat2("Wind Direction (screen)", &s.windDirection.x, 0.01f, -1.0f, 1.0f);
	ImGui::DragFloat("Wind Speed Min", &s.windSpeedMin, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Wind Speed Max", &s.windSpeedMax, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Turbulence", &s.turbulence, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Turbulence Frequency", &s.turbulenceFrequency, 0.01f, 0.0f, 20.0f);
	ImGui::DragFloat("Spin Speed [rad/s]", &s.spinSpeed, 0.05f, 0.0f, 30.0f);
	ImGui::DragFloat("Face Tilt [rad]", &s.faceTilt, 0.01f, 0.0f, 1.5f);
	ImGui::DragFloat("Tumble Speed [rad/s]", &s.tumbleSpeed, 0.05f, 0.0f, 20.0f);
	ImGui::SeparatorText("Feathers");
	ImGui::DragFloat("Spawn Rate Start [/s]", &s.spawnRateStart, 1.0f, 0.0f, 1000.0f);
	ImGui::DragFloat("Spawn Rate End [/s]", &s.spawnRateEnd, 1.0f, 0.0f, 3000.0f);
	ImGui::DragFloat("Near Depth", &s.nearDepth, 0.05f, 0.3f, 30.0f);
	ImGui::DragFloat("Far Depth", &s.farDepth, 0.05f, 0.3f, 60.0f);
	ImGui::DragFloat("Near Bias", &s.nearBias, 0.05f, 1.0f, 8.0f);
	ImGui::DragFloat("Size Min", &s.sizeMin, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Size Max", &s.sizeMax, 0.01f, 0.05f, 3.0f);
	ImGui::DragInt("Burst Count", &s.burstCount, 0.2f, 0, 200);
	ImGui::Checkbox("Gap Fill", &s.gapFill);
	ImGui::BeginDisabled(!s.gapFill);
	ImGui::DragFloat("Fill Start [s]", &s.fillStart, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Fill Out Time [s]", &s.fillOutTime, 0.01f, 0.01f, 5.0f);
	ImGui::DragFloat("Fill Noise Scale", &s.fillNoiseScale, 0.05f, 0.1f, 20.0f);
	ImGui::EndDisabled();
	ImGui::DragFloat("Burst Offset X", &s.burstOffsetX, 0.005f, -1.0f, 1.0f);
	ImGui::SeparatorText("Reborn Feather");
	ImGui::DragFloat("Hero Depth", &s.heroDepth, 0.05f, 0.5f, 30.0f);
	ImGui::DragFloat("Hero Size", &s.heroSize, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Hero Lift Acceleration", &s.heroLiftAcceleration, 0.05f, 0.0f, 50.0f);
	ImGui::DragFloat2("Origin UV", &s.originUV.x, 0.005f, 0.0f, 1.0f);
	ImGui::SeparatorText("Feather Look");
	renderer_.OnImGui();
	if (ImGui::Button("Preview (no scene change)")) {
		TransitionManager::GetInstance()->StartTransition(TransitionType::FeatherRise, "", "", nullptr);
	}
#endif
}

void FeatherRiseTransition::LoadTuning(const JsonValue& json) {
	TransitionTuning::Load(json, settings_);
	FeatherRenderer::Settings feather = renderer_.GetSettings();
	TransitionTuning::Load(json["feather"], feather);
	renderer_.SetSettings(feather);
	SetHoldDuration(static_cast<float>(json["holdDuration"].AsDouble(GetHoldDuration())));
}

void FeatherRiseTransition::SaveTuning(JsonValue& json) const {
	json = TransitionTuning::Save(settings_);
	json["feather"] = TransitionTuning::Save(renderer_.GetSettings());
	json["holdDuration"] = JsonValue(static_cast<double>(GetHoldDuration()));
}
