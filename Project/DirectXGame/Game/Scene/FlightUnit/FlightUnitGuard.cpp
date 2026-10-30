#include "FlightUnitGuard.h"

#include "Components/Gameplay.h"
#include "Effect/EffectPrimitiveRenderer.h"
#include "Primitive/PrimitivePipeline.h"
#include "Score/DifficultyProfile.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

FlightUnitGuard::FlightUnitGuard() = default;
FlightUnitGuard::~FlightUnitGuard() = default;

void FlightUnitGuard::Initialize(IFlightUnitHost* host, Camera* camera) {
	FlightUnitSkill::Initialize(host, camera);
	vis_ = std::make_unique<EffectPrimitiveRenderer>();
	vis_->Initialize(2 /*Sphere*/, "Resources/Textures/white1x1.dds");
	vis_->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
	vis_->SetDepthWrite(false);
	vis_->SetCullBackface(false);
	vis_->SetColor(color_);
}

bool FlightUnitGuard::Activate() {
	if (!host_) return false;
	IImGuiEditable* player = host_->GetPlayer();
	const int maxHp = player ? Gameplay::Of(player).GetHP().maxHP : 100;
	durabilityMax_ = (std::max)(1.0f, static_cast<float>(maxHp) * barrierHpRatio_[LevelIndex()]);
	durability_ = durabilityMax_;
	active_ = true;
	timer_ = 0.0f;
	contactTimer_ = 0.0f;
	hitFlash_ = 1.0f;
	return true;
}

void FlightUnitGuard::Cancel(bool restoreTime) {
	(void)restoreTime;
	active_ = false;
	timer_ = 0.0f;
}

float FlightUnitGuard::GetActiveRatio() const {
	// 時間と耐久力の少ない方
	const float d = barrierDuration_[LevelIndex()];
	const float byTime = d > 1e-4f ? 1.0f - timer_ / d : 0.0f;
	const float byHp = durabilityMax_ > 1e-4f ? durability_ / durabilityMax_ : 0.0f;
	return std::clamp((std::min)(byTime, byHp), 0.0f, 1.0f);
}

void FlightUnitGuard::Drain(int damage) {
	durability_ -= static_cast<float>(damage);
	hitFlash_ = 1.0f;
	if (durability_ <= 0.0f) {
		durability_ = 0.0f;
		Cancel(false);
	}
}

void FlightUnitGuard::Update(float dt, bool canAct) {
	(void)canAct;
	if (passiveTimer_ > 0.0f) passiveTimer_ -= dt;
	if (flashTimer_ > 0.0f) flashTimer_ -= dt;
	if (contactTimer_ > 0.0f) contactTimer_ -= dt;
	hitFlash_ = (std::max)(0.0f, hitFlash_ - dt * 4.0f);
	if (!host_) return;

	if (active_) {
		timer_ += dt;
		if (timer_ >= barrierDuration_[LevelIndex()]) {
			Cancel(false);
		} else {
			// バリアに入った敵弾を消して耐久力を減らす（被弾と同じ難易度の倍率を掛ける）
			const int raw = host_->AbsorbEnemyBulletsInSphere(host_->GetFlightUnitAnchor(), barrierRadius_);
			if (raw > 0) Drain(DifficultyProfile::ScaleInt(raw, DifficultyProfile::GetInstance()->GetDamageScale()));
		}
	}

	if (vis_ && (active_ || flashTimer_ > 0.0f)) {
		const float d = barrierRadius_ * 2.0f;
		vis_->SetTranslate(host_->GetFlightUnitAnchor());
		vis_->SetScale({ d, d, d });
		float a = barrierAlpha_ * (1.0f + hitFlash_);
		if (active_) {
			// 耐久力が減るほど薄く、残りが少ないと点滅する
			const float hp = durabilityMax_ > 1e-4f ? durability_ / durabilityMax_ : 0.0f;
			a *= 0.4f + 0.6f * hp;
			if (hp < 0.25f) a *= 0.6f + 0.4f * std::sin(timer_ * 30.0f);
		} else {
			a *= flashDuration_ > 1e-4f ? flashTimer_ / flashDuration_ : 0.0f;
		}
		vis_->SetColor({ color_.x, color_.y, color_.z, a });
		vis_->Update(camera_, dt);
	}
}

void FlightUnitGuard::Draw() {
	if (vis_ && (active_ || flashTimer_ > 0.0f)) vis_->Draw();
}

int FlightUnitGuard::ModifyIncomingDamage(int damage) {
	if (passiveTimer_ > 0.0f || damage <= 0) return damage;
	passiveTimer_ = passiveCooldown_[LevelIndex()];
	flashTimer_ = flashDuration_;
	hitFlash_ = 1.0f;
	const float reduced = static_cast<float>(damage) * (1.0f - std::clamp(passiveReduce_[LevelIndex()], 0.0f, 1.0f));
	return (std::max)(0, static_cast<int>(std::lround(reduced)));
}

bool FlightUnitGuard::AbsorbHit(int damage) {
	if (!active_) return false;
	// 弾でない攻撃は当たり続けるので、間隔を空けて 1 回分だけ減らす
	if (contactTimer_ <= 0.0f) {
		contactTimer_ = contactHitInterval_;
		Drain(damage);
	}
	return true;
}

void FlightUnitGuard::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "passiveReduce", passiveReduce_);
	ReadLevels(node, "passiveCooldown", passiveCooldown_);
	ReadLevels(node, "barrierHpRatio", barrierHpRatio_);
	ReadLevels(node, "barrierDuration", barrierDuration_);
	ReadLevels(node, "cooldown", cooldown_);
	ReadLevels(node, "maxHpScale", maxHpScale_);
	barrierRadius_ = static_cast<float>(node["barrierRadius"].AsDouble(barrierRadius_));
	contactHitInterval_ = static_cast<float>(node["contactHitInterval"].AsDouble(contactHitInterval_));
	barrierAlpha_ = static_cast<float>(node["barrierAlpha"].AsDouble(barrierAlpha_));
	flashDuration_ = static_cast<float>(node["flashDuration"].AsDouble(flashDuration_));
	ReadVec4(node, "color", color_);
}

void FlightUnitGuard::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "passiveReduce", passiveReduce_);
	WriteLevels(node, "passiveCooldown", passiveCooldown_);
	WriteLevels(node, "barrierHpRatio", barrierHpRatio_);
	WriteLevels(node, "barrierDuration", barrierDuration_);
	WriteLevels(node, "cooldown", cooldown_);
	WriteLevels(node, "maxHpScale", maxHpScale_);
	node["barrierRadius"] = JsonValue(static_cast<double>(barrierRadius_));
	node["contactHitInterval"] = JsonValue(static_cast<double>(contactHitInterval_));
	node["barrierAlpha"] = JsonValue(static_cast<double>(barrierAlpha_));
	node["flashDuration"] = JsonValue(static_cast<double>(flashDuration_));
	WriteVec4(node, "color", color_);
}

void FlightUnitGuard::OnImGui() {
#ifdef _DEBUG
	ImGui::SeparatorText("Passive (reduce)");
	DragLevels("Reduce##Guard", passiveReduce_, 0.01f, 0.0f, 1.0f);
	DragLevels("Passive Cooldown##Guard", passiveCooldown_, 0.1f, 0.0f, 60.0f);
	ImGui::Text("Passive ready in %.1f s", (std::max)(0.0f, passiveTimer_));
	ImGui::SeparatorText("RB (barrier)");
	DragLevels("HP Ratio##Guard", barrierHpRatio_, 0.01f, 0.05f, 5.0f);
	DragLevels("Duration##Guard", barrierDuration_, 0.05f, 0.1f, 30.0f);
	DragLevels("Cooldown##Guard", cooldown_, 0.1f, 0.0f, 120.0f);
	DragLevels("Max HP Scale##Guard", maxHpScale_, 0.01f, 0.5f, 3.0f);
	ImGui::DragFloat("Radius##Guard", &barrierRadius_, 0.05f, 0.3f, 10.0f, "%.2f");
	ImGui::DragFloat("Contact Interval##Guard", &contactHitInterval_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::SliderFloat("Alpha##Guard", &barrierAlpha_, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Flash##Guard", &flashDuration_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::ColorEdit4("Color##Guard", &color_.x);
	if (active_) ImGui::Text("Durability %.0f / %.0f", durability_, durabilityMax_);
#endif
}
