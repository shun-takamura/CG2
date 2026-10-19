#include "CatapultFacility.h"

#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "Material.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"
#include "Hub/CockpitReveal.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// tools/BlenderPipeline/gen_catapult.py --export Assets/Models/Catapult を cook したもの（16 番 §2.1）
	constexpr const char* kModelDir = "Resources/Models/Catapult";
	constexpr const char* kLightMaterial = "Light";
	// 縦穴の単位を置く枠の数（8m × 10 = 80m）
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

	// .mat のパスに name を含む submesh の色（ベースカラーに掛ける）。消えたネオンの板を暗くする
	void SetColorByMaterialName(Object3DInstance* object, const std::string& name, float gray) {
		ModelInstance* model = object ? object->GetModelInstance() : nullptr;
		if (!model) return;
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (sm.material && sm.matFilePath.find(name) != std::string::npos) sm.material->color = { gray, gray, gray, 1.0f };
		}
	}

	// .mat のパスに name を含む submesh の UV を U 方向へ u だけずらす
	void SetUvOffsetU(Object3DInstance* object, const std::string& name, float u) {
		ModelInstance* model = object ? object->GetModelInstance() : nullptr;
		if (!model) return;
		const Matrix4x4 uv = MakeAffineMatrix(Transform{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { u, 0.0f, 0.0f } });
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (sm.material && sm.matFilePath.find(name) != std::string::npos) sm.material->uvTransform = uv;
		}
	}
}

CatapultFacility::CatapultFacility() {
	lampSlots_.fill(kInvalidLightSlot);
	// 天井の近く（y 25 の少し下）に、リフトの所から射出口の方へ並べる
	lamps_[0] = { { 0.0f, 21.0f, -6.0f }, 1.2f, 32.0f, 1.5f };
	lamps_[1] = { { 0.0f, 21.0f, 12.0f }, 1.2f, 32.0f, 1.5f };
	lamps_[2] = { { 0.0f, 21.0f, 40.0f }, 1.0f, 36.0f, 1.5f };
	lamps_[3] = { { 0.0f, 21.0f, 80.0f }, 1.0f, 40.0f, 1.5f };
}

CatapultFacility::~CatapultFacility() = default;

void CatapultFacility::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore, const StageStartGeometry& geometry) {
	geometry_ = geometry;

	catapult_ = std::make_unique<Object3DInstance>();
	catapult_->Initialize(object3DManager, dxCore, kModelDir, "Catapult.mesh", "Catapult");
	lcd_ = std::make_unique<Object3DInstance>();
	lcd_->Initialize(object3DManager, dxCore, kModelDir, "CatapultLcd.mesh", "CatapultLcd");
	exitLight_ = std::make_unique<Object3DInstance>();
	exitLight_->Initialize(object3DManager, dxCore, kModelDir, "CatapultExitLight.mesh", "CatapultExitLight");
	lift_ = std::make_unique<Object3DInstance>();
	lift_->Initialize(object3DManager, dxCore, kModelDir, "StartLift.mesh", "CatapultLift");
	for (size_t i = 0; i < clamps_.size(); ++i) {
		clamps_[i] = std::make_unique<Object3DInstance>();
		clamps_[i]->Initialize(object3DManager, dxCore, kModelDir, "CatapultClamp.mesh", "CatapultClamp" + std::to_string(i));
	}
	shaftUnits_.clear();
	for (int i = 0; i < kMaxShaftUnits; ++i) {
		auto unit = std::make_unique<Object3DInstance>();
		unit->Initialize(object3DManager, dxCore, kModelDir, "StartShaftUnit.mesh", "CatapultShaftUnit" + std::to_string(i));
		shaftUnits_.push_back(std::move(unit));
	}

	Update(0.0f, 0.0f);
}

void CatapultFacility::AcquireLights() {
	auto* lm = LightManager::GetInstance();
	for (uint32_t& slot : lampSlots_) {
		if (slot == kInvalidLightSlot) slot = lm->AcquirePointLight();
	}
}

void CatapultFacility::Finalize() {
	auto* lm = LightManager::GetInstance();
	for (uint32_t& slot : lampSlots_) {
		if (slot != kInvalidLightSlot) lm->ReleasePointLight(slot);
		slot = kInvalidLightSlot;
	}
	// ModelInstance はファイル名で共有なので、ずらした UV・差し替えた液晶・暗くしたネオンを持ち越さない
	for (const std::string& name : geometry_.lcdMaterials) SetUvOffsetU(lcd_.get(), name, 0.0f);
	ResetLaunchState();
	SetLcdTexture(geometry_.lcdTextureUnable);
	SetColorByMaterialName(catapult_.get(), geometry_.neonMaterial, 1.0f);
	SetColorByMaterialName(clamps_[0].get(), geometry_.neonMaterial, 1.0f);
}

void CatapultFacility::ResetLaunchState() {
	readyPhase_ = ReadyPhase::Unable;
	readyTime_ = 0.0f;
}

void CatapultFacility::BeginReadySequence() {
	if (readyPhase_ != ReadyPhase::Unable) return;
	readyPhase_ = ReadyPhase::Sweeping;
	readyTime_ = 0.0f;
}

void CatapultFacility::AdvanceReadySequence(float deltaTime) {
	if (readyPhase_ != ReadyPhase::Sweeping) return;
	readyTime_ += deltaTime;
	if (readyTime_ >= ReadySequenceSeconds()) readyPhase_ = ReadyPhase::Ready;
}

float CatapultFacility::ReadySequenceSeconds() const {
	float farZ = neonSweepOriginZ_;
	for (const float z : geometry_.neonRowZ) farZ = (std::max)(farZ, z);
	const float sweep = neonSweepSpeed_ > 1e-3f ? (farZ - neonSweepOriginZ_) / neonSweepSpeed_ : 0.0f;
	return neonOffSeconds_ + sweep + neonFlickerSeconds_;
}

float CatapultFacility::GetSweepFrontZ() const {
	switch (readyPhase_) {
	case ReadyPhase::Unable: return -1.0e9f;
	case ReadyPhase::Ready:  return geometry_.catapultTunnelEndZ;
	default: break;
	}
	return neonSweepOriginZ_ + (std::max)(readyTime_ - neonOffSeconds_, 0.0f) * neonSweepSpeed_;
}

float CatapultFacility::NeonLevelAt(float z) const {
	if (readyPhase_ == ReadyPhase::Ready) return 1.0f;
	const float delay = neonSweepSpeed_ > 1e-3f ? (std::max)(z - neonSweepOriginZ_, 0.0f) / neonSweepSpeed_ : 0.0f;
	return CockpitReveal::Flicker(readyTime_ - neonOffSeconds_ - delay, neonFlickerSeconds_);
}

void CatapultFacility::SetLcdTexture(const std::string& path) {
	if (!lcd_ || path.empty() || path == lcdTexture_) return;
	ModelInstance* model = lcd_->GetModelInstance();
	if (!model) return;
	int count = 0;
	for (const std::string& name : geometry_.lcdMaterials) count += model->SetTextureByMaterialName(name, path);
	// GPU の準備前なら次のフレームで貼り直す
	if (count > 0) lcdTexture_ = path;
}

Vector3 CatapultFacility::ToWorld(const Vector3& local) const {
	return { origin_.x + local.x, origin_.y + local.y, origin_.z + local.z };
}

void CatapultFacility::Update(float lampLevel, float lcdScroll) {
	if (!catapult_) return;
	for (Object3DInstance* part : { catapult_.get(), lcd_.get(), exitLight_.get() }) {
		part->SetTranslate(origin_);
		part->Update();
	}
	lift_->SetTranslate(ToWorld({ 0.0f, liftY_, 0.0f }));
	lift_->Update();
	for (size_t i = 0; i < clamps_.size(); ++i) {
		const Vector3& c = geometry_.clamps[i];
		clamps_[i]->SetTranslate(ToWorld({ c.x, c.y + liftY_, c.z + clampTravel_ }));
		clamps_[i]->Update();
	}
	PlaceShaftUnits();
	ApplyEmissive(lcdScroll);

	auto* lm = LightManager::GetInstance();
	const float level = (std::max)(lampLevel, 0.0f);
	for (uint32_t i = 0; i < kLampCount; ++i) {
		const uint32_t slot = lampSlots_[i];
		if (slot == kInvalidLightSlot) continue;
		const Lamp& lamp = lamps_[i];
		lm->SetPointLightColor(slot, lampColor_);
		lm->SetPointLightPosition(slot, ToWorld(lamp.position));
		lm->SetPointLightIntensity(slot, lamp.intensity * level);
		lm->SetPointLightRadius(slot, lamp.radius);
		lm->SetPointLightDecay(slot, lamp.decay);
	}
}

void CatapultFacility::PlaceShaftUnits() {
	// 枠 k の下の端 y = base + H·k（k ≥ 0）。リフトより下はリフトの板で見えないので、リフトの裏から上だけ
	const float base = geometry_.catapultShaftBaseY;
	const float unitH = (std::max)(geometry_.shaftUnitHeight, 1e-3f);
	const float low = (std::max)(base, liftY_ - geometry_.liftThickness);
	const float high = liftY_ + unitsAboveLift_;
	shaftUnitCount_ = 0;
	if (high <= base) return;
	const int kMin = (std::max)(0, static_cast<int>(std::floor((low - base) / unitH)));
	const int kMax = static_cast<int>(std::ceil((high - base) / unitH)) - 1;
	shaftUnitCount_ = std::clamp(kMax - kMin + 1, 0, static_cast<int>(shaftUnits_.size()));
	for (int i = 0; i < shaftUnitCount_; ++i) {
		shaftUnits_[i]->SetTranslate(ToWorld({ 0.0f, base + unitH * static_cast<float>(kMin + i), 0.0f }));
		shaftUnits_[i]->Update();
	}
}

void CatapultFacility::ApplyEmissive(float lcdScroll) {
	// ModelInstance はファイル名で共有（縦穴の単位・固定台は HUB とも共有）なので毎フレーム書き直す
	const Vector3 light = Scale(lightEmissive_, lightEmissiveScale_);
	const Vector3 neon = Scale(neonColor_, neonScale_);
	const Vector3 ready = Scale(readyNeonColor_, readyNeonScale_);
	const bool unable = (readyPhase_ == ReadyPhase::Unable);
	SetLcdTexture(unable ? geometry_.lcdTextureUnable : geometry_.lcdTextureReady);
	// 発進不可なら赤ピンク。点け直し中は点き具合で 消えた（暗い地・発光なし）→ 緑
	auto applyNeon = [&](Object3DInstance* object, const std::string& name, float level) {
		ModelInstance* model = object ? object->GetModelInstance() : nullptr;
		if (!model) return;
		model->SetEmissiveByMaterialName(name, unable ? neon : Scale(ready, level));
		SetColorByMaterialName(object, name, unable ? 1.0f : neonOffColor_ + (1.0f - neonOffColor_) * level);
	};
	if (ModelInstance* model = catapult_->GetModelInstance()) {
		model->SetEmissiveByMaterialName(kLightMaterial, light);
		if (geometry_.neonRowMaterials.empty()) {
			// 列に分かれていない古い書き出し：全部を機体の位置の列と同じに
			applyNeon(catapult_.get(), geometry_.neonMaterial, NeonLevelAt(neonSweepOriginZ_));
		}
		for (size_t i = 0; i < geometry_.neonRowMaterials.size(); ++i) {
			applyNeon(catapult_.get(), geometry_.neonRowMaterials[i], NeonLevelAt(geometry_.neonRowZ[i]));
		}
	}
	if (ModelInstance* model = exitLight_->GetModelInstance()) {
		model->SetEmissiveByMaterialName(kLightMaterial, Scale(lightEmissive_, exitLightScale_ * exitLightLevel_));
	}
	if (!shaftUnits_.empty()) {
		if (ModelInstance* model = shaftUnits_[0]->GetModelInstance()) model->SetEmissiveByMaterialName(kLightMaterial, light);
	}
	// 固定台は機体の足元なので最初に点く
	applyNeon(clamps_[0].get(), geometry_.neonMaterial, NeonLevelAt(neonSweepOriginZ_));
	if (ModelInstance* model = lcd_->GetModelInstance()) {
		for (size_t i = 0; i < geometry_.lcdMaterials.size(); ++i) {
			model->SetEmissiveByMaterialName(geometry_.lcdMaterials[i], { lcdEmissiveScale_, lcdEmissiveScale_, lcdEmissiveScale_ }, 1.0f);
			// 文字が鏡にならないよう U の向きが左右で逆なので、流す符号も左右で逆（JSON の lcd.uPerMeterZ）
			SetUvOffsetU(lcd_.get(), geometry_.lcdMaterials[i], -lcdScroll * geometry_.lcdUPerMeterZ[i]);
		}
	}
}

void CatapultFacility::Draw(DirectXCore* dxCore) {
	catapult_->Draw(dxCore);
	lcd_->Draw(dxCore);
	// 射出口を開けているときは板を描かず、奥に StagePlay の空（skybox）を見せる
	if (!exitOpenToSky_) exitLight_->Draw(dxCore);
	lift_->Draw(dxCore);
	for (auto& clamp : clamps_) clamp->Draw(dxCore);
	for (int i = 0; i < shaftUnitCount_; ++i) shaftUnits_[i]->Draw(dxCore);
}

void CatapultFacility::DrawBloom(DirectXCore* dxCore, float bloomScale) {
	catapult_->DrawBloomPass(dxCore, bloomScale);
	lcd_->DrawBloomPass(dxCore, bloomScale);
	// 空に開けているときも、穴のまわりに少しだけにじませる（板は画面には描かず、発光 RT にだけ弱く描く）
	exitLight_->DrawBloomPass(dxCore, exitOpenToSky_ ? bloomScale * exitSkyBloom_ : bloomScale);
	for (auto& clamp : clamps_) clamp->DrawBloomPass(dxCore, bloomScale);
	for (int i = 0; i < shaftUnitCount_; ++i) shaftUnits_[i]->DrawBloomPass(dxCore, bloomScale);
}

void CatapultFacility::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	const JsonValue& lamps = node["lamps"];
	for (size_t i = 0; lamps.IsArray() && i < lamps.Size() && i < kLampCount; ++i) {
		const JsonValue& l = lamps[i];
		Lamp& lamp = lamps_[i];
		ReadVector3(l["position"], lamp.position);
		lamp.intensity = static_cast<float>(l["intensity"].AsDouble(lamp.intensity));
		lamp.radius = static_cast<float>(l["radius"].AsDouble(lamp.radius));
		lamp.decay = static_cast<float>(l["decay"].AsDouble(lamp.decay));
	}
	Vector3 lampColor = { lampColor_.x, lampColor_.y, lampColor_.z };
	ReadVector3(node["lampColor"], lampColor);
	lampColor_ = { lampColor.x, lampColor.y, lampColor.z, 1.0f };
	read("unitsAboveLift", unitsAboveLift_);
	ReadVector3(node["lightEmissive"], lightEmissive_);
	read("lightEmissiveScale", lightEmissiveScale_);
	read("exitLightScale", exitLightScale_);
	exitOpenToSky_ = node["exitOpenToSky"].AsBool(exitOpenToSky_);
	read("exitSkyBloom", exitSkyBloom_);
	ReadVector3(node["neonColor"], neonColor_);
	read("neonScale", neonScale_);
	read("lcdEmissiveScale", lcdEmissiveScale_);
	ReadVector3(node["readyNeonColor"], readyNeonColor_);
	read("readyNeonScale", readyNeonScale_);
	read("neonOffColor", neonOffColor_);
	read("neonOffSeconds", neonOffSeconds_);
	read("neonSweepOriginZ", neonSweepOriginZ_);
	read("neonSweepSpeed", neonSweepSpeed_);
	read("neonFlickerSeconds", neonFlickerSeconds_);
}

void CatapultFacility::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	JsonValue lamps = JsonValue::MakeArray();
	for (const Lamp& lamp : lamps_) {
		JsonValue l = JsonValue::MakeObject();
		l["position"] = MakeVector3(lamp.position);
		l["intensity"] = JsonValue(static_cast<double>(lamp.intensity));
		l["radius"] = JsonValue(static_cast<double>(lamp.radius));
		l["decay"] = JsonValue(static_cast<double>(lamp.decay));
		lamps.Push(std::move(l));
	}
	node["lamps"] = std::move(lamps);
	node["lampColor"] = MakeVector3({ lampColor_.x, lampColor_.y, lampColor_.z });
	write("unitsAboveLift", unitsAboveLift_);
	node["lightEmissive"] = MakeVector3(lightEmissive_);
	write("lightEmissiveScale", lightEmissiveScale_);
	write("exitLightScale", exitLightScale_);
	node["exitOpenToSky"] = JsonValue(exitOpenToSky_);
	write("exitSkyBloom", exitSkyBloom_);
	node["neonColor"] = MakeVector3(neonColor_);
	write("neonScale", neonScale_);
	write("lcdEmissiveScale", lcdEmissiveScale_);
	node["readyNeonColor"] = MakeVector3(readyNeonColor_);
	write("readyNeonScale", readyNeonScale_);
	write("neonOffColor", neonOffColor_);
	write("neonOffSeconds", neonOffSeconds_);
	write("neonSweepOriginZ", neonSweepOriginZ_);
	write("neonSweepSpeed", neonSweepSpeed_);
	write("neonFlickerSeconds", neonFlickerSeconds_);
}

void CatapultFacility::OnImGui() {
#ifdef _DEBUG
	ImGui::Text("origin (%.0f, %.0f, %.0f)  liftY = %.2f m  units = %d", origin_.x, origin_.y, origin_.z, liftY_, shaftUnitCount_);
	ImGui::SeparatorText("Lamps (from catapult origin)");
	ImGui::ColorEdit3("Lamp Color", &lampColor_.x);
	for (uint32_t i = 0; i < kLampCount; ++i) {
		Lamp& lamp = lamps_[i];
		ImGui::PushID(static_cast<int>(i));
		ImGui::Text("Lamp %u", i);
		ImGui::DragFloat3("Position", &lamp.position.x, 0.1f);
		ImGui::DragFloat("Intensity", &lamp.intensity, 0.01f, 0.0f, 20.0f);
		ImGui::DragFloat("Radius", &lamp.radius, 0.1f, 0.1f, 200.0f);
		ImGui::DragFloat("Decay", &lamp.decay, 0.01f, 0.1f, 8.0f);
		ImGui::PopID();
	}
	ImGui::SeparatorText("Emissive");
	ImGui::ColorEdit3("Light", &lightEmissive_.x);
	ImGui::DragFloat("Light Scale", &lightEmissiveScale_, 0.05f, 0.0f, 20.0f);
	ImGui::Checkbox("Exit Open To Sky", &exitOpenToSky_);
	ImGui::DragFloat("Exit Sky Bloom", &exitSkyBloom_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Exit Light Scale", &exitLightScale_, 0.05f, 0.0f, 40.0f);
	ImGui::ColorEdit3("Neon", &neonColor_.x);
	ImGui::DragFloat("Neon Scale", &neonScale_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("LCD Scale", &lcdEmissiveScale_, 0.05f, 0.0f, 20.0f);
	ImGui::SeparatorText("Ready Sequence (F4)");
	ImGui::Text("phase = %s  t = %.2f / %.2f s  rows = %zu",
		readyPhase_ == ReadyPhase::Unable ? "Unable" : readyPhase_ == ReadyPhase::Sweeping ? "Sweeping" : "Ready",
		readyTime_, ReadySequenceSeconds(), geometry_.neonRowMaterials.size());
	if (ImGui::Button("Begin##ready")) { ResetLaunchState(); BeginReadySequence(); }
	ImGui::SameLine();
	if (ImGui::Button("Reset##ready")) ResetLaunchState();
	ImGui::ColorEdit3("Ready Neon", &readyNeonColor_.x);
	ImGui::DragFloat("Ready Neon Scale", &readyNeonScale_, 0.05f, 0.0f, 20.0f);
	ImGui::DragFloat("Neon Off Color", &neonOffColor_, 0.005f, 0.0f, 1.0f);
	ImGui::DragFloat("Neon Off (s)", &neonOffSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Sweep Origin Z (m)", &neonSweepOriginZ_, 0.5f, -32.0f, 160.0f);
	ImGui::DragFloat("Sweep Speed (m/s)", &neonSweepSpeed_, 0.5f, 1.0f, 500.0f);
	ImGui::DragFloat("Row Flicker (s)", &neonFlickerSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Units Above Lift (m)", &unitsAboveLift_, 0.5f, 0.0f,
		geometry_.shaftUnitHeight * static_cast<float>(kMaxShaftUnits - 2));
	ImGui::TextDisabled("寸法は %s（gen_catapult.py が書く。手で直さない）", StageStartGeometry::kPath);
#endif // _DEBUG
}
