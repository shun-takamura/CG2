#include "FlightUnitAssault.h"

#include "IImGuiEditable.h"
#include "MathUtility.h"
#include "Object3DInstance.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kFunnelDir = "Resources/Models/Weapons/FlightUnit/Attack/Export";
	constexpr const char* kFunnelLargeFile = "FunnelLarge.mesh";
	constexpr const char* kFunnelSmallFile = "FunnelSmall.mesh";

	Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < 1e-5f) return fallback;
		return { v.x / len, v.y / len, v.z / len };
	}
	Vector3 CrossV(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
	float DotV(const Vector3& a, const Vector3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
	Vector3 Row(const Matrix4x4& m, int r) { return { m.m[r][0], m.m[r][1], m.m[r][2] }; }
	// 前（+Z）を forward、上を upHint に寄せた行列（行＝右・上・前・位置）
	Matrix4x4 MakeLookWorld(const Vector3& pos, const Vector3& forward, const Vector3& upHint, float scale) {
		const Vector3 f = Normalized(forward, { 0.0f, 0.0f, 1.0f });
		const Vector3 r = Normalized(CrossV(upHint, f), { 1.0f, 0.0f, 0.0f });
		const Vector3 u = CrossV(f, r);
		Matrix4x4 m = MakeIdentity4x4();
		m.m[0][0] = r.x * scale; m.m[0][1] = r.y * scale; m.m[0][2] = r.z * scale;
		m.m[1][0] = u.x * scale; m.m[1][1] = u.y * scale; m.m[1][2] = u.z * scale;
		m.m[2][0] = f.x * scale; m.m[2][1] = f.y * scale; m.m[2][2] = f.z * scale;
		m.m[3][0] = pos.x; m.m[3][1] = pos.y; m.m[3][2] = pos.z;
		return m;
	}
}

FlightUnitAssault::FlightUnitAssault() {
	// 付ける所の初期値（バックパックのローカル。バックパックは装甲の面＝+Z を後ろへ向けて背負うので、前は -Z。
	// ファンネルの前（+Z）を自機の前へ向けるため Y を 180°）。ImGui で合わせて FlightUnit.json に保存する
	const Vector3 front{ 0.0f, kPi, 0.0f };
	funnels_[0].mount = { { 0.8f, 0.8f, 0.8f }, front, { -0.2f, 0.6f, 0.0f } };
	funnels_[1].mount = { { 0.8f, 0.8f, 0.8f }, front, { 0.2f, 0.6f, 0.0f } };
	funnels_[2].mount = { { 0.6f, 0.6f, 0.6f }, front, { -0.4f, -0.3f, 0.0f } };
	funnels_[3].mount = { { 0.6f, 0.6f, 0.6f }, front, { 0.4f, -0.3f, 0.0f } };
}

FlightUnitAssault::~FlightUnitAssault() = default;

void FlightUnitAssault::Initialize(IFlightUnitHost* host, Camera* camera) {
	FlightUnitSkill::Initialize(host, camera);
}

void FlightUnitAssault::InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) {
	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const bool large = i < kLargeCount;
		f.model = std::make_unique<Object3DInstance>();
		f.model->Initialize(object3DManager, dxCore, kFunnelDir, large ? kFunnelLargeFile : kFunnelSmallFile,
			large ? "FunnelLarge" : "FunnelSmall");
		f.model->SetCamera(camera_);
	}
}

bool FlightUnitAssault::Activate() {
	if (!host_) return false;
	// 戻る途中に出し直したら、今の離れ具合から出ていく
	const float k = ReleaseRatio();
	active_ = true;
	returning_ = false;
	timer_ = k * funnelDeploy_;
	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		if (k <= 0.0f) f.aimDir = (i < kLargeCount) ? f.turretDir : f.mountForward;
		// 撃つ間隔をずらして、一斉ではなく順に撃たせる
		f.fireTimer = funnelDeploy_ + funnelFireInterval_ * static_cast<float>(i) / static_cast<float>(kFunnelCount);
	}
	return true;
}

void FlightUnitAssault::Cancel(bool restoreTime) {
	(void)restoreTime;
	if (active_) {
		// 輪からバックパックへ戻る（出ていく途中なら、その離れ具合から）
		returnTimer_ = (1.0f - ReleaseRatio()) * funnelReturn_;
		returning_ = true;
	}
	active_ = false;
	timer_ = 0.0f;
}

float FlightUnitAssault::GetActiveRatio() const {
	const float d = funnelDuration_[LevelIndex()];
	return d > 1e-4f ? std::clamp(1.0f - timer_ / d, 0.0f, 1.0f) : 0.0f;
}

float FlightUnitAssault::ReleaseRatio() const {
	if (active_) return funnelDeploy_ > 1e-4f ? (std::min)(timer_ / funnelDeploy_, 1.0f) : 1.0f;
	if (returning_) return funnelReturn_ > 1e-4f ? std::clamp(1.0f - returnTimer_ / funnelReturn_, 0.0f, 1.0f) : 0.0f;
	return 0.0f;
}

Vector3 FlightUnitAssault::MuzzlePosition(const Funnel& f) const {
	const Vector3 p = Row(f.world, 3);
	const Vector3 z = Row(f.world, 2);   // 大きさ込みの +Z
	return { p.x + z.x * muzzleZ_, p.y + z.y * muzzleZ_, p.z + z.z * muzzleZ_ };
}

void FlightUnitAssault::Update(float dt, bool canAct) {
	if (!host_) return;
	UpdateFunnels(dt, canAct);
	UpdatePassive(dt, canAct);
}

void FlightUnitAssault::UpdatePassive(float dt, bool canAct) {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 aim = host_->GetReticleAimPoint();

	// ファンネル大の砲身を、付いたままレティクルへ向ける（後ろ寄りの狙いは追わない）
	const float turn = 1.0f - std::exp(-dt * turretTurnSpeed_);
	for (int i = 0; i < kLargeCount; ++i) {
		Funnel& f = funnels_[i];
		Vector3 desired = f.mountForward;
		if (canAct && f.hasWorld) {
			const Vector3 p = Row(f.world, 3);
			const Vector3 d = Normalized({ aim.x - p.x, aim.y - p.y, aim.z - p.z }, f.mountForward);
			if (DotV(d, f.mountForward) > 0.3f) desired = d;
		}
		f.turretDir = Normalized(Lerp(f.turretDir, desired, turn), f.mountForward);
	}

	// ファンネルが離れている間（戻るまで）は撃たない
	if (!canAct || active_ || returning_) return;
	passiveTimer_ -= dt;
	if (passiveTimer_ > 0.0f) return;
	passiveTimer_ = passiveInterval_[LevelIndex()];

	const Funnel& f = funnels_[passiveNext_];
	passiveNext_ = (passiveNext_ + 1) % kLargeCount;
	const Vector3 from = f.hasWorld ? MuzzlePosition(f) : host_->GetFlightUnitAnchor();
	IImGuiEditable* target = host_->GetReticleTarget();
	const Vector3 dir = Normalized({ aim.x - from.x, aim.y - from.y, aim.z - from.z }, forward);
	host_->FireSupportBullet(from, dir, target, passiveDamageScale_[LevelIndex()]);
}

Vector3 FlightUnitAssault::FunnelHome(int i) const {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const float a = spinAngle_ + 6.2831853f * static_cast<float>(i) / static_cast<float>(kFunnelCount);
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
	if (returning_) {
		spinAngle_ += funnelSpin_ * dt;
		returnTimer_ += dt;
		if (returnTimer_ >= funnelReturn_) returning_ = false;
		return;
	}
	if (!active_) return;
	timer_ += dt;
	if (timer_ >= funnelDuration_[LevelIndex()]) {
		Cancel(false);
		return;
	}
	spinAngle_ += funnelSpin_ * dt;

	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	screenEnemies_.clear();
	if (canAct) host_->CollectOnScreenEnemies(screenEnemies_);

	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const Vector3 pos = f.hasWorld ? Row(f.world, 3) : FunnelHome(i);

		IImGuiEditable* target = canAct ? PickFunnelTarget(i) : nullptr;
		Vector3 desired = forward;
		if (target) {
			if (const Vector3* tp = target->GetEditableTranslate()) {
				desired = Normalized({ tp->x - pos.x, tp->y - pos.y, tp->z - pos.z }, forward);
			}
		}
		// 向きはすっと寄せる
		const float turn = 1.0f - std::exp(-dt * 12.0f);
		f.aimDir = Normalized(Lerp(f.aimDir, desired, turn), forward);

		f.fireTimer -= dt;
		if (f.fireTimer <= 0.0f) {
			f.fireTimer = funnelFireInterval_;
			if (target) host_->FireSupportBullet(f.hasWorld ? MuzzlePosition(f) : pos, desired, target, funnelDamageScale_[LevelIndex()]);
		}
	}
}

void FlightUnitAssault::UpdateModels(const Matrix4x4& backpackWorld) {
	if (!host_) return;
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	float k = ReleaseRatio();
	k = k * k * (3.0f - 2.0f * k);

	for (int i = 0; i < kFunnelCount; ++i) {
		Funnel& f = funnels_[i];
		const Matrix4x4 mount = Multiply(MakeAffineMatrix(f.mount), backpackWorld);
		const Vector3 mountZ = Row(mount, 2);
		const float scale = Length(mountZ);   // バックパックと付ける所の大きさを掛けたもの
		f.mountForward = Normalized(mountZ, forward);
		if (!f.hasWorld) f.turretDir = f.mountForward;

		if (k <= 0.0f && i >= kLargeCount) {
			f.world = mount;
		} else {
			// 付いている形（大は砲身をレティクルへ）と輪を回る形の間
			const Vector3 restDir = (i < kLargeCount) ? f.turretDir : f.mountForward;
			const Vector3 mountUp = Normalized(Row(mount, 1), up);
			const Vector3 pos = Lerp(Row(mount, 3), FunnelHome(i), k);
			const Vector3 dir = Normalized(Lerp(restDir, f.aimDir, k), f.mountForward);
			const Vector3 upHint = Normalized(Lerp(mountUp, up, k), up);
			f.world = MakeLookWorld(pos, dir, upHint, scale);
		}
		f.hasWorld = true;
		if (f.model) {
			f.model->SetWorldMatrixOverride(f.world);
			f.model->SetTranslate(Row(f.world, 3));
			f.model->Update();
		}
	}
}

void FlightUnitAssault::DrawModels(DirectXCore* dxCore) {
	for (const Funnel& f : funnels_) {
		if (f.model && f.hasWorld) f.model->Draw(dxCore);
	}
}

void FlightUnitAssault::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "passiveInterval", passiveInterval_);
	ReadLevels(node, "passiveDamageScale", passiveDamageScale_);
	ReadLevels(node, "funnelDuration", funnelDuration_);
	ReadLevels(node, "funnelDamageScale", funnelDamageScale_);
	ReadLevels(node, "cooldown", cooldown_);
	ReadLevels(node, "attackScale", attackScale_);
	funnelFireInterval_ = static_cast<float>(node["funnelFireInterval"].AsDouble(funnelFireInterval_));
	funnelRadius_ = static_cast<float>(node["funnelRadius"].AsDouble(funnelRadius_));
	funnelSpin_ = static_cast<float>(node["funnelSpin"].AsDouble(funnelSpin_));
	funnelDeploy_ = static_cast<float>(node["funnelDeploy"].AsDouble(funnelDeploy_));
	funnelReturn_ = static_cast<float>(node["funnelReturn"].AsDouble(funnelReturn_));
	turretTurnSpeed_ = static_cast<float>(node["turretTurnSpeed"].AsDouble(turretTurnSpeed_));
	muzzleZ_ = static_cast<float>(node["muzzleZ"].AsDouble(muzzleZ_));
	const JsonValue& mounts = node["mounts"];
	if (mounts.IsArray()) {
		for (size_t i = 0; i < funnels_.size() && i < mounts.Size(); ++i) ReadTransform(mounts[i], funnels_[i].mount);
	}
	ReadVec4(node, "color", color_);
}

void FlightUnitAssault::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "passiveInterval", passiveInterval_);
	WriteLevels(node, "passiveDamageScale", passiveDamageScale_);
	WriteLevels(node, "funnelDuration", funnelDuration_);
	WriteLevels(node, "funnelDamageScale", funnelDamageScale_);
	WriteLevels(node, "cooldown", cooldown_);
	WriteLevels(node, "attackScale", attackScale_);
	node["funnelFireInterval"] = JsonValue(static_cast<double>(funnelFireInterval_));
	node["funnelRadius"] = JsonValue(static_cast<double>(funnelRadius_));
	node["funnelSpin"] = JsonValue(static_cast<double>(funnelSpin_));
	node["funnelDeploy"] = JsonValue(static_cast<double>(funnelDeploy_));
	node["funnelReturn"] = JsonValue(static_cast<double>(funnelReturn_));
	node["turretTurnSpeed"] = JsonValue(static_cast<double>(turretTurnSpeed_));
	node["muzzleZ"] = JsonValue(static_cast<double>(muzzleZ_));
	JsonValue mounts = JsonValue::MakeArray();
	for (const Funnel& f : funnels_) mounts.Push(WriteTransform(f.mount));
	node["mounts"] = std::move(mounts);
	WriteVec4(node, "color", color_);
}

void FlightUnitAssault::OnImGui() {
#ifdef _DEBUG
	ImGui::SeparatorText("Passive (support fire)");
	DragLevels("Interval##Assault", passiveInterval_, 0.01f, 0.05f, 10.0f);
	DragLevels("Damage Scale##AssaultPassive", passiveDamageScale_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Turret Turn##Assault", &turretTurnSpeed_, 0.1f, 0.0f, 50.0f, "%.1f");
	ImGui::DragFloat("Muzzle Z##Assault", &muzzleZ_, 0.01f, -2.0f, 2.0f, "%.2f");
	ImGui::SeparatorText("RB (funnels)");
	DragLevels("Duration##Assault", funnelDuration_, 0.05f, 0.1f, 30.0f);
	DragLevels("Damage Scale##AssaultFunnel", funnelDamageScale_, 0.01f, 0.0f, 5.0f);
	DragLevels("Cooldown##Assault", cooldown_, 0.1f, 0.0f, 120.0f);
	DragLevels("Attack Scale##Assault", attackScale_, 0.01f, 0.5f, 3.0f);
	ImGui::DragFloat("Fire Interval##Assault", &funnelFireInterval_, 0.01f, 0.05f, 5.0f, "%.2f");
	ImGui::DragFloat("Radius##Assault", &funnelRadius_, 0.05f, 0.1f, 10.0f, "%.2f");
	ImGui::DragFloat("Spin##Assault", &funnelSpin_, 0.05f, -10.0f, 10.0f, "%.2f");
	ImGui::DragFloat("Deploy##Assault", &funnelDeploy_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Return##Assault", &funnelReturn_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::ColorEdit4("Color##Assault", &color_.x);
	// 付いている所（バックパックのローカル）。攻撃系を付けているとゲームの中でそのまま見える
	if (ImGui::TreeNode("Mounts (backpack local)##Assault")) {
		const char* names[kFunnelCount] = { "Large A", "Large B", "Small A", "Small B" };
		for (int i = 0; i < kFunnelCount; ++i) {
			ImGui::SeparatorText(names[i]);
			DragTransform(names[i], funnels_[i].mount);
		}
		ImGui::TreePop();
	}
#endif
}
