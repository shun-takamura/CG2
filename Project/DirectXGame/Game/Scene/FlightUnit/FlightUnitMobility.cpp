#include "FlightUnitMobility.h"

#include "Json/JsonValue.h"
#include "MathUtility.h"
#include "Object3DInstance.h"
#include <algorithm>
#include <string>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kWingDir = "Resources/Models/Weapons/FlightUnit/Mobility/Export";
	// [束][板]（板の順は FlightUnitMobility::kPlateCount の説明と同じ。右は左の左右反転）
	constexpr const char* kPlateNames[2][3] = {
		{ "MobilityOutBack", "MobilityMiddle", "MobilityOut" },
		{ "MobilityOutBackR", "MobilityMiddleR", "MobilityOutR" },
	};
}

FlightUnitMobility::FlightUnitMobility() {
	// 初期値（ImGui で合わせて FlightUnit.json に保存する）。
	// 閉じた形：3 枚を前後に重ねて、支点から下へ垂らす（束の傾きで外へ倒す）
	const Vector3 one{ 1.0f, 1.0f, 1.0f };
	closedPlates_[0] = { one, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -0.07f } };
	closedPlates_[1] = { one, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };
	closedPlates_[2] = { one, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.07f } };
	// 開いた形：奥はそのまま、中と手前を支点のまわりに外・上へ回す（Z の回転が大きいほど上がる）
	openPlates_[0] = closedPlates_[0];
	openPlates_[1] = { one, { 0.0f, 0.0f, 0.8f }, { 0.0f, 0.0f, 0.0f } };
	openPlates_[2] = { one, { 0.0f, 0.0f, 1.6f }, { 0.0f, 0.0f, 0.07f } };
	// 束：自機の左の肩の後ろ。下へ垂れる板を外へ倒す（右は鏡写し）
	groupMounts_[0] = { { 1.6f, 1.6f, 1.6f }, { 0.0f, 0.0f, 0.6f }, { 0.3f, 0.6f, 0.15f } };
	groupMounts_[1] = MirroredX(groupMounts_[0]);
}

FlightUnitMobility::~FlightUnitMobility() = default;

void FlightUnitMobility::InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) {
	for (int g = 0; g < kGroupCount; ++g) {
		for (int p = 0; p < kPlateCount; ++p) {
			auto& m = groups_[g].plates[p];
			m = std::make_unique<Object3DInstance>();
			m->Initialize(object3DManager, dxCore, kWingDir, std::string(kPlateNames[g][p]) + ".mesh", kPlateNames[g][p]);
			m->SetCamera(camera_);
		}
	}
}

void FlightUnitMobility::UpdateModels(const Matrix4x4& backpackWorld) {
	float k = preview_ ? std::clamp(previewRatio_, 0.0f, 1.0f) : openness_;
	k = k * k * (3.0f - 2.0f * k);
	for (int g = 0; g < kGroupCount; ++g) {
		Group& group = groups_[g];
		const Matrix4x4 groupWorld = Multiply(MakeAffineMatrix(groupMounts_[g]), backpackWorld);
		for (int p = 0; p < kPlateCount; ++p) {
			Transform local = LerpTransform(closedPlates_[p], openPlates_[p], k);
			if (g == 1) local = MirroredX(local);
			const Matrix4x4 w = Multiply(MakeAffineMatrix(local), groupWorld);
			if (auto& m = group.plates[p]) {
				m->SetWorldMatrixOverride(w);
				m->SetTranslate({ w.m[3][0], w.m[3][1], w.m[3][2] });
				m->Update();
			}
		}
		group.placed = true;
	}
}

void FlightUnitMobility::DrawModels(DirectXCore* dxCore) {
	for (const Group& g : groups_) {
		if (!g.placed) continue;
		for (const auto& m : g.plates) {
			if (m) m->Draw(dxCore);
		}
	}
}

bool FlightUnitMobility::Activate() {
	if (!host_) return false;
	// ほかの演出が World を握っている間は出さない（ジャスト回避・必殺技・Aim オーサリング）
	if (host_->GetTimeScale(TimeGroup::World) < 0.999f) return false;
	active_ = true;
	timer_ = 0.0f;
	return true;
}

void FlightUnitMobility::Cancel(bool restoreTime) {
	if (!active_) return;
	active_ = false;
	timer_ = 0.0f;
	if (restoreTime && host_) host_->SetTimeScale(TimeGroup::World, 1.0f);
}

float FlightUnitMobility::GetActiveRatio() const {
	const float d = slowDuration_[LevelIndex()];
	return d > 1e-4f ? std::clamp(1.0f - timer_ / d, 0.0f, 1.0f) : 0.0f;
}

void FlightUnitMobility::Update(float dt, bool canAct) {
	(void)canAct;
	// ウィングはスローの間だけ開く（実時間で動かす）
	if (active_) openness_ = openTime_ > 1e-4f ? (std::min)(1.0f, openness_ + dt / openTime_) : 1.0f;
	else openness_ = closeTime_ > 1e-4f ? (std::max)(0.0f, openness_ - dt / closeTime_) : 0.0f;
	if (!active_ || !host_) return;
	timer_ += dt;
	const float d = slowDuration_[LevelIndex()];
	if (timer_ >= d) {
		Cancel(true);
		return;
	}
	// 入り・抜けを短く補間する（1.0 ⇄ slowWorld_）
	float k = 1.0f;
	if (easeIn_ > 1e-4f && timer_ < easeIn_) k = timer_ / easeIn_;
	if (easeOut_ > 1e-4f && d - timer_ < easeOut_) k = (std::min)(k, (d - timer_) / easeOut_);
	host_->SetTimeScale(TimeGroup::World, 1.0f + (slowWorld_ - 1.0f) * k);
}

void FlightUnitMobility::LoadTuning(const JsonValue& node) {
	ReadLevels(node, "moveSpeedScale", moveSpeedScale_);
	ReadLevels(node, "justDodgeGaugeBonus", justDodgeGaugeBonus_);
	ReadLevels(node, "justDodgeWindowBonus", justDodgeWindowBonus_);
	ReadLevels(node, "slowDuration", slowDuration_);
	ReadLevels(node, "cooldown", cooldown_);
	slowWorld_ = static_cast<float>(node["slowWorld"].AsDouble(slowWorld_));
	easeIn_ = static_cast<float>(node["easeIn"].AsDouble(easeIn_));
	easeOut_ = static_cast<float>(node["easeOut"].AsDouble(easeOut_));
	ReadVec4(node, "color", color_);
	const JsonValue& mounts = node["wingMounts"];
	if (mounts.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kGroupCount) && i < mounts.Size(); ++i) ReadTransform(mounts[i], groupMounts_[i]);
	}
	const JsonValue& closed = node["wingClosed"];
	if (closed.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kPlateCount) && i < closed.Size(); ++i) ReadTransform(closed[i], closedPlates_[i]);
	}
	const JsonValue& open = node["wingOpen"];
	if (open.IsArray()) {
		for (size_t i = 0; i < static_cast<size_t>(kPlateCount) && i < open.Size(); ++i) ReadTransform(open[i], openPlates_[i]);
	}
	openTime_ = static_cast<float>(node["wingOpenTime"].AsDouble(openTime_));
	closeTime_ = static_cast<float>(node["wingCloseTime"].AsDouble(closeTime_));
}

void FlightUnitMobility::SaveTuning(JsonValue& node) const {
	WriteLevels(node, "moveSpeedScale", moveSpeedScale_);
	WriteLevels(node, "justDodgeGaugeBonus", justDodgeGaugeBonus_);
	WriteLevels(node, "justDodgeWindowBonus", justDodgeWindowBonus_);
	WriteLevels(node, "slowDuration", slowDuration_);
	WriteLevels(node, "cooldown", cooldown_);
	node["slowWorld"] = JsonValue(static_cast<double>(slowWorld_));
	node["easeIn"] = JsonValue(static_cast<double>(easeIn_));
	node["easeOut"] = JsonValue(static_cast<double>(easeOut_));
	WriteVec4(node, "color", color_);
	JsonValue mounts = JsonValue::MakeArray();
	for (const Transform& t : groupMounts_) mounts.Push(WriteTransform(t));
	node["wingMounts"] = std::move(mounts);
	JsonValue closed = JsonValue::MakeArray();
	JsonValue open = JsonValue::MakeArray();
	for (int i = 0; i < kPlateCount; ++i) {
		closed.Push(WriteTransform(closedPlates_[i]));
		open.Push(WriteTransform(openPlates_[i]));
	}
	node["wingClosed"] = std::move(closed);
	node["wingOpen"] = std::move(open);
	node["wingOpenTime"] = JsonValue(static_cast<double>(openTime_));
	node["wingCloseTime"] = JsonValue(static_cast<double>(closeTime_));
}

void FlightUnitMobility::OnImGui() {
#ifdef _DEBUG
	DragLevels("Move Speed Scale##Mobility", moveSpeedScale_, 0.01f, 0.5f, 3.0f);
	DragLevels("JustDodge Gauge Bonus##Mobility", justDodgeGaugeBonus_, 0.1f, 0.0f, 100.0f);
	DragLevels("JustDodge Window Bonus##Mobility", justDodgeWindowBonus_, 0.001f, 0.0f, 0.2f);
	DragLevels("Slow Duration##Mobility", slowDuration_, 0.05f, 0.1f, 20.0f);
	DragLevels("Cooldown##Mobility", cooldown_, 0.1f, 0.0f, 120.0f);
	ImGui::SliderFloat("Slow World##Mobility", &slowWorld_, 0.05f, 1.0f, "%.2f");
	ImGui::DragFloat("Ease In##Mobility", &easeIn_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Ease Out##Mobility", &easeOut_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::ColorEdit4("Color##Mobility", &color_.x);

	// 可変ウィング：機動系を付けているとゲームの中でそのまま見える
	ImGui::SeparatorText("Wings");
	ImGui::Checkbox("Preview##MobilityWing", &preview_);
	ImGui::SameLine();
	ImGui::SliderFloat("Open##MobilityWingPreview", &previewRatio_, 0.0f, 1.0f, "%.2f");
	ImGui::TextDisabled("0 = folded, 1 = open");
	ImGui::DragFloat("Open Time##MobilityWing", &openTime_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Close Time##MobilityWing", &closeTime_, 0.01f, 0.0f, 2.0f, "%.2f");
	if (ImGui::TreeNode("Mounts (backpack local)##MobilityWing")) {
		ImGui::SeparatorText("Left wing");
		DragTransform("WingMountL", groupMounts_[0]);
		ImGui::SeparatorText("Right wing");
		DragTransform("WingMountR", groupMounts_[1]);
		if (ImGui::Button("Right = mirror of Left##MobilityWing")) groupMounts_[1] = MirroredX(groupMounts_[0]);
		ImGui::TreePop();
	}
	const char* plateNames[kPlateCount] = { "Back (body side, fixed)", "Middle", "Front (camera side)" };
	if (ImGui::TreeNode("Closed plates (left wing local, pivot = top)##MobilityWing")) {
		for (int i = 0; i < kPlateCount; ++i) {
			ImGui::SeparatorText(plateNames[i]);
			ImGui::PushID(i);
			DragTransform("WingClosed", closedPlates_[i]);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
	if (ImGui::TreeNode("Open plates (left wing local, pivot = top)##MobilityWing")) {
		for (int i = 0; i < kPlateCount; ++i) {
			ImGui::SeparatorText(plateNames[i]);
			ImGui::PushID(i);
			DragTransform("WingOpen", openPlates_[i]);
			ImGui::PopID();
		}
		ImGui::TreePop();
	}
#endif
}
