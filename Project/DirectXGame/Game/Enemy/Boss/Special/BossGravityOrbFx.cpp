#include "Enemy/Boss/Special/BossGravityOrbFx.h"
#include "Game.h"
#include "Json/JsonValue.h"
#include "LightManager.h"
#include "MonochromeEffect.h"
#include "PostEffect.h"
#include "Text/TextRenderer.h"
#include "Vector2.h"
#include "Vector4.h"
#include <algorithm>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// 0 とみなす時間 [s]
	constexpr float kNearZeroTime = 1e-4f;

	// ----- モノクロの既定の並び（Monochrome.json に名前の並びが無いとき）-----
	// 放つ瞬間：一瞬で墨絵の 2 値 → 少しとどまって色へ戻る
	constexpr float kReleaseInkHold = 0.25f;
	constexpr float kReleaseReturn = 0.35f;
	// 着地の瞬間：白い線だけ残る反転 → 白が多い 2 値 → 色へ戻る
	constexpr float kImpactInvertHold = 0.12f;
	constexpr float kImpactWhiteHold = 0.18f;
	constexpr float kImpactReturn = 0.3f;

	// 調整値の float と JSON のキー・ImGui の名前の組
	struct FloatField {
		const char* key;
		const char* label;
		float BossGravityOrbFx::Tuning::* member;
		float speed;
	};
	constexpr FloatField kFloatFields[] = {
		{ "darkLightScale", "暗いときの光の強さ（倍率）", &BossGravityOrbFx::Tuning::darkLightScale, 0.01f },
		{ "darkFadeIn", "暗くなるまで", &BossGravityOrbFx::Tuning::darkFadeIn, 0.01f },
		{ "darkFadeOut", "明るさが戻るまで", &BossGravityOrbFx::Tuning::darkFadeOut, 0.01f },
		{ "releaseFlashAlpha", "放つ瞬間の閃光の濃さ", &BossGravityOrbFx::Tuning::releaseFlashAlpha, 0.01f },
		{ "releaseFlashTime", "放つ瞬間の閃光の長さ", &BossGravityOrbFx::Tuning::releaseFlashTime, 0.01f },
		{ "impactFlashAlpha", "着地の閃光の濃さ", &BossGravityOrbFx::Tuning::impactFlashAlpha, 0.01f },
		{ "impactFlashTime", "着地の閃光の長さ", &BossGravityOrbFx::Tuning::impactFlashTime, 0.01f },
		{ "whiteoutRise", "爆発の白の立ち上がり", &BossGravityOrbFx::Tuning::whiteoutRise, 0.01f },
		{ "whiteoutHold", "爆発の白のとどまり", &BossGravityOrbFx::Tuning::whiteoutHold, 0.01f },
		{ "whiteoutFall", "爆発の白が晴れるまで", &BossGravityOrbFx::Tuning::whiteoutFall, 0.01f },
	};
}

EffectHandle BossGravityOrbFx::PlayFx(const char* name, const Vector3& position) const {
	EffectManager* em = EffectManager::GetInstance();
	if (!em || !em->HasDef(name)) return kInvalidEffectHandle;
	return em->Play(name, position);
}

void BossGravityOrbFx::StopFx(EffectHandle& handle) const {
	if (handle == kInvalidEffectHandle) return;
	if (EffectManager* em = EffectManager::GetInstance()) em->Release(handle);
	handle = kInvalidEffectHandle;
}

void BossGravityOrbFx::PlayMonochrome(const char* name, bool impact) const {
	PostEffect* pe = Game::GetPostEffect();
	if (!pe || !pe->monochrome) return;
	if (pe->monochrome->Play(std::string(name))) return;
	std::vector<MonochromeKey> keys;
	if (impact) {
		keys.push_back(MonochromeEffect::MakeKey(MonochromeStage::InvertInk, 0.0f, kImpactInvertHold));
		keys.push_back(MonochromeEffect::MakeKey(MonochromeStage::WhiteInk, 0.0f, kImpactWhiteHold));
		keys.push_back(MonochromeEffect::MakeKey(MonochromeStage::Color, kImpactReturn, 0.0f));
	} else {
		keys.push_back(MonochromeEffect::MakeKey(MonochromeStage::Ink, 0.0f, kReleaseInkHold));
		keys.push_back(MonochromeEffect::MakeKey(MonochromeStage::Color, kReleaseReturn, 0.0f));
	}
	pe->monochrome->Play(keys);
}

void BossGravityOrbFx::AddFlash(float peak, float rise, float hold, float fall) {
	flashes_.push_back({ peak, rise, hold, (std::max)(fall, kNearZeroTime), 0.0f });
}

float BossGravityOrbFx::FlashAlpha(const Flash& flash) const {
	if (flash.time < flash.rise) return flash.peak * (flash.time / (std::max)(flash.rise, kNearZeroTime));
	const float afterRise = flash.time - flash.rise;
	if (afterRise < flash.hold) return flash.peak;
	const float t = (afterRise - flash.hold) / flash.fall;
	return flash.peak * (1.0f - std::clamp(t, 0.0f, 1.0f));
}

void BossGravityOrbFx::StartDebris(const Vector3& center) {
	StopFx(debris_);
	debris_ = PlayFx(kFxDebris, center);
}

void BossGravityOrbFx::StopDebris() {
	StopFx(debris_);
}

void BossGravityOrbFx::StartCharge(const Vector3& position) {
	StopFx(charge_);
	charge_ = PlayFx(kFxCharge, position);
}

void BossGravityOrbFx::Release(const Vector3& position) {
	StopFx(charge_);
	StopFx(orb_);
	StopFx(pillar_);
	orb_ = PlayFx(kFxOrb, position);
	pillar_ = PlayFx(kFxPillar, position);
	PlayMonochrome(kMonoRelease, false);
	AddFlash(tuning_.releaseFlashAlpha, 0.0f, 0.0f, tuning_.releaseFlashTime);
}

void BossGravityOrbFx::SetOrbPosition(const Vector3& position) {
	EffectManager* em = EffectManager::GetInstance();
	if (!em) return;
	if (charge_ != kInvalidEffectHandle) em->SetPosition(charge_, position);
	if (orb_ != kInvalidEffectHandle) em->SetPosition(orb_, position);
	if (pillar_ != kInvalidEffectHandle) em->SetPosition(pillar_, position);
}

void BossGravityOrbFx::Land(const Vector3& position) {
	StopFx(orb_);
	StopFx(pillar_);
	PlayFx(kFxImpact, position);
}

void BossGravityOrbFx::ImpactBurst() {
	PlayMonochrome(kMonoImpact, true);
	AddFlash(tuning_.impactFlashAlpha, 0.0f, 0.0f, tuning_.impactFlashTime);
}

void BossGravityOrbFx::Explode(const Vector3& position) {
	PlayFx(kFxExplosion, position);
	AddFlash(1.0f, tuning_.whiteoutRise, tuning_.whiteoutHold, tuning_.whiteoutFall);
}

void BossGravityOrbFx::Aftermath(const Vector3& position) {
	PlayFx(kFxEmbers, position);
}

void BossGravityOrbFx::RockLanded(const Vector3& position) {
	PlayFx(kFxRockLand, position);
}

void BossGravityOrbFx::SetDark(bool dark) {
	if (dark && !lightCaptured_) {
		if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
			baseLightIntensity_ = lm->GetDirectionalLightData()->intensity;
			lightCaptured_ = true;
			lightScale_ = 1.0f;
		}
	}
	dark_ = dark;
}

void BossGravityOrbFx::Abort() {
	StopFx(debris_);
	StopFx(charge_);
	StopFx(orb_);
	StopFx(pillar_);
	flashes_.clear();
	if (PostEffect* pe = Game::GetPostEffect(); pe && pe->monochrome && pe->monochrome->IsPlaying()) pe->monochrome->Stop();
	dark_ = false;
	if (lightCaptured_) {
		if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
			lm->GetDirectionalLightData()->intensity = baseLightIntensity_;
		}
		lightCaptured_ = false;
	}
	lightScale_ = 1.0f;
}

void BossGravityOrbFx::Discard() {
	debris_ = kInvalidEffectHandle;
	charge_ = kInvalidEffectHandle;
	orb_ = kInvalidEffectHandle;
	pillar_ = kInvalidEffectHandle;
	flashes_.clear();
	dark_ = false;
	lightCaptured_ = false;
	lightScale_ = 1.0f;
}

void BossGravityOrbFx::Update(float dt) {
	for (Flash& flash : flashes_) flash.time += dt;
	flashes_.erase(std::remove_if(flashes_.begin(), flashes_.end(), [](const Flash& f) {
		return f.time >= f.rise + f.hold + f.fall;
	}), flashes_.end());

	if (!lightCaptured_) return;
	const float target = dark_ ? tuning_.darkLightScale : 1.0f;
	const float fadeTime = (std::max)(dark_ ? tuning_.darkFadeIn : tuning_.darkFadeOut, kNearZeroTime);
	const float step = std::fabs(1.0f - tuning_.darkLightScale) / fadeTime * dt;
	if (lightScale_ < target) lightScale_ = (std::min)(lightScale_ + step, target);
	else lightScale_ = (std::max)(lightScale_ - step, target);

	auto* lm = LightManager::GetInstance();
	if (!lm || !lm->GetDirectionalLightData()) return;
	lm->GetDirectionalLightData()->intensity = baseLightIntensity_ * lightScale_;
	// 明るさが戻り切ったら元の値へ戻して手を離す
	if (!dark_ && lightScale_ >= 1.0f) {
		lm->GetDirectionalLightData()->intensity = baseLightIntensity_;
		lightCaptured_ = false;
	}
}

void BossGravityOrbFx::DrawOverlay(float screenW, float screenH) const {
	float alpha = 0.0f;
	for (const Flash& flash : flashes_) alpha = (std::max)(alpha, FlashAlpha(flash));
	if (alpha <= 0.0f) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr || !tr->IsInitialized()) return;
	tr->DrawRect({ 0.0f, 0.0f }, { screenW, screenH }, { 1.0f, 1.0f, 1.0f, std::clamp(alpha, 0.0f, 1.0f) });
}

void BossGravityOrbFx::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	for (const FloatField& field : kFloatFields) {
		tuning_.*field.member = static_cast<float>(node[field.key].AsDouble(tuning_.*field.member));
	}
}

void BossGravityOrbFx::SaveTuning(JsonValue& node) const {
	for (const FloatField& field : kFloatFields) {
		node[field.key] = static_cast<double>(tuning_.*field.member);
	}
}

void BossGravityOrbFx::OnImGui(bool& changed) {
#ifdef USE_IMGUI
	for (const FloatField& field : kFloatFields) {
		changed |= ImGui::DragFloat(field.label, &(tuning_.*field.member), field.speed, 0.0f, 10.0f, "%.2f");
	}
	EffectManager* em = EffectManager::GetInstance();
	ImGui::TextUnformatted("エフェクト（無い物は出さない）");
	for (const char* name : { kFxDebris, kFxCharge, kFxOrb, kFxPillar, kFxImpact, kFxExplosion, kFxEmbers, kFxRockLand }) {
		const bool has = em && em->HasDef(name);
		ImGui::TextColored(has ? ImVec4{ 0.6f, 1.0f, 0.6f, 1.0f } : ImVec4{ 1.0f, 0.5f, 0.4f, 1.0f }, "  %s %s", has ? "o" : "x", name);
	}
#else
	(void)changed;
#endif
}
