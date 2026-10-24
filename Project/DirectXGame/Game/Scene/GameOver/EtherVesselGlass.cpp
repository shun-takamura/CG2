#include "EtherVesselGlass.h"

#include "Glass/LiquidGlassSphere.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <algorithm>
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/EtherVesselGlass.json";
	// G4 の gen_ether_vessel.py が書く寸法（手で直さない）
	constexpr const char* kGeometryPath = "Resources/Json/Tuning/EtherVesselGeometry.json";

	void ReadVector3(const JsonValue& node, Vector3& value) {
		if (!node.IsArray() || node.Size() < 3) return;
		value = { static_cast<float>(node[0].AsDouble(value.x)), static_cast<float>(node[1].AsDouble(value.y)),
			static_cast<float>(node[2].AsDouble(value.z)) };
	}

	JsonValue MakeVector3(const Vector3& v) {
		JsonValue a = JsonValue::MakeArray();
		a.Push(JsonValue(static_cast<double>(v.x)));
		a.Push(JsonValue(static_cast<double>(v.y)));
		a.Push(JsonValue(static_cast<double>(v.z)));
		return a;
	}

	void ReadFloat(const JsonValue& node, const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	}

	void WriteFloat(JsonValue& node, const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	}
}

EtherVesselGlass::EtherVesselGlass() = default;
EtherVesselGlass::~EtherVesselGlass() = default;

void EtherVesselGlass::Initialize(DirectXCore* dxCore, SRVManager* srvManager) {
	sphere_ = std::make_unique<LiquidGlassSphere>();
	sphere_->Initialize(dxCore, srvManager);
	LoadGeometry();
	LoadTuning();
	sphere_->SetCenter(GetSphereCenter());
	sphere_->SetRadius(sphereRadius_);
	sphere_->ResetMotion();
	ApplyGlow();
}

void EtherVesselGlass::LoadGeometry() {
	if (!std::filesystem::exists(kGeometryPath)) return;
	const auto result = JsonParser::ParseFile(kGeometryPath);
	if (!result.success) return;
	const JsonValue& sphere = result.value["sphere"];
	ReadVector3(sphere["center"], sphereCenter_);
	ReadFloat(sphere, "radius", sphereRadius_);
}

Vector3 EtherVesselGlass::GetSphereCenter() const {
	return { origin_.x + sphereCenter_.x, origin_.y + sphereCenter_.y, origin_.z + sphereCenter_.z };
}

void EtherVesselGlass::SetFill(float amount, bool immediate) {
	sphere_->SetFill(amount, immediate);
}

void EtherVesselGlass::SetDifficulty(float difficulty01) {
	difficulty_ = std::clamp(difficulty01, 0.0f, 1.0f);
	ApplyGlow();
}

void EtherVesselGlass::ApplyGlow() {
	sphere_->GetAppearance().liquidGlow = (glowAtMin_ + (glowAtMax_ - glowAtMin_) * difficulty_) * glowBoost_;
}

void EtherVesselGlass::Splash(float strength) {
	const Vector3 c = GetSphereCenter();
	sphere_->AddRipple({ c.x, c.y + sphere_->GetSurfaceOffset(), c.z }, splashRadius_, strength);
}

void EtherVesselGlass::Update(float deltaTime) {
	sphere_->SetCenter(GetSphereCenter());
	sphere_->SetRadius(sphereRadius_);
	sphere_->SetContainerRotation({ 0.0f, 0.0f, GetRotationZ() });
	sphere_->Update(deltaTime);
}

void EtherVesselGlass::DispatchSimulation() {
	sphere_->DispatchSimulation();
}

void EtherVesselGlass::Draw(const Camera& camera, const std::string& environmentCubemapPath) {
	sphere_->Draw(camera, environmentCubemapPath);
}

void EtherVesselGlass::DrawBloom(const Camera& camera, const std::string& environmentCubemapPath) {
	sphere_->DrawBloomPass(camera, environmentCubemapPath);
}

bool EtherVesselGlass::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return false;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;

	ReadFloat(root, "glowAtMin", glowAtMin_);
	ReadFloat(root, "glowAtMax", glowAtMax_);
	ReadFloat(root, "splashRadius", splashRadius_);

	LiquidGlassSphere::Appearance& a = sphere_->GetAppearance();
	const JsonValue& glass = root["glass"];
	ReadVector3(glass["tint"], a.glassTint);
	ReadFloat(glass, "fresnelF0", a.fresnelF0);
	ReadVector3(glass["rimColor"], a.rimColor);
	ReadFloat(glass, "rimPower", a.rimPower);
	ReadFloat(glass, "reflectionIntensity", a.reflectionIntensity);
	ReadFloat(glass, "specularIntensity", a.specularIntensity);
	ReadFloat(glass, "specularPower", a.specularPower);
	ReadFloat(glass, "glassAlpha", a.glassAlpha);
	ReadFloat(glass, "backRimIntensity", a.backRimIntensity);
	ReadFloat(glass, "lensIor", a.lensIor);
	ReadFloat(glass, "rimBloom", a.rimBloom);

	const JsonValue& liquid = root["liquid"];
	ReadVector3(liquid["color"], a.liquidColor);
	ReadFloat(liquid, "density", a.liquidDensity);
	ReadFloat(liquid, "absorption", a.liquidAbsorption);
	ReadFloat(liquid, "coreWhite", a.coreWhite);
	ReadFloat(liquid, "corePower", a.corePower);
	ReadVector3(liquid["surfaceColor"], a.surfaceColor);
	ReadFloat(liquid, "surfaceIntensity", a.surfaceIntensity);
	ReadFloat(liquid, "meniscusWidth", a.meniscusWidth);
	ReadFloat(liquid, "meniscusIntensity", a.meniscusIntensity);
	ReadFloat(liquid, "bloomScale", a.bloomScale);
	ReadFloat(liquid, "rippleNormalScale", a.rippleNormalScale);

	LiquidGlassSphere::Motion& m = sphere_->GetMotion();
	const JsonValue& motion = root["motion"];
	ReadFloat(motion, "fillFollow", m.fillFollow);
	ReadFloat(motion, "sloshStiffness", m.sloshStiffness);
	ReadFloat(motion, "sloshDamping", m.sloshDamping);
	ReadFloat(motion, "sloshDrive", m.sloshDrive);
	ReadFloat(motion, "sloshMaxTilt", m.sloshMaxTilt);
	ReadFloat(motion, "rippleCoverage", m.rippleCoverage);
	ReadFloat(motion, "sloshRippleStrength", m.sloshRippleStrength);
	ReadFloat(motion, "sloshRippleRadius", m.sloshRippleRadius);
	ReadFloat(motion, "ambientRippleInterval", m.ambientRippleInterval);
	ReadFloat(motion, "ambientRippleStrength", m.ambientRippleStrength);
	ReadFloat(motion, "ambientRippleRadius", m.ambientRippleRadius);
	ReadFloat(motion, "ambientRippleSpread", m.ambientRippleSpread);
	ApplyGlow();
	return true;
}

void EtherVesselGlass::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["_comment"] = JsonValue(std::string("19_GameOverAndDifficulty.md §4 G5。球の位置と半径は EtherVesselGeometry.json（G4）"));
	WriteFloat(root, "glowAtMin", glowAtMin_);
	WriteFloat(root, "glowAtMax", glowAtMax_);
	WriteFloat(root, "splashRadius", splashRadius_);

	const LiquidGlassSphere::Appearance& a = sphere_->GetAppearance();
	JsonValue& glass = root["glass"];
	glass = JsonValue::MakeObject();
	glass["tint"] = MakeVector3(a.glassTint);
	WriteFloat(glass, "fresnelF0", a.fresnelF0);
	glass["rimColor"] = MakeVector3(a.rimColor);
	WriteFloat(glass, "rimPower", a.rimPower);
	WriteFloat(glass, "reflectionIntensity", a.reflectionIntensity);
	WriteFloat(glass, "specularIntensity", a.specularIntensity);
	WriteFloat(glass, "specularPower", a.specularPower);
	WriteFloat(glass, "glassAlpha", a.glassAlpha);
	WriteFloat(glass, "backRimIntensity", a.backRimIntensity);
	WriteFloat(glass, "lensIor", a.lensIor);
	WriteFloat(glass, "rimBloom", a.rimBloom);

	JsonValue& liquid = root["liquid"];
	liquid = JsonValue::MakeObject();
	liquid["color"] = MakeVector3(a.liquidColor);
	WriteFloat(liquid, "density", a.liquidDensity);
	WriteFloat(liquid, "absorption", a.liquidAbsorption);
	WriteFloat(liquid, "coreWhite", a.coreWhite);
	WriteFloat(liquid, "corePower", a.corePower);
	liquid["surfaceColor"] = MakeVector3(a.surfaceColor);
	WriteFloat(liquid, "surfaceIntensity", a.surfaceIntensity);
	WriteFloat(liquid, "meniscusWidth", a.meniscusWidth);
	WriteFloat(liquid, "meniscusIntensity", a.meniscusIntensity);
	WriteFloat(liquid, "bloomScale", a.bloomScale);
	WriteFloat(liquid, "rippleNormalScale", a.rippleNormalScale);

	const LiquidGlassSphere::Motion& m = sphere_->GetMotion();
	JsonValue& motion = root["motion"];
	motion = JsonValue::MakeObject();
	WriteFloat(motion, "fillFollow", m.fillFollow);
	WriteFloat(motion, "sloshStiffness", m.sloshStiffness);
	WriteFloat(motion, "sloshDamping", m.sloshDamping);
	WriteFloat(motion, "sloshDrive", m.sloshDrive);
	WriteFloat(motion, "sloshMaxTilt", m.sloshMaxTilt);
	WriteFloat(motion, "rippleCoverage", m.rippleCoverage);
	WriteFloat(motion, "sloshRippleStrength", m.sloshRippleStrength);
	WriteFloat(motion, "sloshRippleRadius", m.sloshRippleRadius);
	WriteFloat(motion, "ambientRippleInterval", m.ambientRippleInterval);
	WriteFloat(motion, "ambientRippleStrength", m.ambientRippleStrength);
	WriteFloat(motion, "ambientRippleRadius", m.ambientRippleRadius);
	WriteFloat(motion, "ambientRippleSpread", m.ambientRippleSpread);

	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void EtherVesselGlass::OnImGui() {
#ifdef _DEBUG
	if (ImGui::Button("Save##EtherGlass")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload##EtherGlass")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
	ImGui::Text("Sphere center (%.3f, %.3f, %.3f) r %.3f", sphereCenter_.x, sphereCenter_.y, sphereCenter_.z, sphereRadius_);
	bool glowChanged = false;
	glowChanged |= ImGui::DragFloat("Glow at Difficulty 0", &glowAtMin_, 0.01f, 0.0f, 10.0f);
	glowChanged |= ImGui::DragFloat("Glow at Difficulty 1", &glowAtMax_, 0.01f, 0.0f, 10.0f);
	if (glowChanged) ApplyGlow();
	ImGui::DragFloat("Splash Radius [m]", &splashRadius_, 0.001f, 0.001f, 0.3f, "%.3f");
	sphere_->OnImGui();
	// Liquid の Glow は難易度から決まるので、ImGui で直接いじった値は次の SetDifficulty で上書きされる
#endif
}
