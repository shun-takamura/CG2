#include "MechThrusters.h"

#include "AnimatedObject3DInstance.h"
#include "Primitive/PrimitiveMesh.h"   // AnimatedObject3DInstance の暗黙のデストラクタが PrimitiveMesh の完全型を要る
#include "Effect/EffectManager.h"
#include "MathUtility.h"
#include "Quaternion.h"
#include "Json/JsonValue.h"

#include <cmath>

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

	/// <summary>エフェクトの -Y（噴く向き）を dir へ向けるオイラー角（EffectInstance の MakeRotateMatrix(Vector3) の規約）。
	/// 筒は Y まわりに対称なので最短の回転でよい。回転の向きの規約に頼らず、回した結果で符号を確かめる</summary>
	Vector3 EulerAimingDown(const Vector3& dir) {
		const Vector3 from = { 0.0f, -1.0f, 0.0f };
		const Vector3 to = Normalize(dir);
		const float c = Dot(from, to);
		if (c > 0.9999f) return { 0.0f, 0.0f, 0.0f };
		Vector3 axis = { from.y * to.z - from.z * to.y, from.z * to.x - from.x * to.z, from.x * to.y - from.y * to.x };
		const float axisLength = std::sqrt(Dot(axis, axis));
		axis = axisLength > 1e-5f ? Vector3{ axis.x / axisLength, axis.y / axisLength, axis.z / axisLength } : Vector3{ 1.0f, 0.0f, 0.0f };
		const float angle = std::acos(c < -1.0f ? -1.0f : c);
		Quaternion q = MakeRotateAxisAngleQuaternion(axis, angle);
		if (Dot(TransformMatrix(from, MakeRotateMatrix(q)), to) < 0.0f) q = MakeRotateAxisAngleQuaternion(axis, -angle);
		return EulerFromQuaternion(q);
	}
}

MechThrusters::MechThrusters(Preset preset) {
	// 機体は +Z を向く（左手系）ので機体の左は −X・右は +X（2026-10-19 実機：+X を左にしていて背中の左右が逆だった）。
	// 噴く向きは -Y を X まわりに回して後ろ下へ（70° で後ろ寄り）
	// 背中は背中の装甲の外へ（Spine2 から後ろ 1.3m では胴体に埋もれた。2026-10-19 実機）
	mounts_[kBackLeft] = { "mixamorig:Spine2", { -1.3f, 0.6f, -2.2f }, { 70.0f, 0.0f, 0.0f } };
	mounts_[kBackRight] = { "mixamorig:Spine2", { 1.3f, 0.6f, -2.2f }, { 70.0f, 0.0f, 0.0f } };
	// ふくらはぎは脚の表面の外へ（後ろ 0.8m では脚のメッシュに埋もれて光が隠れた。2026-10-19 実機）
	mounts_[kCalfLeft] = { "mixamorig:LeftFoot", { 0.0f, 1.5f, -1.6f }, { 60.0f, 0.0f, 0.0f } };
	mounts_[kCalfRight] = { "mixamorig:RightFoot", { 0.0f, 1.5f, -1.6f }, { 60.0f, 0.0f, 0.0f } };
	if (preset == Preset::Player) {
		// ゲームの自機は大きさ 1（演出の機体の 1/10.625）。ずらしも同じ比で縮め、エフェクトは縮めた版。
		// ずらしは機体の向き（+Z が前）で持ち、機体の回転で回す
		constexpr float kScale = 1.0f / 10.625f;
		for (Mount& m : mounts_) m.offset = { m.offset.x * kScale, m.offset.y * kScale, m.offset.z * kScale };
		smallEffect_ = "PlayerThruster_S";
		mediumEffect_ = "PlayerThruster_M";
		largeEffect_ = "PlayerThruster_L";
		followBody_ = true;
	}
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
	// followBody：機体の回転だけの行列（インスタンスの世界行列と同じ作り方。拡大・移動は除く）
	const Matrix4x4 body = followBody_ ? MakeAffineMatrix(Transform{ { 1.0f, 1.0f, 1.0f }, mech->GetRotate(), { 0.0f, 0.0f, 0.0f } })
		: MakeIdentity4x4();
	for (int i = 0; i < kCount; ++i) {
		const Mount& m = mounts_[i];
		const Matrix4x4 w = mech->GetJointWorldMatrix(m.bone);
		const Vector3 rotation = { DegToRad(m.rotationDegrees.x), DegToRad(m.rotationDegrees.y), DegToRad(m.rotationDegrees.z) };
		const Vector3 offset = followBody_ ? TransformMatrix(m.offset, body) : m.offset;
		const Vector3 pos = { w.m[3][0] + offset.x, w.m[3][1] + offset.y, w.m[3][2] + offset.z };
		// 大きさが変わったら今のを止めて、別の大きさを再生し直す
		if (sizes[i] != playing_[i] || (sizes[i] != Size::Off && !em->IsAlive(handles_[i]))) {
			if (handles_[i] != kInvalidEffectHandle) em->Stop(handles_[i]);
			handles_[i] = sizes[i] == Size::Off ? kInvalidEffectHandle : em->Play(EffectName(sizes[i]), pos);
			playing_[i] = sizes[i];
		}
		if (handles_[i] == kInvalidEffectHandle) continue;
		em->SetPosition(handles_[i], pos);
		if (followBody_) {
			// 機体の向きで回した噴く向きへ（ずらしの角度 → 機体の回転の順）
			const Vector3 dir = TransformMatrix(TransformMatrix({ 0.0f, -1.0f, 0.0f }, MakeRotateMatrix(rotation)), body);
			em->SetRotation(handles_[i], EulerAimingDown(dir));
		} else {
			em->SetRotation(handles_[i], rotation);
		}
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

MechThrusters::Sizes MechThrusters::FromMoveAxis(float x, float y) const {
	return FromMove(y > moveThreshold_, x < -moveThreshold_, y < -moveThreshold_, x > moveThreshold_);
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
	moveThreshold_ = static_cast<float>(node["moveThreshold"].AsDouble(moveThreshold_));
	followBody_ = node["followBody"].AsBool(followBody_);
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
	node["moveThreshold"] = JsonValue(static_cast<double>(moveThreshold_));
	node["followBody"] = JsonValue(followBody_);
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
	ImGui::SliderFloat("Move Threshold (axis)", &moveThreshold_, 0.05f, 0.95f);
	ImGui::Checkbox("Follow Body Rotation", &followBody_);
	ImGui::Text("effects: %s / %s / %s", smallEffect_.c_str(), mediumEffect_.c_str(), largeEffect_.c_str());
	for (int i = 0; i < kCount; ++i) {
		ImGui::PushID(i);
		ImGui::TextDisabled("%s (%s)", kMountNames[i], mounts_[i].bone.c_str());
		ImGui::DragFloat3("Offset (m)", &mounts_[i].offset.x, 0.02f);
		ImGui::DragFloat3("Rotation (deg)", &mounts_[i].rotationDegrees.x, 0.5f);
		ImGui::PopID();
	}
#endif // _DEBUG
}
