#include "HubMechModel.h"

#include "Cockpit/CockpitModel.h"
#include "Object3DInstance.h"
#include "Json/JsonValue.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// tools/BlenderPipeline/rig_hub_mech.py で書き出して cook したもの
	constexpr const char* kModelDir = "Resources/Models/HubMech/Export";
}

HubMechModel::HubMechModel() = default;
HubMechModel::~HubMechModel() = default;

Object3DInstance* HubMechModel::GetCockpit() const {
	return cockpit_->GetCockpit();
}

Object3DInstance* HubMechModel::GetHatch() const {
	return cockpit_->GetHatch();
}

void HubMechModel::SetHatchOpenRatio(float ratio) {
	cockpit_->SetHatchOpenRatio(ratio);
}

float HubMechModel::GetHatchOpenRatio() const {
	return cockpit_->GetHatchOpenRatio();
}

void HubMechModel::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore) {
	body_ = std::make_unique<Object3DInstance>();
	body_->Initialize(object3DManager, dxCore, kModelDir, "HubMech.mesh", "HubMech");
	cockpit_ = std::make_unique<CockpitModel>();
	cockpit_->Initialize(object3DManager, dxCore);
	ApplyBodyMaterial();
	Update();
}

void HubMechModel::Update() {
	// 本体とコクピットは同じ位置・回転（機体は回さない。正面 +Z）
	body_->SetTranslate(position_);
	body_->Update();
	cockpit_->SetPosition(position_);
	cockpit_->Update();
}

void HubMechModel::Draw(DirectXCore* dxCore) {
	if (drawBody_ && !bodyHidden_) body_->Draw(dxCore);
	cockpit_->Draw(dxCore);
}

void HubMechModel::DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll) {
	cockpit_->DrawBloom(dxCore, bloomScale, includeAll);
}

void HubMechModel::ApplyBodyMaterial() {
	if (!body_ || !overrideBodyMaterial_) return;
	body_->SetRoughness(bodyRoughness_);
	body_->SetMetallic(bodyMetallic_);
}

void HubMechModel::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	cockpit_->SetHatchOpenDegrees(static_cast<float>(node["hatchOpenDegrees"].AsDouble(cockpit_->GetHatchOpenDegrees())));
	overrideBodyMaterial_ = node["overrideBodyMaterial"].AsBool(overrideBodyMaterial_);
	bodyRoughness_ = static_cast<float>(node["bodyRoughness"].AsDouble(bodyRoughness_));
	bodyMetallic_ = static_cast<float>(node["bodyMetallic"].AsDouble(bodyMetallic_));
	ApplyBodyMaterial();
}

void HubMechModel::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["hatchOpenDegrees"] = JsonValue(static_cast<double>(cockpit_->GetHatchOpenDegrees()));
	node["overrideBodyMaterial"] = JsonValue(overrideBodyMaterial_);
	node["bodyRoughness"] = JsonValue(static_cast<double>(bodyRoughness_));
	node["bodyMetallic"] = JsonValue(static_cast<double>(bodyMetallic_));
}

void HubMechModel::OnImGui() {
#ifdef _DEBUG
	float hatchOpenDegrees = cockpit_->GetHatchOpenDegrees();
	if (ImGui::DragFloat("Hatch Open (deg)", &hatchOpenDegrees, 1.0f, -180.0f, 180.0f)) {
		cockpit_->SetHatchOpenDegrees(hatchOpenDegrees);
	}
	ImGui::Checkbox("Draw Body", &drawBody_);
	ImGui::SeparatorText("Body Material");
	bool changed = ImGui::Checkbox("Override (else cook value)", &overrideBodyMaterial_);
	changed |= ImGui::SliderFloat("Roughness", &bodyRoughness_, 0.04f, 1.0f);
	changed |= ImGui::SliderFloat("Metallic", &bodyMetallic_, 0.0f, 1.0f);
	if (changed) ApplyBodyMaterial();
	ImGui::TextDisabled("Override を外しても cook の値には戻らない（再起動で戻る）");
#endif // _DEBUG
}
