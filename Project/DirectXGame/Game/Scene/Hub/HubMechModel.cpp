#include "HubMechModel.h"

#include "Cockpit/CockpitModel.h"
#include "Object3DInstance.h"
#include "ModelManager.h"
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "Material.h"
#include "Json/JsonValue.h"
#include <algorithm>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 自機の新モデル（rig_player_mech.py --body-clips で書き出して cook。StageStartPart と同じもの）
	constexpr const char* kBodyDir = "Resources/Models/Player/PlayerModel";
	constexpr const char* kBodyFile = "PlayerModel.mesh";
	constexpr const char* kOfferClip = "Resources/Models/Player/PlayerModel/PlayerModel_Offer.anim";
	constexpr const char* kRetractClip = "Resources/Models/Player/PlayerModel/PlayerModel_Retract.anim";
	// 胸の装甲の部位（ベースカラーのテクスチャ Chest_BaseColor）。差し出した手は穴の範囲に入っても消さない
	constexpr const char* kChestTextureKeyword = "Chest_";

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

void HubMechModel::Initialize(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
	DirectXCore* dxCore, SRVManager* srvManager) {
	bodyModel_ = std::make_unique<AnimatedModelInstance>();
	bodyModel_->Initialize(ModelManager::GetInstance()->GetModelCore(), kBodyDir, kBodyFile);
	body_ = std::make_unique<AnimatedObject3DInstance>();
	body_->Initialize(object3DManager, skinningComputeManager, dxCore, srvManager, bodyModel_.get(), "HubMech");
	cockpit_ = std::make_unique<CockpitModel>();
	cockpit_->Initialize(object3DManager, dxCore);
	armLoaded_ = false;
	Update();
}

void HubMechModel::ApplyArm() {
	if (!armLoaded_ || loadedArm_ != pose_.arm) {
		body_->PlayAnimation(pose_.arm == ArmClip::Offer ? kOfferClip : kRetractClip, 0.0f);
		body_->SetLoop(false);
		loadedArm_ = pose_.arm;
		armLoaded_ = true;
	}
	// 時刻は時間軸が決める（ImGui のシークでも手が一緒に動く）。再生は止めておき、Update(0) でその時刻の姿勢にする
	body_->Pause();
	const float duration = bodyModel_->GetAnimation().duration;
	body_->SetAnimationTime(std::clamp(pose_.armTime, 0.0f, (std::max)(duration, 0.0f)));
}

void HubMechModel::ApplyChest() {
	if (pose_.chestOpen <= 0.0f) {
		body_->ClearDissolve();
		return;
	}
	DissolveSphere d{};
	d.mode = kDissolveHexHole;
	d.center = { position_.x + chestCenter_.x, position_.y + chestCenter_.y, position_.z + chestCenter_.z };
	// 開き切りで chestRadius_ の内側が消える（帯はその外側に残る）
	d.radius = (chestRadius_ + chestEdgeWidth_) * std::clamp(pose_.chestOpen, 0.0f, 1.0f);
	d.stretchY = chestStretchY_;
	d.edgeWidth = chestEdgeWidth_;
	d.edgeColor = chestEdgeColor_;
	d.cellSize = chestCellSize_;
	d.cellJitter = chestCellJitter_;
	body_->SetDissolveSphere(d, kChestTextureKeyword);
}

void HubMechModel::Update() {
	// 本体とコクピットは同じ位置（機体は回さない。正面 +Z）
	body_->SetTranslate(position_);
	body_->SetRotate({ 0.0f, 0.0f, 0.0f });
	body_->SetScale({ bodyScale_, bodyScale_, bodyScale_ });
	body_->SetVisible(drawBody_ && !bodyHidden_);
	ApplyArm();
	ApplyChest();
	body_->Update(0.0f);
	cockpit_->SetPosition(position_);
	cockpit_->Update();
}

void HubMechModel::Draw(DirectXCore* dxCore) {
	// コクピットを先に（本体の Draw はルートシグネチャを貼り直してから自分の分を貼る）
	cockpit_->Draw(dxCore);
	body_->Draw(dxCore);
}

void HubMechModel::DrawBloom(DirectXCore* dxCore, float bloomScale, bool includeAll) {
	cockpit_->DrawBloom(dxCore, bloomScale, includeAll);
	if (pose_.chestOpen > 0.0f) body_->DrawBloomPass(dxCore, chestBloomScale_);
}

void HubMechModel::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	cockpit_->SetHatchOpenDegrees(static_cast<float>(node["hatchOpenDegrees"].AsDouble(cockpit_->GetHatchOpenDegrees())));
	readFloat("bodyScale", bodyScale_);
	ReadVector3(node["chestCenter"], chestCenter_);
	readFloat("chestRadius", chestRadius_);
	readFloat("chestStretchY", chestStretchY_);
	readFloat("chestEdgeWidth", chestEdgeWidth_);
	ReadVector3(node["chestEdgeColor"], chestEdgeColor_);
	readFloat("chestCellSize", chestCellSize_);
	readFloat("chestCellJitter", chestCellJitter_);
	readFloat("chestBloomScale", chestBloomScale_);
}

void HubMechModel::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["hatchOpenDegrees"] = JsonValue(static_cast<double>(cockpit_->GetHatchOpenDegrees()));
	node["bodyScale"] = JsonValue(static_cast<double>(bodyScale_));
	node["chestCenter"] = MakeVector3(chestCenter_);
	node["chestRadius"] = JsonValue(static_cast<double>(chestRadius_));
	node["chestStretchY"] = JsonValue(static_cast<double>(chestStretchY_));
	node["chestEdgeWidth"] = JsonValue(static_cast<double>(chestEdgeWidth_));
	node["chestEdgeColor"] = MakeVector3(chestEdgeColor_);
	node["chestCellSize"] = JsonValue(static_cast<double>(chestCellSize_));
	node["chestCellJitter"] = JsonValue(static_cast<double>(chestCellJitter_));
	node["chestBloomScale"] = JsonValue(static_cast<double>(chestBloomScale_));
}

void HubMechModel::OnImGui() {
#ifdef _DEBUG
	float hatchOpenDegrees = cockpit_->GetHatchOpenDegrees();
	if (ImGui::DragFloat("Hatch Open (deg)", &hatchOpenDegrees, 1.0f, -180.0f, 180.0f)) {
		cockpit_->SetHatchOpenDegrees(hatchOpenDegrees);
	}
	ImGui::Checkbox("Draw Body", &drawBody_);
	ImGui::DragFloat("Body Scale", &bodyScale_, 0.01f, 0.1f, 100.0f);
	ImGui::Text("Arm: %s %.2f s / Chest Open %.2f", pose_.arm == ArmClip::Offer ? "Offer" : "Retract",
		pose_.armTime, pose_.chestOpen);
	ImGui::SeparatorText("Chest Opening (hexagon dissolve)");
	ImGui::DragFloat3("Center (from origin)", &chestCenter_.x, 0.01f);
	ImGui::DragFloat("Radius", &chestRadius_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Stretch Y", &chestStretchY_, 0.01f, 0.1f, 4.0f);
	ImGui::DragFloat("Glow Band", &chestEdgeWidth_, 0.005f, 0.0f, 2.0f);
	ImGui::ColorEdit3("Edge Color", &chestEdgeColor_.x, ImGuiColorEditFlags_HDR | ImGuiColorEditFlags_Float);
	ImGui::DragFloat("Cell Size", &chestCellSize_, 0.005f, 0.05f, 2.0f);
	ImGui::DragFloat("Cell Jitter", &chestCellJitter_, 0.005f, 0.0f, 2.0f);
	ImGui::DragFloat("Bloom Scale", &chestBloomScale_, 0.01f, 0.0f, 10.0f);
#endif // _DEBUG
}
