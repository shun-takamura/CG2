#include "FlightUnitPart.h"

#include "FlightUnitAssault.h"
#include "FlightUnitGuard.h"
#include "FlightUnitMobility.h"
#include "Camera.h"
#include "MathUtility.h"
#include "Object3DInstance.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "SoundManager.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <filesystem>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/FlightUnit.json";
	constexpr const char* kBackpackDir = "Resources/Models/Weapons/FlightUnit/Backpack/Export";
	constexpr const char* kBackpackFile = "FlightUnitBackpack.mesh";
}

//====================
// FlightUnitSkill（共通の道具）
//====================

void FlightUnitSkill::GetCameraBasis(Vector3& right, Vector3& up, Vector3& forward) const {
	right = { 1.0f, 0.0f, 0.0f };
	up = { 0.0f, 1.0f, 0.0f };
	forward = { 0.0f, 0.0f, 1.0f };
	if (!camera_) return;
	const Matrix4x4& w = camera_->GetWorldMatrix();
	right = { w.m[0][0], w.m[0][1], w.m[0][2] };
	up = { w.m[1][0], w.m[1][1], w.m[1][2] };
	forward = { w.m[2][0], w.m[2][1], w.m[2][2] };
}

void FlightUnitSkill::ReadLevels(const JsonValue& node, const char* key, float (&v)[3]) {
	const JsonValue& a = node[key];
	if (!a.IsArray()) return;
	for (size_t i = 0; i < 3 && i < a.Size(); ++i) {
		v[i] = static_cast<float>(a[i].AsDouble(v[i]));
	}
}

void FlightUnitSkill::WriteLevels(JsonValue& node, const char* key, const float (&v)[3]) {
	JsonValue a = JsonValue::MakeArray();
	for (float f : v) a.Push(JsonValue(static_cast<double>(f)));
	node[key] = std::move(a);
}

void FlightUnitSkill::DragLevels(const char* label, float (&v)[3], float speed, float minV, float maxV) {
#ifdef _DEBUG
	ImGui::DragFloat3(label, v, speed, minV, maxV, "%.2f");
#else
	(void)label; (void)v; (void)speed; (void)minV; (void)maxV;
#endif
}

void FlightUnitSkill::ReadVec4(const JsonValue& node, const char* key, Vector4& v) {
	const JsonValue& a = node[key];
	if (!a.IsArray() || a.Size() < 4) return;
	v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)),
		static_cast<float>(a[2].AsDouble(v.z)), static_cast<float>(a[3].AsDouble(v.w)) };
}

void FlightUnitSkill::WriteVec4(JsonValue& node, const char* key, const Vector4& v) {
	JsonValue a = JsonValue::MakeArray();
	a.Push(JsonValue(static_cast<double>(v.x)));
	a.Push(JsonValue(static_cast<double>(v.y)));
	a.Push(JsonValue(static_cast<double>(v.z)));
	a.Push(JsonValue(static_cast<double>(v.w)));
	node[key] = std::move(a);
}

void FlightUnitSkill::ReadVec3(const JsonValue& node, const char* key, Vector3& v) {
	const JsonValue& a = node[key];
	if (!a.IsArray() || a.Size() < 3) return;
	v = { static_cast<float>(a[0].AsDouble(v.x)), static_cast<float>(a[1].AsDouble(v.y)), static_cast<float>(a[2].AsDouble(v.z)) };
}

void FlightUnitSkill::WriteVec3(JsonValue& node, const char* key, const Vector3& v) {
	JsonValue a = JsonValue::MakeArray();
	a.Push(JsonValue(static_cast<double>(v.x)));
	a.Push(JsonValue(static_cast<double>(v.y)));
	a.Push(JsonValue(static_cast<double>(v.z)));
	node[key] = std::move(a);
}

void FlightUnitSkill::ReadTransform(const JsonValue& node, Transform& t) {
	if (!node.IsObject()) return;
	ReadVec3(node, "translate", t.translate);
	ReadVec3(node, "rotate", t.rotate);
	ReadVec3(node, "scale", t.scale);
}

JsonValue FlightUnitSkill::WriteTransform(const Transform& t) {
	JsonValue node = JsonValue::MakeObject();
	WriteVec3(node, "translate", t.translate);
	WriteVec3(node, "rotate", t.rotate);
	WriteVec3(node, "scale", t.scale);
	return node;
}

void FlightUnitSkill::DragTransform(const char* id, Transform& t) {
#ifdef _DEBUG
	ImGui::PushID(id);
	ImGui::DragFloat3("Translate", &t.translate.x, 0.005f, -5.0f, 5.0f, "%.3f");
	Vector3 deg{ RadToDeg(t.rotate.x), RadToDeg(t.rotate.y), RadToDeg(t.rotate.z) };
	if (ImGui::DragFloat3("Rotate (deg)", &deg.x, 0.5f, -360.0f, 360.0f, "%.1f")) {
		t.rotate = { DegToRad(deg.x), DegToRad(deg.y), DegToRad(deg.z) };
	}
	float s = t.scale.x;
	if (ImGui::DragFloat("Scale", &s, 0.005f, 0.01f, 10.0f, "%.3f")) t.scale = { s, s, s };
	ImGui::PopID();
#else
	(void)id; (void)t;
#endif
}

//====================
// FlightUnitPart
//====================

FlightUnitPart::FlightUnitPart() {
	skills_[static_cast<size_t>(FlightUnitKind::Assault)] = std::make_unique<FlightUnitAssault>();
	skills_[static_cast<size_t>(FlightUnitKind::Guard)] = std::make_unique<FlightUnitGuard>();
	skills_[static_cast<size_t>(FlightUnitKind::Mobility)] = std::make_unique<FlightUnitMobility>();
	LoadTuning();
}

FlightUnitPart::~FlightUnitPart() = default;

void FlightUnitPart::Initialize(IFlightUnitHost* host, Camera* camera, SpriteManager* spriteManager,
	Object3DManager* object3DManager, DirectXCore* dxCore) {
	host_ = host;
	spriteManager_ = spriteManager;
	for (auto& s : skills_) {
		if (s) {
			s->Initialize(host, camera);
			if (object3DManager && dxCore) s->InitializeModels(object3DManager, dxCore);
		}
	}
	if (object3DManager && dxCore) {
		backpack_ = std::make_unique<Object3DInstance>();
		backpack_->Initialize(object3DManager, dxCore, kBackpackDir, kBackpackFile, "FlightUnitBackpack");
		backpack_->SetCamera(camera);
	}
	if (spriteManager_) {
		barBg_ = std::make_unique<SpriteInstance>();
		barBg_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "FlightUnitBarBackground");
		barFg_ = std::make_unique<SpriteInstance>();
		barFg_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "FlightUnitBarForeground");
	}
	SyncLoadout();
}

FlightUnitSkill* FlightUnitPart::Current() const {
	return skills_[static_cast<size_t>(kind_)].get();
}

void FlightUnitPart::SyncLoadout() {
	if (FlightUnitSkill* s = Current(); s && s->IsActive()) s->Cancel(true);
	const LoadoutState* loadout = LoadoutState::GetInstance();
	kind_ = loadout->GetFlightUnit();
	for (size_t i = 0; i < skills_.size(); ++i) {
		if (skills_[i]) skills_[i]->SetLevel(loadout->GetFlightUnitLevel(static_cast<FlightUnitKind>(i)));
	}
	cooldownTimer_ = 0.0f;
	wasActive_ = false;
	readyPulse_ = 0.0f;
}

void FlightUnitPart::Update(float dt, bool triggered, bool canAct, bool timeTaken) {
	if (!host_) return;
	FlightUnitSkill* s = Current();
	if (!s) return;
	if (readyPulse_ > 0.0f) readyPulse_ -= dt;

	// RB のスロー中にジャスト回避・必殺技が起きたら、スローはそこで終わる。時間の倍率はそちらが握っているので触らない
	if (timeTaken && s->IsActive() && s->UsesTimeScale()) s->Cancel(false);

	s->Update(dt, canAct);

	// クールタイムは技が終わってから数える
	const bool active = s->IsActive();
	if (wasActive_ && !active) cooldownTimer_ = s->GetCooldown();
	wasActive_ = active;
	if (!active && cooldownTimer_ > 0.0f) {
		cooldownTimer_ -= dt;
		if (cooldownTimer_ <= 0.0f) {
			cooldownTimer_ = 0.0f;
			readyPulse_ = readyPulseDuration_;
		}
	}

	if (triggered && canAct && !active && cooldownTimer_ <= 0.0f && !(timeTaken && s->UsesTimeScale())) {
		if (s->Activate()) {
			wasActive_ = true;
			SoundManager::GetInstance()->Play2DSound("se_charge_shot");
		}
	}
}

void FlightUnitPart::Draw3D() {
	if (FlightUnitSkill* s = Current()) s->Draw();
}

bool FlightUnitPart::UsesBackpack() const {
	const FlightUnitSkill* s = Current();
	return s && s->UsesBackpack();
}

void FlightUnitPart::UpdateModels(AnimatedObject3DInstance* body) {
	backpackPlaced_ = false;
	FlightUnitSkill* s = Current();
	if (!backpack_ || !body || !s || !s->UsesBackpack()) return;
	backpackSocket_.target = body;
	backpackSocket_.jointName = backpackBone_;
	backpackSocket_.offset = backpackMount_;
	const Matrix4x4 world = backpackSocket_.World();
	backpack_->SetWorldMatrixOverride(world);
	backpack_->SetTranslate({ world.m[3][0], world.m[3][1], world.m[3][2] });
	backpack_->Update();
	s->UpdateModels(world);
	backpackPlaced_ = true;
}

void FlightUnitPart::DrawModels(DirectXCore* dxCore) {
	if (!backpackPlaced_) return;
	FlightUnitSkill* s = Current();
	if (!s) return;
	if (backpack_ && s->DrawsBackpack()) backpack_->Draw(dxCore);
	s->DrawModels(dxCore);
}

void FlightUnitPart::DrawUI() {
	FlightUnitSkill* s = Current();
	if (!s || !barBg_ || !barFg_) return;

	const Vector4 c = s->GetColor();
	float ratio = 1.0f;
	Vector4 fg = c;
	if (s->IsActive()) {
		ratio = s->GetActiveRatio();
	} else if (cooldownTimer_ > 0.0f) {
		const float cd = (std::max)(s->GetCooldown(), 1e-3f);
		ratio = 1.0f - cooldownTimer_ / cd;
		fg.w = c.w * barCoolingAlpha_;
	} else if (readyPulse_ > 0.0f && readyPulseDuration_ > 0.0f) {
		// 撃てるようになった瞬間に白へ寄せて光らせる
		const float k = readyPulse_ / readyPulseDuration_;
		fg = { c.x + (1.0f - c.x) * k, c.y + (1.0f - c.y) * k, c.z + (1.0f - c.z) * k, c.w };
	}
	ratio = std::clamp(ratio, 0.0f, 1.0f);

	barBg_->SetPosition(barPos_);
	barBg_->SetSize(barSize_);
	barBg_->SetColor(barBgColor_);
	barFg_->SetPosition(barPos_);
	barFg_->SetSize({ barSize_.x * ratio, barSize_.y });
	barFg_->SetColor(fg);
	barBg_->Update();
	barFg_->Update();
	barBg_->Draw();
	barFg_->Draw();
}

void FlightUnitPart::Reset() {
	if (FlightUnitSkill* s = Current(); s && s->IsActive()) s->Cancel(true);
	cooldownTimer_ = 0.0f;
	wasActive_ = false;
	readyPulse_ = 0.0f;
}

bool FlightUnitPart::IsTimeSlowActive() const {
	const FlightUnitSkill* s = Current();
	return s && s->IsActive() && s->UsesTimeScale();
}

float FlightUnitPart::GetMoveSpeedScale() const {
	const FlightUnitSkill* s = Current();
	return s ? s->GetMoveSpeedScale() : 1.0f;
}

float FlightUnitPart::GetJustDodgeGaugeBonus() const {
	const FlightUnitSkill* s = Current();
	return s ? s->GetJustDodgeGaugeBonus() : 0.0f;
}

float FlightUnitPart::GetAttackScale() const {
	const FlightUnitSkill* s = Current();
	return s ? s->GetAttackScale() : 1.0f;
}

float FlightUnitPart::GetMaxHpScale() const {
	const FlightUnitSkill* s = Current();
	return s ? s->GetMaxHpScale() : 1.0f;
}

float FlightUnitPart::GetJustDodgeWindowBonus() const {
	const FlightUnitSkill* s = Current();
	return s ? s->GetJustDodgeWindowBonus() : 0.0f;
}

int FlightUnitPart::ModifyIncomingDamage(int damage) {
	FlightUnitSkill* s = Current();
	return s ? s->ModifyIncomingDamage(damage) : damage;
}

bool FlightUnitPart::TryAbsorbHit(int damage) {
	FlightUnitSkill* s = Current();
	return s ? s->AbsorbHit(damage) : false;
}

bool FlightUnitPart::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;
	const char* keys[] = { "", "assault", "guard", "mobility" };
	for (size_t i = 1; i < skills_.size(); ++i) {
		if (skills_[i] && root[keys[i]].IsObject()) skills_[i]->LoadTuning(root[keys[i]]);
	}
	const JsonValue& ui = root["ui"];
	if (ui.IsObject()) {
		barPos_.x = static_cast<float>(ui["posX"].AsDouble(barPos_.x));
		barPos_.y = static_cast<float>(ui["posY"].AsDouble(barPos_.y));
		barSize_.x = static_cast<float>(ui["width"].AsDouble(barSize_.x));
		barSize_.y = static_cast<float>(ui["height"].AsDouble(barSize_.y));
		barCoolingAlpha_ = static_cast<float>(ui["coolingAlpha"].AsDouble(barCoolingAlpha_));
		FlightUnitSkill::ReadVec4(ui, "bgColor", barBgColor_);
	}
	const JsonValue& bp = root["backpack"];
	if (bp.IsObject()) {
		backpackBone_ = bp["bone"].AsString(backpackBone_);
		FlightUnitSkill::ReadTransform(bp["mount"], backpackMount_);
	}
	return true;
}

void FlightUnitPart::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("20_Loadout.md §3 飛行ユニット。配列は [段階1, 段階2, 段階3]。秒は実時間"));
	const char* keys[] = { "", "assault", "guard", "mobility" };
	for (size_t i = 1; i < skills_.size(); ++i) {
		if (!skills_[i]) continue;
		JsonValue node = JsonValue::MakeObject();
		skills_[i]->SaveTuning(node);
		root[keys[i]] = std::move(node);
	}
	JsonValue ui = JsonValue::MakeObject();
	ui["posX"] = JsonValue(static_cast<double>(barPos_.x));
	ui["posY"] = JsonValue(static_cast<double>(barPos_.y));
	ui["width"] = JsonValue(static_cast<double>(barSize_.x));
	ui["height"] = JsonValue(static_cast<double>(barSize_.y));
	ui["coolingAlpha"] = JsonValue(static_cast<double>(barCoolingAlpha_));
	FlightUnitSkill::WriteVec4(ui, "bgColor", barBgColor_);
	root["ui"] = std::move(ui);
	JsonValue bp = JsonValue::MakeObject();
	bp["bone"] = JsonValue(backpackBone_);
	bp["mount"] = FlightUnitSkill::WriteTransform(backpackMount_);
	root["backpack"] = std::move(bp);

	std::filesystem::path p(kTuningPath);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
	}
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void FlightUnitPart::OnImGui() {
#ifdef _DEBUG
	if (LoadoutState::GetInstance()->OnImGui()) SyncLoadout();
	if (const FlightUnitSkill* s = Current()) {
		ImGui::Text("Equipped: %s  Active: %s  Cooldown: %.1f / %.1f", LoadoutState::GetFlightUnitName(kind_),
			s->IsActive() ? "yes" : "no", cooldownTimer_, s->GetCooldown());
		if (ImGui::Button("Reset Cooldown##FlightUnit")) Reset();
	} else {
		ImGui::TextDisabled("Equipped: Wings (no RB)");
	}
	const char* names[] = { "", "Assault##FlightUnitTuning", "Guard##FlightUnitTuning", "Mobility##FlightUnitTuning" };
	for (size_t i = 1; i < skills_.size(); ++i) {
		if (skills_[i] && ImGui::TreeNode(names[i])) {
			skills_[i]->OnImGui();
			ImGui::TreePop();
		}
	}
	if (ImGui::TreeNode("Backpack##FlightUnit")) {
		ImGui::TextDisabled("Bone: %s  (attack / defense units use it)", backpackBone_.c_str());
		FlightUnitSkill::DragTransform("BackpackMount", backpackMount_);
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Cooldown Bar##FlightUnit")) {
		ImGui::DragFloat2("Pos##FlightUnitBar", &barPos_.x, 1.0f, 0.0f, 4000.0f, "%.0f");
		ImGui::DragFloat2("Size##FlightUnitBar", &barSize_.x, 1.0f, 1.0f, 1000.0f, "%.0f");
		ImGui::SliderFloat("Cooling Alpha##FlightUnitBar", &barCoolingAlpha_, 0.0f, 1.0f, "%.2f");
		ImGui::ColorEdit4("Bg##FlightUnitBar", &barBgColor_.x);
		ImGui::TreePop();
	}
	if (ImGui::Button("Save##FlightUnit")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload##FlightUnit")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
#endif
}
