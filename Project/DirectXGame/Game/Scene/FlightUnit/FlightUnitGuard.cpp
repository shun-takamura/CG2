#include "FlightUnitGuard.h"

#include "Components/Gameplay.h"
#include "Effect/EffectPrimitiveRenderer.h"
#include "Primitive/PrimitivePipeline.h"
#include "Score/DifficultyProfile.h"
#include "Json/JsonValue.h"
#include "MathUtility.h"
#include "Object3DInstance.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kDefenseDir = "Resources/Models/Weapons/FlightUnit/Defense/Export";
	// パーツの順は FlightUnitGuard::kPartCount の説明と同じ
	constexpr const char* kPartNames[] = { "DefenseMain", "DefenseFrontRight", "DefenseBackRight", "DefenseFrontLeft", "DefenseBackLeft" };

	Vector3 Normalized(const Vector3& v, const Vector3& fallback) {
		const float len = std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
		if (len < 1e-5f) return fallback;
		return { v.x / len, v.y / len, v.z / len };
	}
	Vector3 CrossV(const Vector3& a, const Vector3& b) {
		return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
	}
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
	// X の面で鏡に映した置き方（Rx·Ry·Rz の順なので Y・Z の回転を反転する）
	Transform Mirrored(const Transform& t) {
		return { t.scale, { t.rotate.x, -t.rotate.y, -t.rotate.z }, { -t.translate.x, t.translate.y, t.translate.z } };
	}
	Transform LerpTransform(const Transform& a, const Transform& b, float k) {
		return { Lerp(a.scale, b.scale, k), Lerp(a.rotate, b.rotate, k), Lerp(a.translate, b.translate, k) };
	}
}

FlightUnitGuard::FlightUnitGuard() {
	// 初期値（ImGui で合わせて FlightUnit.json に保存する）。
	// パーツはユニットのローカル（+Z が表、上が +Y、自機から見て右が -X）。モデルは展開図の並びで回転なしにそろえてある
	const Vector3 one{ 1.0f, 1.0f, 1.0f };
	openParts_[0] = { one, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	openParts_[1] = { one, { 0.0f, 0.0f, 0.0f }, { -0.216f, 0.243f, -0.03f } };
	openParts_[2] = { one, { 0.0f, 0.0f, 0.0f }, { -0.253f, -0.138f, -0.03f } };
	// 閉じた形：横のパーツを縦に起こして Main の裏へ寄せる
	closedParts_[0] = openParts_[0];
	closedParts_[1] = { one, { 0.0f, 0.0f, 0.72f }, { -0.07f, 0.2f, -0.05f } };
	closedParts_[2] = { one, { 0.0f, 0.0f, 0.49f }, { -0.07f, -0.2f, -0.05f } };
	// 背中：自機の左（バックパックのローカルで +X）・右の肩の後ろ。上を外へ倒して V 字に（右の初期値は左の鏡写し）
	stowMounts_[0] = { { 1.6f, 1.6f, 1.6f }, { 0.0f, 0.0f, 0.35f }, { 0.5f, 0.5f, 0.1f } };
	stowMounts_[1] = Mirrored(stowMounts_[0]);
}

FlightUnitGuard::~FlightUnitGuard() = default;

void FlightUnitGuard::InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) {
	for (Unit& u : units_) {
		for (int p = 0; p < kPartCount; ++p) {
			auto& m = u.parts[p];
			m = std::make_unique<Object3DInstance>();
			m->Initialize(object3DManager, dxCore, kDefenseDir, std::string(kPartNames[p]) + ".mesh", kPartNames[p]);
			m->SetCamera(camera_);
		}
	}
}

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
	// 戻る途中に張り直したら、今の開き具合から出ていく
	deployTimer_ = ReleaseRatio() * deployTime_;
	returning_ = false;
	active_ = true;
	timer_ = 0.0f;
	contactTimer_ = 0.0f;
	hitFlash_ = 1.0f;
	return true;
}

void FlightUnitGuard::Cancel(bool restoreTime) {
	(void)restoreTime;
	if (active_) {
		// 閉じながら背中へ戻る（出ていく途中なら、その開き具合から）
		returnTimer_ = (1.0f - ReleaseRatio()) * returnTime_;
		returning_ = true;
	}
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

float FlightUnitGuard::ReleaseRatio() const {
	if (active_) return deployTime_ > 1e-4f ? (std::min)(deployTimer_ / deployTime_, 1.0f) : 1.0f;
	if (returning_) return returnTime_ > 1e-4f ? std::clamp(1.0f - returnTimer_ / returnTime_, 0.0f, 1.0f) : 0.0f;
	return 0.0f;
}

Vector3 FlightUnitGuard::OrbitPosition(int unit) const {
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	const Vector3 anchor = host_ ? host_->GetFlightUnitAnchor() : Vector3{};
	const float a = orbitAngle_ + kPi * static_cast<float>(unit);
	const float c = std::cos(a) * orbitRadius_;
	const float s = std::sin(a) * orbitRadius_;
	const float ct = std::cos(orbitTilt_);
	const float st = std::sin(orbitTilt_);
	// 自機のまわりの水平な輪（右・前の面）を、奥が上がるように傾ける
	return {
		anchor.x + up.x * orbitHeight_ + right.x * c + (forward.x * ct + up.x * st) * s,
		anchor.y + up.y * orbitHeight_ + right.y * c + (forward.y * ct + up.y * st) * s,
		anchor.z + up.z * orbitHeight_ + right.z * c + (forward.z * ct + up.z * st) * s,
	};
}

void FlightUnitGuard::UpdateModels(const Matrix4x4& backpackWorld) {
	if (!host_) return;
	Vector3 right, up, forward;
	GetCameraBasis(right, up, forward);
	float k = preview_ ? std::clamp(previewRatio_, 0.0f, 1.0f) : ReleaseRatio();
	k = k * k * (3.0f - 2.0f * k);
	const Vector3 anchor = host_->GetFlightUnitAnchor();
	const Vector3 center{ anchor.x + up.x * orbitHeight_, anchor.y + up.y * orbitHeight_, anchor.z + up.z * orbitHeight_ };

	for (int u = 0; u < kUnitCount; ++u) {
		Unit& unit = units_[u];
		const Matrix4x4 stow = Multiply(MakeAffineMatrix(stowMounts_[u]), backpackWorld);
		Matrix4x4 unitWorld = stow;
		if (k > 0.0f) {
			// 背中の形と、表を外へ向けて回る形の間
			const Vector3 orbit = OrbitPosition(u);
			const Vector3 outward = Normalized({ orbit.x - center.x, orbit.y - center.y, orbit.z - center.z }, forward);
			const Vector3 stowZ = Row(stow, 2);
			const Vector3 pos = Lerp(Row(stow, 3), orbit, k);
			const Vector3 dir = Normalized(Lerp(Normalized(stowZ, forward), outward, k), outward);
			const Vector3 upHint = Normalized(Lerp(Normalized(Row(stow, 1), up), up, k), up);
			const float scale = Length(stowZ) + (orbitScale_ - Length(stowZ)) * k;
			unitWorld = MakeLookWorld(pos, dir, upHint, scale);
		}
		for (int p = 0; p < kPartCount; ++p) {
			const int src = p < kTunedParts ? p : p - 2;   // 左は右（1・2）の鏡写し
			Transform local = LerpTransform(closedParts_[src], openParts_[src], k);
			if (p >= kTunedParts) local = Mirrored(local);
			const Matrix4x4 w = Multiply(MakeAffineMatrix(local), unitWorld);
			if (auto& m = unit.parts[p]) {
				m->SetWorldMatrixOverride(w);
				m->SetTranslate(Row(w, 3));
				m->Update();
			}
		}
		unit.placed = true;
	}
}

void FlightUnitGuard::DrawModels(DirectXCore* dxCore) {
	for (const Unit& u : units_) {
		if (!u.placed) continue;
		for (const auto& m : u.parts) {
			if (m) m->Draw(dxCore);
		}
	}
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
	if (active_) deployTimer_ += dt;
	if (returning_) {
		returnTimer_ += dt;
		if (returnTimer_ >= returnTime_) returning_ = false;
	}
	if (active_ || returning_) orbitAngle_ += orbitSpin_ * dt;
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
	const JsonValue& stows = node["stowMounts"];
	if (stows.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kUnitCount) && i < stows.Size(); ++i) ReadTransform(stows[i], stowMounts_[i]);
	}
	const JsonValue& closed = node["closedParts"];
	if (closed.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kTunedParts) && i < closed.Size(); ++i) ReadTransform(closed[i], closedParts_[i]);
	}
	const JsonValue& open = node["openParts"];
	if (open.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kTunedParts) && i < open.Size(); ++i) ReadTransform(open[i], openParts_[i]);
	}
	orbitRadius_ = static_cast<float>(node["orbitRadius"].AsDouble(orbitRadius_));
	orbitHeight_ = static_cast<float>(node["orbitHeight"].AsDouble(orbitHeight_));
	orbitTilt_ = static_cast<float>(node["orbitTilt"].AsDouble(orbitTilt_));
	orbitSpin_ = static_cast<float>(node["orbitSpin"].AsDouble(orbitSpin_));
	orbitScale_ = static_cast<float>(node["orbitScale"].AsDouble(orbitScale_));
	deployTime_ = static_cast<float>(node["deployTime"].AsDouble(deployTime_));
	returnTime_ = static_cast<float>(node["returnTime"].AsDouble(returnTime_));
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
	JsonValue stows = JsonValue::MakeArray();
	for (const Transform& t : stowMounts_) stows.Push(WriteTransform(t));
	node["stowMounts"] = std::move(stows);
	JsonValue closed = JsonValue::MakeArray();
	JsonValue open = JsonValue::MakeArray();
	for (int i = 0; i < kTunedParts; ++i) {
		closed.Push(WriteTransform(closedParts_[i]));
		open.Push(WriteTransform(openParts_[i]));
	}
	node["closedParts"] = std::move(closed);
	node["openParts"] = std::move(open);
	node["orbitRadius"] = JsonValue(static_cast<double>(orbitRadius_));
	node["orbitHeight"] = JsonValue(static_cast<double>(orbitHeight_));
	node["orbitTilt"] = JsonValue(static_cast<double>(orbitTilt_));
	node["orbitSpin"] = JsonValue(static_cast<double>(orbitSpin_));
	node["orbitScale"] = JsonValue(static_cast<double>(orbitScale_));
	node["deployTime"] = JsonValue(static_cast<double>(deployTime_));
	node["returnTime"] = JsonValue(static_cast<double>(returnTime_));
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

	// モデル：防御系を付けているとゲームの中でそのまま見える。背中は 2 基別々、パーツの左は右の鏡写し
	ImGui::SeparatorText("Models");
	ImGui::Checkbox("Preview##GuardModel", &preview_);
	ImGui::SameLine();
	ImGui::SliderFloat("Open##GuardPreview", &previewRatio_, 0.0f, 1.0f, "%.2f");
	ImGui::TextDisabled("0 = stowed on back, 1 = open & orbiting");
	if (ImGui::TreeNode("Stowed (backpack local)##Guard")) {
		ImGui::SeparatorText("Left unit");
		DragTransform("GuardStowL", stowMounts_[0]);
		ImGui::SeparatorText("Right unit");
		DragTransform("GuardStowR", stowMounts_[1]);
		if (ImGui::Button("Right = mirror of Left##Guard")) stowMounts_[1] = Mirrored(stowMounts_[0]);
		ImGui::TreePop();
	}
	const char* partNames[kTunedParts] = { "Main", "Front Right", "Back Right" };
	if (ImGui::TreeNode("Closed parts (unit local)##Guard")) {
		for (int i = 0; i < kTunedParts; ++i) {
			ImGui::SeparatorText(partNames[i]);
			ImGui::PushID(i);
			DragTransform("GuardClosed", closedParts_[i]);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Open parts (unit local)##Guard")) {
		for (int i = 0; i < kTunedParts; ++i) {
			ImGui::SeparatorText(partNames[i]);
			ImGui::PushID(i);
			DragTransform("GuardOpen", openParts_[i]);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Orbit##Guard")) {
		ImGui::DragFloat("Radius##GuardOrbit", &orbitRadius_, 0.01f, 0.1f, 10.0f, "%.2f");
		ImGui::DragFloat("Height##GuardOrbit", &orbitHeight_, 0.01f, -5.0f, 5.0f, "%.2f");
		float tiltDeg = RadToDeg(orbitTilt_);
		if (ImGui::DragFloat("Tilt (deg)##GuardOrbit", &tiltDeg, 0.5f, -90.0f, 90.0f, "%.1f")) orbitTilt_ = DegToRad(tiltDeg);
		ImGui::DragFloat("Spin##GuardOrbit", &orbitSpin_, 0.01f, -10.0f, 10.0f, "%.2f");
		ImGui::DragFloat("Scale##GuardOrbit", &orbitScale_, 0.01f, 0.05f, 5.0f, "%.2f");
		ImGui::DragFloat("Deploy##GuardOrbit", &deployTime_, 0.01f, 0.0f, 2.0f, "%.2f");
		ImGui::DragFloat("Return##GuardOrbit", &returnTime_, 0.01f, 0.0f, 2.0f, "%.2f");
		ImGui::TreePop();
	}
#endif
}
