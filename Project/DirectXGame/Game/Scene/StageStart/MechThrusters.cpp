#include "MechThrusters.h"

#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "Effect/EffectManager.h"
#include "MathUtility.h"
#include "Json/JsonValue.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	const char* kMountNames[MechThrusters::kCount] = { "Back L", "Back R", "Calf L", "Calf R" };
	const char* kMountKeys[MechThrusters::kCount] = { "backLeft", "backRight", "calfLeft", "calfRight" };

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

MechThrusters::MechThrusters() {
	// 機体は +Z を向く（左手系）ので機体の左は −X・右は +X（2026-10-19 実機：+X を左にしていて背中の左右が逆だった）。
	// 噴く向きは -Y を X まわりに回して後ろ下へ（70° で後ろ寄り）
	// 背中は背中の装甲の外へ（Spine2 から後ろ 1.3m では胴体に埋もれた。2026-10-19 実機）
	mounts_[kBackLeft] = { "mixamorig:Spine2", { -1.3f, 0.6f, -2.2f }, { 70.0f, 0.0f, 0.0f } };
	mounts_[kBackRight] = { "mixamorig:Spine2", { 1.3f, 0.6f, -2.2f }, { 70.0f, 0.0f, 0.0f } };
	// ふくらはぎは脚の表面の外へ（後ろ 0.8m では脚のメッシュに埋もれて光が隠れた。2026-10-19 実機）
	mounts_[kCalfLeft] = { "mixamorig:LeftFoot", { 0.0f, 1.5f, -1.6f }, { 60.0f, 0.0f, 0.0f } };
	mounts_[kCalfRight] = { "mixamorig:RightFoot", { 0.0f, 1.5f, -1.6f }, { 60.0f, 0.0f, 0.0f } };
	handles_.fill(kInvalidEffectHandle);
	playing_.fill(Size::Off);
}

MechThrusters::~MechThrusters() = default;

const std::string& MechThrusters::EffectName(Size size) const {
	switch (size) {
	case Size::Small:  return smallEffect_;
	case Size::Large:  return largeEffect_;
	default:           return mediumEffect_;
	}
}

void MechThrusters::StopAll() {
	EffectManager* em = EffectManager::GetInstance();
	for (int i = 0; i < kCount; ++i) {
		if (handles_[i] != kInvalidEffectHandle) em->Stop(handles_[i]);
		handles_[i] = kInvalidEffectHandle;
		playing_[i] = Size::Off;
	}
}

void MechThrusters::Update(const AnimatedObject3DInstance* mech, const Sizes& sizes) {
	if (!mech) {
		StopAll();
		return;
	}
	EffectManager* em = EffectManager::GetInstance();
	for (int i = 0; i < kCount; ++i) {
		const Mount& m = mounts_[i];
		const Matrix4x4 w = mech->GetJointWorldMatrix(m.bone);
		const Vector3 pos = { w.m[3][0] + m.offset.x, w.m[3][1] + m.offset.y, w.m[3][2] + m.offset.z };
		// 大きさが変わったら今のを止めて、別の大きさを再生し直す
		if (sizes[i] != playing_[i] || (sizes[i] != Size::Off && !em->IsAlive(handles_[i]))) {
			if (handles_[i] != kInvalidEffectHandle) em->Stop(handles_[i]);
			handles_[i] = sizes[i] == Size::Off ? kInvalidEffectHandle : em->Play(EffectName(sizes[i]), pos);
			playing_[i] = sizes[i];
		}
		if (handles_[i] == kInvalidEffectHandle) continue;
		em->SetPosition(handles_[i], pos);
		em->SetRotation(handles_[i], { DegToRad(m.rotationDegrees.x), DegToRad(m.rotationDegrees.y), DegToRad(m.rotationDegrees.z) });
	}
}

MechThrusters::Sizes MechThrusters::FromMove(bool up, bool left, bool down, bool right) const {
	// ポイント制：足し合わせるので WA などの斜めも勝手に組み合わさる
	std::array<int, kCount> level;
	level.fill(neutralLevel_);
	if (up) { level[kBackLeft] += 1; level[kBackRight] += 1; }
	if (down) { for (int& l : level) l -= 1; }
	// 左へ動く＝右の 2 基で押す
	if (left) { level[kBackRight] += 1; level[kCalfRight] += 1; level[kBackLeft] -= 1; level[kCalfLeft] -= 1; }
	if (right) { level[kBackLeft] += 1; level[kCalfLeft] += 1; level[kBackRight] -= 1; level[kCalfRight] -= 1; }
	Sizes s{};
	for (int i = 0; i < kCount; ++i) {
		const int l = level[i] < 0 ? 0 : (level[i] > 2 ? 2 : level[i]);
		s[i] = l == 0 ? Size::Small : (l == 1 ? Size::Medium : Size::Large);
	}
	return s;
}

MechThrusters::Sizes MechThrusters::FromThrust(float thrust) const {
	if (thrust < smallBelow_) return All(Size::Small);
	if (thrust < mediumBelow_) return All(Size::Medium);
	return All(Size::Large);
}

void MechThrusters::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	smallEffect_ = node["smallEffect"].AsString(smallEffect_);
	mediumEffect_ = node["mediumEffect"].AsString(mediumEffect_);
	largeEffect_ = node["largeEffect"].AsString(largeEffect_);
	smallBelow_ = static_cast<float>(node["smallBelow"].AsDouble(smallBelow_));
	mediumBelow_ = static_cast<float>(node["mediumBelow"].AsDouble(mediumBelow_));
	neutralLevel_ = static_cast<int>(node["neutralLevel"].AsDouble(neutralLevel_));
	for (int i = 0; i < kCount; ++i) {
		const JsonValue& m = node[kMountKeys[i]];
		if (!m.IsObject()) continue;
		mounts_[i].bone = m["bone"].AsString(mounts_[i].bone);
		ReadVector3(m["offset"], mounts_[i].offset);
		ReadVector3(m["rotationDegrees"], mounts_[i].rotationDegrees);
	}
}

void MechThrusters::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	node["smallEffect"] = JsonValue(smallEffect_);
	node["mediumEffect"] = JsonValue(mediumEffect_);
	node["largeEffect"] = JsonValue(largeEffect_);
	node["smallBelow"] = JsonValue(static_cast<double>(smallBelow_));
	node["mediumBelow"] = JsonValue(static_cast<double>(mediumBelow_));
	node["neutralLevel"] = JsonValue(static_cast<double>(neutralLevel_));
	for (int i = 0; i < kCount; ++i) {
		JsonValue m = JsonValue::MakeObject();
		m["bone"] = JsonValue(mounts_[i].bone);
		m["offset"] = MakeVector3(mounts_[i].offset);
		m["rotationDegrees"] = MakeVector3(mounts_[i].rotationDegrees);
		node[kMountKeys[i]] = m;
	}
}

void MechThrusters::OnImGui() {
#ifdef _DEBUG
	static const char* kSizeNames[] = { "Off", "S", "M", "L" };
	ImGui::Text("playing: %s %s %s %s", kSizeNames[static_cast<int>(playing_[0])], kSizeNames[static_cast<int>(playing_[1])],
		kSizeNames[static_cast<int>(playing_[2])], kSizeNames[static_cast<int>(playing_[3])]);
	ImGui::SliderFloat("Small Below (thrust)", &smallBelow_, 0.0f, 1.0f);
	ImGui::SliderFloat("Medium Below (thrust)", &mediumBelow_, 0.0f, 1.0f);
	ImGui::SliderInt("Neutral Level (0 S / 1 M / 2 L)", &neutralLevel_, 0, 2);
	for (int i = 0; i < kCount; ++i) {
		ImGui::PushID(i);
		ImGui::TextDisabled("%s (%s)", kMountNames[i], mounts_[i].bone.c_str());
		ImGui::DragFloat3("Offset (m)", &mounts_[i].offset.x, 0.02f);
		ImGui::DragFloat3("Rotation (deg)", &mounts_[i].rotationDegrees.x, 0.5f);
		ImGui::PopID();
	}
#endif // _DEBUG
}
