#include "CockpitModel.h"

#include "Object3DInstance.h"
#include "MathUtility.h"

namespace {
	// tools/BlenderPipeline/rig_hub_mech.py で書き出して cook したもの
	constexpr const char* kModelDir = "Resources/Models/HubMech/Export";

	// Blender の (x, y, z) はエンジンで (−x, z, −y)
	// ハッチの原点＝回転の軸。Blender の (0, −0.465, 12.343)。2026-10-18 自機の新モデルの胸に収めるため 0.7m 後ろへ（17 番 §5.1）
	const Vector3 kCockpitAxis = { 0.0f, 12.343f, 0.465f };
	// 座った目はコクピットの軸から Blender で (0, 0.18, 0.20)
	const Vector3 kSeatedEyeFromAxis = { 0.0f, 0.20f, -0.18f };
}

CockpitModel::CockpitModel() = default;
CockpitModel::~CockpitModel() = default;

Vector3 CockpitModel::GetAxis() {
	return kCockpitAxis;
}

Vector3 CockpitModel::GetSeatedEye() {
	return { kCockpitAxis.x + kSeatedEyeFromAxis.x, kCockpitAxis.y + kSeatedEyeFromAxis.y,
		kCockpitAxis.z + kSeatedEyeFromAxis.z };
}

void CockpitModel::Initialize(Object3DManager* object3DManager, DirectXCore* dxCore) {
	cockpit_ = std::make_unique<Object3DInstance>();
	cockpit_->Initialize(object3DManager, dxCore, kModelDir, "HubMech_Cockpit.mesh", "HubMechCockpit");
	hatch_ = std::make_unique<Object3DInstance>();
	hatch_->Initialize(object3DManager, dxCore, kModelDir, "HubMech_Hatch.mesh", "HubMechHatch");
	Update();
}

void CockpitModel::Update() {
	cockpit_->SetTranslate(position_);
	hatch_->SetTranslate({ position_.x + kCockpitAxis.x, position_.y + kCockpitAxis.y, position_.z + kCockpitAxis.z });
	hatch_->SetRotate({ DegToRad(hatchOpenDegrees_ * hatchOpenRatio_), 0.0f, 0.0f });
	cockpit_->Update();
	hatch_->Update();
}

void CockpitModel::Draw(DirectXCore* dxCore) {
	cockpit_->Draw(dxCore);
	hatch_->Draw(dxCore);
}

void CockpitModel::DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll) {
	cockpit_->DrawBloomPass(dxCore, bloomScale, includeAll);
	hatch_->DrawBloomPass(dxCore, bloomScale, includeAll);
}
