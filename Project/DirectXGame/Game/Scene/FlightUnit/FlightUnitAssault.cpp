#include "FlightUnitAssault.h"

#include "IImGuiEditable.h"
#include "Effect/EffectPrimitiveRenderer.h"
#include "Primitive/PrimitivePipeline.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < 1e-5f) return fallback;
		return { v.x / len, v.y / len, v.z / len };
	}
	// +Z を dir へ向ける回転（Rx·Ry·Rz の Euler。ロールは 0）
	Vector3 DirToEuler(const Vector3& d) {
		const float yaw = std::atan2(d.x, d.z);
		const float pitch = std::atan2(-d.y, std::sqrt(d.x * d.x + d.z * d.z));
		return { pitch, yaw, 0.0f };
	}
}

FlightUnitAssault::FlightUnitAssault() = default;
FlightUnitAssault::~FlightUnitAssault() = default;

void FlightUnitAssault::Initialize(IFlightUnitHost* host, Camera* camera) {
	FlightUnitSkill::Initialize(host, camera);
	for (auto& f : funnels_) {
		f.vis = std::make_unique<EffectPrimitiveRenderer>();
		f.vis->Initialize(1 /*Box*/, "Resources/Textures/white1x1.dds");
		f.vis->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
		f.vis->SetDepthWrite(false);
		f.vis->SetColor(color_);
		f.vis->SetScale(funnelScale_);
	}
}

bool FlightUnitAssault::Activate() {
	if (!host_) return false;
	active_ = true;
	timer_ = 0.0f;
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const int count = std::clamp(funnelCount_, 1, kMaxFunnels);
	for (int i = 0; i < count; ++i) {
		funnels_[i].pos = anchor;
		// 撃つ間隔をずらして、一斉ではなく順に撃たせる
		funnels_[i].fireTimer = funnelDeploy_ + funnelFireInterval_ * static_cast<float>(i) / static_cast<float>(count);
	}
	return true;
}

void FlightUnitAssault::Cancel(bool restoreTime) {
	(void)restoreTime;
	active_ = false;
	timer_ = 0.0f;
}

float FlightUnitAssault::GetActiveRatio() const {
	const float d = funnelDuration_[LevelIndex()];
	return d > 1e-4f ? std::clamp(1.0f - timer_ / d, 0.0f, 1.0f) : 0.0f;
}

void FlightUnitAssault::Update(float dt, bool canAct) {
	if (!host_) return;
	if (canAct) UpdatePassive(dt);
	UpdateFunnels(dt, canAct);
}

void FlightUnitAssault::UpdatePassive(float dt) {
	passiveTimer_ -= dt;
	if (passiveTimer_ > 0.0f) return;
	passiveTimer_ = passiveInterval_[LevelIndex()];

	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const float side = passiveRight_ ? 1.0f : -1.0f;
	passiveRight_ = !passiveRight_;
	const Vector3 from = {
		anchor.x + right.x * passiveMuzzle_.x * side + up.x * passiveMuzzle_.y,
		anchor.y + right.y * passiveMuzzle_.x * side + up.y * passiveMuzzle_.y,
		anchor.z + right.z * passiveMuzzle_.x * side + up.z * passiveMuzzle_.y,
	};
	IImGuiEditable* target = host_->GetReticleTarget();
	const Vector3 aim = host_->GetReticleAimPoint();
	const Vector3 dir = Normalized({ aim.x - from.x, aim.y - from.y, aim.z - from.z }, forward);
	host_->FireSupportBullet(from, dir, target, passiveDamageScale_[LevelIndex()]);
}

Vector3 FlightUnitAssault::FunnelHome(int i, int count) const {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const float a = spinAngle_ + 6.2831853f * static_cast<float>(i) / static_cast<float>((std::max)(count, 1));
	const float c = std::cos(a) * funnelRadius_;
	const float s = std::sin(a) * funnelRadius_ * 0.6f;   // 縦に少しつぶす（自機の上下を隠さない）
	return {
		anchor.x + right.x * c + up.x * s - forward.x * 0.3f,
		anchor.y + right.y * c + up.y * s - forward.y * 0.3f,
		anchor.z + right.z * c + up.z * s - forward.z * 0.3f,
	};
}

IImGuiEditable* FlightUnitAssault::PickFunnelTarget(int i) {
	// レティクルが重なっている敵を優先
	if (IImGuiEditable* t = host_->GetReticleTarget(); t && host_->IsEnemyAlive(t)) return t;
	if (screenEnemies_.empty()) return nullptr;
	// 重なっていなければ画面上の敵へ散らす（ファンネルごとに違う敵を狙いやすく）
	pickSeed_ = pickSeed_ * 1664525u + 1013904223u;
	const size_t idx = (static_cast<size_t>(i) + (pickSeed_ >> 16)) % screenEnemies_.size();
	return screenEnemies_[idx];
}

void FlightUnitAssault::UpdateFunnels(float dt, bool canAct) {
	if (!active_) return;
	timer_ += dt;
	if (timer_ >= funnelDuration_[LevelIndex()]) {
		Cancel(false);
		return;
	}
	spinAngle_ += funnelSpin_ * dt;

	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const int count = std::clamp(funnelCount_, 1, kMaxFunnels);
	// 自機から輪へ出ていく（ease-out）
	float k = funnelDeploy_ > 1e-4f ? (std::min)(timer_ / funnelDeploy_, 1.0f) : 1.0f;
	k = 1.0f - (1.0f - k) * (1.0f - k);

	screenEnemies_.clear();
	if (canAct) host_->CollectOnScreenEnemies(screenEnemies_);

	for (int i = 0; i < count; ++i) {
		Funnel& f = funnels_[i];
		const Vector3 home = FunnelHome(i, count);
		f.pos = { anchor.x + (home.x - anchor.x) * k, anchor.y + (home.y - anchor.y) * k, anchor.z + (home.z - anchor.z) * k };

		IImGuiEditable* target = canAct ? PickFunnelTarget(i) : nullptr;
		Vector3 desired = forward;
		if (target) {
			if (const Vector3* tp = target->GetEditableTranslate()) {
				desired = Normalized({ tp->x - f.pos.x, tp->y - f.pos.y, tp->z - f.pos.z }, forward);
			}
		}
		// 向きはすっと寄せる
		const float turn = 1.0f - std::exp(-dt * 12.0f);
		f.aimDir = Normalized({ f.aimDir.x + (desired.x - f.aimDir.x) * turn,
			f.aimDir.y + (desired.y - f.aimDir.y) * turn,
			f.aimDir.z + (desired.z - f.aimDir.z) * turn }, forward);

		f.fireTimer -= dt;
		if (f.fireTimer <= 0.0f) {
			f.fireTimer = funnelFireInterval_;
			if (target) host_->FireSupportBullet(f.pos, desired, target, funnelDamageScale_[LevelIndex()]);
		}

		if (f.vis) {
			f.vis->SetTranslate(f.pos);
			f.vis->SetRotate(DirToEuler(f.aimDir));
			f.vis->SetScale(funnelScale_);
			f.vis->SetColor(color_);
			f.vis->Update(camera_, dt);
		}
	}
}

void FlightUnitAssault::Draw() {
	if (!active_) return;
	const int count = std::clamp(funnelCount_, 1, kMaxFunnels);
	for (int i = 0; i < count; ++i) {
		if (funnels_[i].vis) funnels_[i].vis->Draw();
	}
}

void FlightUnitAssault::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "passiveInterval", passiveInterval_);
	ReadLevels(node, "passiveDamageScale", passiveDamageScale_);
	ReadLevels(node, "funnelDuration", funnelDuration_);
	ReadLevels(node, "funnelDamageScale", funnelDamageScale_);
	ReadLevels(node, "cooldown", cooldown_);
	ReadLevels(node, "attackScale", attackScale_);
	funnelCount_ = static_cast<int>(node["funnelCount"].AsDouble(funnelCount_));
	funnelFireInterval_ = static_cast<float>(node["funnelFireInterval"].AsDouble(funnelFireInterval_));
	funnelRadius_ = static_cast<float>(node["funnelRadius"].AsDouble(funnelRadius_));
	funnelSpin_ = static_cast<float>(node["funnelSpin"].AsDouble(funnelSpin_));
	funnelDeploy_ = static_cast<float>(node["funnelDeploy"].AsDouble(funnelDeploy_));
	passiveMuzzle_.x = static_cast<float>(node["passiveMuzzleRight"].AsDouble(passiveMuzzle_.x));
	passiveMuzzle_.y = static_cast<float>(node["passiveMuzzleUp"].AsDouble(passiveMuzzle_.y));
	Vector4 s{ funnelScale_.x, funnelScale_.y, funnelScale_.z, 0.0f };
	ReadVec4(node, "funnelScale", s);
	funnelScale_ = { s.x, s.y, s.z };
	ReadVec4(node, "color", color_);
}

void FlightUnitAssault::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "passiveInterval", passiveInterval_);
	WriteLevels(node, "passiveDamageScale", passiveDamageScale_);
	WriteLevels(node, "funnelDuration", funnelDuration_);
	WriteLevels(node, "funnelDamageScale", funnelDamageScale_);
	WriteLevels(node, "cooldown", cooldown_);
	WriteLevels(node, "attackScale", attackScale_);
	node["funnelCount"] = JsonValue(static_cast<double>(funnelCount_));
	node["funnelFireInterval"] = JsonValue(static_cast<double>(funnelFireInterval_));
	node["funnelRadius"] = JsonValue(static_cast<double>(funnelRadius_));
	node["funnelSpin"] = JsonValue(static_cast<double>(funnelSpin_));
	node["funnelDeploy"] = JsonValue(static_cast<double>(funnelDeploy_));
	node["passiveMuzzleRight"] = JsonValue(static_cast<double>(passiveMuzzle_.x));
	node["passiveMuzzleUp"] = JsonValue(static_cast<double>(passiveMuzzle_.y));
	WriteVec4(node, "funnelScale", { funnelScale_.x, funnelScale_.y, funnelScale_.z, 0.0f });
	WriteVec4(node, "color", color_);
}

void FlightUnitAssault::OnImGui() {
#ifdef _DEBUG
	ImGui::SeparatorText("Passive (support fire)");
	DragLevels("Interval##Assault", passiveInterval_, 0.01f, 0.05f, 10.0f);
	DragLevels("Damage Scale##AssaultPassive", passiveDamageScale_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat2("Muzzle (R/U)##Assault", &passiveMuzzle_.x, 0.05f, -5.0f, 5.0f, "%.2f");
	ImGui::SeparatorText("RB (funnels)");
	ImGui::SliderInt("Count##Assault", &funnelCount_, 1, kMaxFunnels);
	DragLevels("Duration##Assault", funnelDuration_, 0.05f, 0.1f, 30.0f);
	DragLevels("Damage Scale##AssaultFunnel", funnelDamageScale_, 0.01f, 0.0f, 5.0f);
	DragLevels("Cooldown##Assault", cooldown_, 0.1f, 0.0f, 120.0f);
	DragLevels("Attack Scale##Assault", attackScale_, 0.01f, 0.5f, 3.0f);
	ImGui::DragFloat("Fire Interval##Assault", &funnelFireInterval_, 0.01f, 0.05f, 5.0f, "%.2f");
	ImGui::DragFloat("Radius##Assault", &funnelRadius_, 0.05f, 0.1f, 10.0f, "%.2f");
	ImGui::DragFloat("Spin##Assault", &funnelSpin_, 0.05f, -10.0f, 10.0f, "%.2f");
	ImGui::DragFloat("Deploy##Assault", &funnelDeploy_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat3("Funnel Scale##Assault", &funnelScale_.x, 0.01f, 0.01f, 3.0f, "%.2f");
	ImGui::ColorEdit4("Color##Assault", &color_.x);
#endif
}
