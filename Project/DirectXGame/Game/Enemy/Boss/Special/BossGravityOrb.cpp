#include "Enemy/Boss/Special/BossGravityOrb.h"
#include "IImGuiEditable.h"
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <cmath>
#include <filesystem>

#ifdef USE_IMGUI
#include "imgui.h"
#endif

namespace {
	// 調整値のファイル
	constexpr const char* kTuningPath = "Resources/Json/Tuning/BossGravityOrb.json";
	// 調整値の JSON の字下げ [スペースの数]
	constexpr int kJsonIndentWidth = 2;
	// 0 とみなす時間 [s]
	constexpr float kNearZeroTime = 1e-4f;
	// 浮いている間の上下の揺れの速さ [rad/s]
	constexpr float kBossBobSpeed = 1.5f;
	// 球の落ち方：等速の割合（残りは加速）。ゆっくり落ちて、最後に少しだけ速くなる
	constexpr float kOrbLinearPart = 0.4f;

	float SmoothStep01(float t) {
		t = std::clamp(t, 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	Vector3 LerpVec(const Vector3& a, const Vector3& b, float t) {
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t };
	}

	// 段階の名前（ImGui）
	constexpr const char* kPhaseNames[] = {
		"Idle", "Rise", "RocksRise", "Crumble", "Drop", "Charge", "Fall", "Impact", "Whiteout", "Aftermath", "Done",
	};

	// 調整値の float と JSON のキー・ImGui の名前の組
	struct FloatField {
		const char* key;
		const char* label;
		float BossGravityOrb::Tuning::* member;
		float speed;
	};
	constexpr FloatField kFloatFields[] = {
		{ "riseTime", "ボスが浮く時間", &BossGravityOrb::Tuning::riseTime, 0.01f },
		{ "rocksRiseTime", "岩が浮く時間", &BossGravityOrb::Tuning::rocksRiseTime, 0.01f },
		{ "crumbleTime", "崩れてから重力が戻るまで", &BossGravityOrb::Tuning::crumbleTime, 0.01f },
		{ "dropTime", "落ちる時間", &BossGravityOrb::Tuning::dropTime, 0.01f },
		{ "chargeTime", "溜める時間", &BossGravityOrb::Tuning::chargeTime, 0.01f },
		{ "fallTime", "球が落ちる時間", &BossGravityOrb::Tuning::fallTime, 0.01f },
		{ "impactDelay", "着地からモノクロまで", &BossGravityOrb::Tuning::impactDelay, 0.01f },
		{ "impactTime", "モノクロから爆発まで", &BossGravityOrb::Tuning::impactTime, 0.01f },
		{ "whiteoutTime", "白く覆われている時間", &BossGravityOrb::Tuning::whiteoutTime, 0.01f },
		{ "aftermathTime", "余韻の時間", &BossGravityOrb::Tuning::aftermathTime, 0.01f },
		{ "bossHoverHeight", "ボスが浮く高さ", &BossGravityOrb::Tuning::bossHoverHeight, 0.1f },
		{ "bossBob", "ボスの上下の揺れ", &BossGravityOrb::Tuning::bossBob, 0.01f },
		{ "orbOffsetY", "球を溜める高さ（ボスから）", &BossGravityOrb::Tuning::orbOffsetY, 0.05f },
		{ "damageRatio", "隠れ損ねたダメージ（最大 HP 比）", &BossGravityOrb::Tuning::damageRatio, 0.01f },
		{ "rockLandShake", "盾の着地の揺れ", &BossGravityOrb::Tuning::rockLandShake, 0.01f },
		{ "releaseShake", "放つ瞬間の揺れ", &BossGravityOrb::Tuning::releaseShake, 0.01f },
		{ "impactShake", "着地の揺れ", &BossGravityOrb::Tuning::impactShake, 0.01f },
		{ "explosionShake", "爆発の揺れ", &BossGravityOrb::Tuning::explosionShake, 0.01f },
		{ "shakeTime", "揺れの長さ", &BossGravityOrb::Tuning::shakeTime, 0.01f },
	};
}

void BossGravityOrb::Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager,
	IBossGravityOrbHost* host) {
	host_ = host;
	LoadTuning();  // 岩のモデル・破片の数を先に読んでから割る
	rocks_.Initialize(dxCore, srvManager, object3DManager);
}

void BossGravityOrb::Finalize() {
	// 終了時は EffectManager・PostEffect がシーンより先に終了している（PostEffect::Finalize 後は
	// monochrome が破棄済みのフィルタを指したまま）ので、Abort で戻さずに手持ちを捨てるだけにする
	rocks_.Clear();
	fx_.Discard();
	phase_ = Phase::Idle;
	phaseTimer_ = 0.0f;
	rocks_.Finalize();
	host_ = nullptr;
}

bool BossGravityOrb::Begin(const Vector3& bossPosition, const Vector3& arenaCenter, float arenaRadius) {
	if (!host_) return false;
	if (IsRunning()) Abort();
	const float groundY = host_->GetGravityOrbGroundY();
	groundY_ = groundY;
	center_ = { arenaCenter.x, groundY, arenaCenter.z };
	bossStart_ = bossPosition;
	bossPosition_ = bossPosition;
	bobTime_ = 0.0f;
	rocks_.Place(center_, groundY, arenaRadius, rng_);
	EnterPhase(Phase::Rise);
	return true;
}

void BossGravityOrb::Abort() {
	const bool wasRunning = IsRunning();
	rocks_.Clear();
	fx_.Abort();
	if (wasRunning && host_) host_->SetGravityOrbSkyDark(false);
	phase_ = Phase::Idle;
	phaseTimer_ = 0.0f;
}

float BossGravityOrb::PhaseDuration(Phase phase) const {
	switch (phase) {
	case Phase::Rise: return tuning_.riseTime;
	case Phase::RocksRise: return tuning_.rocksRiseTime;
	case Phase::Crumble: return tuning_.crumbleTime;
	case Phase::Drop: return tuning_.dropTime;
	case Phase::Charge: return tuning_.chargeTime;
	case Phase::Fall: return tuning_.fallTime;
	case Phase::Impact: return tuning_.impactDelay + tuning_.impactTime;
	case Phase::Whiteout: return tuning_.whiteoutTime;
	case Phase::Aftermath: return tuning_.aftermathTime;
	default: return 0.0f;
	}
}

float BossGravityOrb::GetPhaseProgress() const {
	const float duration = PhaseDuration(phase_);
	if (duration <= kNearZeroTime) return 1.0f;
	return std::clamp(phaseTimer_ / duration, 0.0f, 1.0f);
}

void BossGravityOrb::Shake(float intensity) const {
	if (host_ && intensity > 0.0f) host_->ShakeGravityOrbCamera(intensity, tuning_.shakeTime);
}

void BossGravityOrb::EnterPhase(Phase phase) {
	phase_ = phase;
	phaseTimer_ = 0.0f;
	switch (phase) {
	case Phase::Rise:
		fx_.StartDebris(center_);
		break;
	case Phase::RocksRise:
		rocks_.BeginRise(tuning_.rocksRiseTime);
		break;
	case Phase::Crumble:
		rocks_.Crumble();
		break;
	case Phase::Drop:
		rocks_.Drop();
		break;
	case Phase::Charge:
		orbStart_ = { bossPosition_.x, bossPosition_.y + tuning_.orbOffsetY, bossPosition_.z };
		orbPosition_ = orbStart_;
		fx_.StartCharge(orbStart_);
		break;
	case Phase::Fall:
		orbStart_ = orbPosition_;
		fx_.Release(orbStart_);
		fx_.SetDark(true);
		if (host_) host_->SetGravityOrbSkyDark(true);
		Shake(tuning_.releaseShake);
		break;
	case Phase::Impact:
		// 着地の瞬間はエフェクト（小さな爆発）だけ。モノクロ②・閃光・揺れは impactDelay 後に Update で出す
		orbPosition_ = center_;
		fx_.Land(center_);
		impactBurstDone_ = false;
		break;
	case Phase::Whiteout:
		fx_.Explode(center_);
		fx_.StopDebris();
		JudgePlayer();  // 破片を飛ばす前に、盾が立っている状態で判定する
		rocks_.Sweep(center_);
		Shake(tuning_.explosionShake);
		break;
	case Phase::Aftermath:
		rocks_.CrumbleShield();
		fx_.Aftermath(center_);
		fx_.SetDark(false);
		if (host_) host_->SetGravityOrbSkyDark(false);
		break;
	default:
		break;
	}
}

void BossGravityOrb::UpdateBossPosition() {
	const Vector3 hover{ center_.x, groundY_ + tuning_.bossHoverHeight + std::sin(bobTime_ * kBossBobSpeed) * tuning_.bossBob,
		center_.z };
	switch (phase_) {
	case Phase::Rise:
		bossPosition_ = LerpVec(bossStart_, hover, SmoothStep01(GetPhaseProgress()));
		break;
	case Phase::Aftermath:
		bossPosition_ = LerpVec(hover, { center_.x, bossStart_.y, center_.z }, SmoothStep01(GetPhaseProgress()));
		break;
	case Phase::Idle:
	case Phase::Done:
		break;
	default:
		bossPosition_ = hover;
		break;
	}
}

void BossGravityOrb::UpdateOrb() {
	if (phase_ == Phase::Charge) {
		orbPosition_ = { bossPosition_.x, bossPosition_.y + tuning_.orbOffsetY, bossPosition_.z };
		fx_.SetOrbPosition(orbPosition_);
	} else if (phase_ == Phase::Fall) {
		const float t = GetPhaseProgress();
		const float e = t * kOrbLinearPart + t * t * (1.0f - kOrbLinearPart);
		orbPosition_ = LerpVec(orbStart_, center_, e);
		fx_.SetOrbPosition(orbPosition_);
	}
}

void BossGravityOrb::JudgePlayer() {
	if (!host_) return;
	IImGuiEditable* player = host_->GetGravityOrbPlayer();
	if (!player) return;
	const Vector3* position = player->GetEditableTranslate();
	if (!position) return;
	lastShielded_ = rocks_.IsShielded(center_, *position);
	if (!lastShielded_) host_->OnGravityOrbPlayerHit(tuning_.damageRatio);
}

void BossGravityOrb::Update(float dt) {
	rocks_.Update(dt);
	fx_.Update(dt);
	if (rocks_.ConsumeShieldLanded()) {
		Vector3 shield;
		if (rocks_.GetShieldPosition(shield)) fx_.RockLanded({ shield.x, groundY_, shield.z });
		Shake(tuning_.rockLandShake);
	}
	if (!IsRunning()) return;

	phaseTimer_ += dt;
	bobTime_ += dt;
	UpdateBossPosition();
	UpdateOrb();
	// 着地の小さな爆発の後にモノクロ②・閃光（段階の切り替えより先に見るので、爆発より前に必ず出る）
	if (phase_ == Phase::Impact && !impactBurstDone_ && phaseTimer_ >= tuning_.impactDelay) {
		fx_.ImpactBurst();
		Shake(tuning_.impactShake);
		impactBurstDone_ = true;
	}
	if (phaseTimer_ >= PhaseDuration(phase_)) {
		EnterPhase(static_cast<Phase>(static_cast<int>(phase_) + 1));
		// 入った段階の最初の位置をこのフレームで決める（段階の境目で 1 フレーム止まらないように）
		UpdateBossPosition();
		UpdateOrb();
	}
}

void BossGravityOrb::ResolvePlayerCollision(Vector3& playerPos) const {
	rocks_.ResolvePlayerCollision(playerPos);
}

void BossGravityOrb::Draw(const Camera& camera) {
	rocks_.Draw(camera);
}

void BossGravityOrb::DrawOverlay(float screenW, float screenH) const {
	fx_.DrawOverlay(screenW, screenH);
}

bool BossGravityOrb::ConsumeDebugTrigger() {
	const bool trigger = debugTrigger_;
	debugTrigger_ = false;
	return trigger;
}

void BossGravityOrb::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;
	const JsonValue& timeline = root["timeline"];
	if (timeline.IsObject()) {
		for (const FloatField& field : kFloatFields) {
			tuning_.*field.member = static_cast<float>(timeline[field.key].AsDouble(tuning_.*field.member));
		}
	}
	rocks_.LoadTuning(root["rocks"]);
	fx_.LoadTuning(root["fx"]);
}

void BossGravityOrb::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	JsonValue timeline = JsonValue::MakeObject();
	for (const FloatField& field : kFloatFields) {
		timeline[field.key] = static_cast<double>(tuning_.*field.member);
	}
	root["timeline"] = std::move(timeline);
	JsonValue rocks = JsonValue::MakeObject();
	rocks_.SaveTuning(rocks);
	root["rocks"] = std::move(rocks);
	JsonValue fx = JsonValue::MakeObject();
	fx_.SaveTuning(fx);
	root["fx"] = std::move(fx);
	JsonWriter::WriteFile(kTuningPath, root, { true, kJsonIndentWidth });
}

void BossGravityOrb::OnImGui() {
#ifdef USE_IMGUI
	ImGui::Text("段階: %s  %.2f / %.2f s", kPhaseNames[static_cast<int>(phase_)], phaseTimer_, PhaseDuration(phase_));
	ImGui::Text("最後の判定: %s", lastShielded_ ? "盾に隠れていた" : "当たった（または未判定）");
	if (ImGui::Button("今すぐ使う（ボス戦中）")) debugTrigger_ = true;
	ImGui::SameLine();
	if (ImGui::Button("中断")) Abort();
	ImGui::SameLine();
	if (ImGui::Button("保存")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("読み直す")) {
		LoadTuning();
		rocks_.Rebuild();
	}

	bool changed = false;
	if (ImGui::TreeNode("段取り")) {
		for (const FloatField& field : kFloatFields) {
			changed |= ImGui::DragFloat(field.label, &(tuning_.*field.member), field.speed, 0.0f, 100.0f, "%.2f");
		}
		float total = 0.0f;
		for (int p = static_cast<int>(Phase::Rise); p <= static_cast<int>(Phase::Aftermath); ++p) total += PhaseDuration(static_cast<Phase>(p));
		const float escape = tuning_.crumbleTime + tuning_.dropTime + tuning_.chargeTime + tuning_.fallTime
			+ tuning_.impactDelay + tuning_.impactTime;
		ImGui::Text("全体 %.1f s / 盾が分かってから爆発まで %.1f s", total, escape);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("岩")) {
		rocks_.OnImGui(changed);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("演出")) {
		fx_.OnImGui(changed);
		ImGui::TreePop();
	}
	(void)changed;
#endif
}
