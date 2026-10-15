#include "HubLiftDescent.h"

#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// tools/BlenderPipeline/gen_catapult.py --export Assets/Models/Catapult を cook したもの（16 番 §2.1）
	constexpr const char* kModelDir = "Resources/Models/Catapult";
	constexpr const char* kLightMaterial = "Light";
	// 縦穴の単位を置く枠の数（8m × 10 = 80m。unitsAboveLift_ の上限はこれで決まる）
	constexpr int kMaxShaftUnits = 10;

	Vector3 Scale(const Vector3& v, float s) { return { v.x * s, v.y * s, v.z * s }; }

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}

	JsonValue MakeVector3(const Vector3& v) {
		JsonValue node = JsonValue::MakeArray();
		node.Push(JsonValue(static_cast<double>(v.x)));
		node.Push(JsonValue(static_cast<double>(v.y)));
		node.Push(JsonValue(static_cast<double>(v.z)));
		return node;
	}
}

HubLiftDescent::HubLiftDescent() = default;
HubLiftDescent::~HubLiftDescent() = default;

void HubLiftDescent::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry) {
	geometry_ = geometry;

	lift_ = std::make_unique<Object3DInstance>();
	lift_->Initialize(object3DManager, dxCore, kModelDir, "StartLift.mesh", "StartLift");
	for (size_t i = 0; i < clamps_.size(); ++i) {
		clamps_[i] = std::make_unique<Object3DInstance>();
		clamps_[i]->Initialize(object3DManager, dxCore, kModelDir, "CatapultClamp.mesh", "HubClamp" + std::to_string(i));
	}
	shaftTop_ = std::make_unique<Object3DInstance>();
	shaftTop_->Initialize(object3DManager, dxCore, kModelDir, "StartShaftTop.mesh", "StartShaftTop");
	shaftUnits_.clear();
	for (int i = 0; i < kMaxShaftUnits; ++i) {
		auto unit = std::make_unique<Object3DInstance>();
		unit->Initialize(object3DManager, dxCore, kModelDir, "StartShaftUnit.mesh", "StartShaftUnit" + std::to_string(i));
		shaftUnits_.push_back(std::move(unit));
	}
	Reset();
}

void HubLiftDescent::Start() {
	time_ = 0.0f;
	playing_ = true;
	releaseFired_ = false;
	releaseImpact_ = false;
	previewing_ = false;
	ApplyPose();
}

void HubLiftDescent::Reset() {
	time_ = 0.0f;
	playing_ = false;
	releaseFired_ = false;
	releaseImpact_ = false;
	previewing_ = false;
	ApplyPose();
}

void HubLiftDescent::Update(float deltaTime) {
	if (playing_) {
		time_ += deltaTime;
		if (!releaseFired_ && time_ >= releaseTime_) {
			releaseFired_ = true;
			releaseImpact_ = true;
		}
	}
	ApplyPose();
}

bool HubLiftDescent::ConsumeReleaseImpact() {
	const bool fired = releaseImpact_;
	releaseImpact_ = false;
	return fired;
}

float HubLiftDescent::DistanceAt(float time) const {
	const float t = time - releaseTime_;
	if (t <= 0.0f) return 0.0f;
	const float a = (std::max)(accelSeconds_, 1e-3f);
	if (t < a) {
		// 速さを smoothstep で上げる（v·u²(3−2u)）。その積分 v·a·(u³ − u⁴/2)
		const float u = t / a;
		return descentSpeed_ * a * (u * u * u - 0.5f * u * u * u * u);
	}
	return descentSpeed_ * (0.5f * a + (t - a));
}

float HubLiftDescent::SpeedAt(float time) const {
	const float t = time - releaseTime_;
	if (t <= 0.0f) return 0.0f;
	const float a = (std::max)(accelSeconds_, 1e-3f);
	if (t >= a) return descentSpeed_;
	const float u = t / a;
	return descentSpeed_ * u * u * (3.0f - 2.0f * u);
}

float HubLiftDescent::GetUiCollapse() const {
	if (!playing_ && !previewing_) return 0.0f;
	return std::clamp(GetTime() / (std::max)(uiCollapseSeconds_, 1e-3f), 0.0f, 1.0f);
}

float HubLiftDescent::GetSunScale() const {
	const float k = std::clamp(DistanceAt(GetTime()) / (std::max)(sunFadeDepth_, 1e-3f), 0.0f, 1.0f);
	return 1.0f + (shaftSunScale_ - 1.0f) * k;
}

StageStartHandoff HubLiftDescent::MakeHandoff() const {
	StageStartHandoff handoff;
	const float unitH = (std::max)(geometry_.shaftUnitHeight, 1e-3f);
	handoff.liftY = GetMechY();
	float phase = std::fmod(handoff.liftY + geometry_.shaftTopCollarDepth, unitH);
	if (phase < 0.0f) phase += unitH;
	handoff.shaftPhase = phase;
	handoff.descentSpeed = GetSpeed();
	return handoff;
}

void HubLiftDescent::ApplyPose() {
	if (!lift_) return;
	const float mechY = GetMechY();

	lift_->SetTranslate({ 0.0f, mechY + restOffsetY_, 0.0f });
	lift_->Update();
	for (size_t i = 0; i < clamps_.size(); ++i) {
		const Vector3& c = geometry_.clamps[i];
		clamps_[i]->SetTranslate({ c.x, c.y + mechY, c.z });
		clamps_[i]->Update();
	}
	shaftTop_->SetTranslate({ 0.0f, 0.0f, 0.0f });
	shaftTop_->Update();

	// 縦穴の単位：床の下に決まった枠（単位 k の下の端 y = −c − H(k+1)）があり、リフトの近くの枠だけに置く。
	// リフトより下はリフトの板で見えないので、リフトの裏まででよい
	const float c = geometry_.shaftTopCollarDepth;
	const float unitH = (std::max)(geometry_.shaftUnitHeight, 1e-3f);
	const float high = (std::min)(-c, mechY + unitsAboveLift_);
	const float low = mechY - geometry_.liftThickness;
	const int kMin = (std::max)(0, static_cast<int>(std::floor((-c - high) / unitH)));
	const int kMax = static_cast<int>(std::ceil((-c - low) / unitH)) - 1;
	shaftUnitCount_ = std::clamp(kMax - kMin + 1, 0, static_cast<int>(shaftUnits_.size()));
	for (int i = 0; i < shaftUnitCount_; ++i) {
		const int k = kMin + i;
		shaftUnits_[i]->SetTranslate({ 0.0f, -c - unitH * static_cast<float>(k + 1), 0.0f });
		shaftUnits_[i]->Update();
	}

	// 発光。ModelInstance はファイル名で共有なので、1 つに書けば全部の単位・両方の固定台に効く（毎フレーム書き直す）
	if (!shaftUnits_.empty()) {
		if (ModelInstance* model = shaftUnits_[0]->GetModelInstance()) {
			model->SetEmissiveByMaterialName(kLightMaterial, Scale(bandEmissive_, bandEmissiveScale_));
		}
	}
	if (ModelInstance* model = clamps_[0]->GetModelInstance()) {
		model->SetEmissiveByMaterialName(geometry_.neonMaterial, Scale(clampNeonColor_, clampNeonScale_));
	}
}

void HubLiftDescent::Draw(DirectXCore* dxCore) {
	lift_->Draw(dxCore);
	for (auto& clamp : clamps_) clamp->Draw(dxCore);
	if (!drawShaft_ || !IsHoleOpen()) return;
	shaftTop_->Draw(dxCore);
	for (int i = 0; i < shaftUnitCount_; ++i) shaftUnits_[i]->Draw(dxCore);
}

void HubLiftDescent::DrawBloom(DirectXCore* dxCore, float bloomScale) {
	for (auto& clamp : clamps_) clamp->DrawBloomPass(dxCore, bloomScale);
	if (!drawShaft_ || !IsHoleOpen()) return;
	for (int i = 0; i < shaftUnitCount_; ++i) shaftUnits_[i]->DrawBloomPass(dxCore, bloomScale);
}

void HubLiftDescent::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("uiCollapseSeconds", uiCollapseSeconds_);
	read("releaseTime", releaseTime_);
	read("accelSeconds", accelSeconds_);
	read("descentSpeed", descentSpeed_);
	read("changeSceneTime", changeSceneTime_);
	read("holeOpenDepth", holeOpenDepth_);
	read("restOffsetY", restOffsetY_);
	read("sunFadeDepth", sunFadeDepth_);
	read("shaftSunScale", shaftSunScale_);
	read("unitsAboveLift", unitsAboveLift_);
	read("releaseShakeIntensity", releaseShakeIntensity_);
	read("releaseShakeSeconds", releaseShakeSeconds_);
	ReadVector3(node["bandEmissive"], bandEmissive_);
	read("bandEmissiveScale", bandEmissiveScale_);
	ReadVector3(node["clampNeonColor"], clampNeonColor_);
	read("clampNeonScale", clampNeonScale_);
	ApplyPose();
}

void HubLiftDescent::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("uiCollapseSeconds", uiCollapseSeconds_);
	write("releaseTime", releaseTime_);
	write("accelSeconds", accelSeconds_);
	write("descentSpeed", descentSpeed_);
	write("changeSceneTime", changeSceneTime_);
	write("holeOpenDepth", holeOpenDepth_);
	write("restOffsetY", restOffsetY_);
	write("sunFadeDepth", sunFadeDepth_);
	write("shaftSunScale", shaftSunScale_);
	write("unitsAboveLift", unitsAboveLift_);
	write("releaseShakeIntensity", releaseShakeIntensity_);
	write("releaseShakeSeconds", releaseShakeSeconds_);
	node["bandEmissive"] = MakeVector3(bandEmissive_);
	write("bandEmissiveScale", bandEmissiveScale_);
	node["clampNeonColor"] = MakeVector3(clampNeonColor_);
	write("clampNeonScale", clampNeonScale_);
}

void HubLiftDescent::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("t = %.2f s  mechY = %.2f m  speed = %.2f m/s  hole = %s  units = %d",
		GetTime(), GetMechY(), GetSpeed(), IsHoleOpen() ? "open" : "closed", shaftUnitCount_);
	const StageStartHandoff h = MakeHandoff();
	ImGui::Text("handoff: shaftPhase = %.2f m", h.shaftPhase);

	// シーク（再生中でなくても見え方だけ動かす）
	ImGui::Checkbox("Preview (seek)", &previewing_);
	if (previewing_) {
		ImGui::SliderFloat("Seek (s)", &previewTime_, 0.0f, changeSceneTime_ + 10.0f);
		ApplyPose();
	}

	ImGui::SeparatorText("Timeline");
	ImGui::DragFloat("UI Collapse (s)", &uiCollapseSeconds_, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Release Time (s)", &releaseTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Accel (s)", &accelSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat("Descent Speed (m/s)", &descentSpeed_, 0.05f, 0.1f, 50.0f);
	ImGui::DragFloat("Change Scene Time (s)", &changeSceneTime_, 0.05f, 0.5f, 60.0f);
	ImGui::DragFloat("Release Shake", &releaseShakeIntensity_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Release Shake (s)", &releaseShakeSeconds_, 0.01f, 0.0f, 3.0f);

	ImGui::SeparatorText("Floor / Shaft");
	ImGui::DragFloat("Hole Open Depth (m)", &holeOpenDepth_, 0.001f, 0.0f, 2.0f, "%.3f");
	ImGui::DragFloat("Rest Offset Y (m)", &restOffsetY_, 0.0005f, -0.05f, 0.05f, "%.4f");
	ImGui::DragFloat("Units Above Lift (m)", &unitsAboveLift_, 0.5f, 0.0f,
		geometry_.shaftUnitHeight * static_cast<float>(kMaxShaftUnits - 2));
	ImGui::Checkbox("Draw Shaft", &drawShaft_);

	ImGui::SeparatorText("Light");
	ImGui::DragFloat("Sun Fade Depth (m)", &sunFadeDepth_, 0.1f, 0.1f, 100.0f);
	ImGui::SliderFloat("Shaft Sun Scale", &shaftSunScale_, 0.0f, 1.0f);
	ImGui::ColorEdit3("Band Emissive", &bandEmissive_.x);
	ImGui::DragFloat("Band Emissive Scale", &bandEmissiveScale_, 0.05f, 0.0f, 20.0f);
	ImGui::ColorEdit3("Clamp Neon", &clampNeonColor_.x);
	ImGui::DragFloat("Clamp Neon Scale", &clampNeonScale_, 0.05f, 0.0f, 20.0f);
	ImGui::TextDisabled("寸法は %s（gen_catapult.py が書く。手で直さない）", StageStartGeometry::kPath);
#endif // _DEBUG
}
