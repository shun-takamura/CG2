#include "HubMechModel.h"

#include "Object3DInstance.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// tools/BlenderPipeline/rig_hub_mech.py で書き出して cook したもの
	constexpr const char* kModelDir = "Resources/Models/HubMech/Export";

	// Blender の (x, y, z) はエンジンで (−x, z, −y)
	// ハッチの原点＝回転の軸。Blender の (0, −1.165, 12.343)
	const Vector3 kCockpitAxis = { 0.0f, 12.343f, 1.165f };
	// 座った目はコクピットの軸から Blender で (0, 0.18, 0.20)
	const Vector3 kSeatedEyeFromAxis = { 0.0f, 0.20f, -0.18f };
}

HubMechModel::HubMechModel() = default;
HubMechModel::~HubMechModel() = default;

Vector3 HubMechModel::GetCockpitAxis() {
	return kCockpitAxis;
}

Vector3 HubMechModel::GetSeatedEye() {
	return { kCockpitAxis.x + kSeatedEyeFromAxis.x, kCockpitAxis.y + kSeatedEyeFromAxis.y,
		kCockpitAxis.z + kSeatedEyeFromAxis.z };
}

void HubMechModel::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore) {
	body_ = std::make_unique<Object3DInstance>();
	body_->Initialize(object3DManager, dxCore, kModelDir, "HubMech.mesh", "HubMech");
	cockpit_ = std::make_unique<Object3DInstance>();
	cockpit_->Initialize(object3DManager, dxCore, kModelDir, "HubMech_Cockpit.mesh", "HubMechCockpit");
	hatch_ = std::make_unique<Object3DInstance>();
	hatch_->Initialize(object3DManager, dxCore, kModelDir, "HubMech_Hatch.mesh", "HubMechHatch");
	Update();
}

void HubMechModel::Update() {
	// 本体とコクピットは同じ位置・回転（機体は回さない。正面 +Z）
	body_->SetTranslate(position_);
	cockpit_->SetTranslate(position_);
	hatch_->SetTranslate({ position_.x + kCockpitAxis.x, position_.y + kCockpitAxis.y, position_.z + kCockpitAxis.z });
	hatch_->SetRotate({ DegToRad(hatchOpenDegrees_ * hatchOpenRatio_), 0.0f, 0.0f });
	body_->Update();
	cockpit_->Update();
	hatch_->Update();
}

void HubMechModel::Draw(DirectXCore* dxCore) {
	if (drawBody_) body_->Draw(dxCore);
	cockpit_->Draw(dxCore);
	hatch_->Draw(dxCore);
}

void HubMechModel::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	hatchOpenDegrees_ = static_cast<float>(node["hatchOpenDegrees"].AsDouble(hatchOpenDegrees_));
}

void HubMechModel::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["hatchOpenDegrees"] = JsonValue(static_cast<double>(hatchOpenDegrees_));
}

void HubMechModel::OnImGui() {
#ifdef _DEBUG
	ImGui::DragFloat("Hatch Open (deg)", &hatchOpenDegrees_, 1.0f, -180.0f, 180.0f);
	ImGui::Checkbox("Draw Body", &drawBody_);
#endif // _DEBUG
}
