#include "HubLoadoutSequence.h"

#include "MathUtility.h"
#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}
	float SmoothStep(float t) {
		t = Clamp01(t);
		return t * t * (3.0f - 2.0f * t);
	}
}

HubLoadoutSequence::HubLoadoutSequence() {
	ResetToDefaults();
}

void HubLoadoutSequence::ResetToDefaults() {
	consoleCollapseSeconds_ = 0.35f;
	cutTime_ = 0.45f;
	cutRise_ = 0.2f;
	cutFall_ = 0.3f;
	templeHideTime_ = 1.05f;
	templeRise_ = 0.35f;
	templeFall_ = 0.35f;
	uiDelay_ = 0.25f;
	inputDelay_ = 0.35f;
	uiCollapseSeconds_ = 0.3f;
	templeShowTime_ = 0.55f;
	leaveCutTime_ = 1.05f;
	consoleReturnDelay_ = 0.1f;
	swapPeakTime_ = 0.12f;
	swapFall_ = 0.3f;
	swapIntensity_ = 0.85f;
	cutIntensity_ = 1.0f;
	templeIntensity_ = 1.0f;
	glitchParams_ = GlitchEffect::Params{};
	shots_[0] = { 35.0f, 4.0f, 30.0f, 10.5f };    // メイン武器：右前
	shots_[1] = { -35.0f, 4.0f, 30.0f, 10.5f };   // 盾：左前
	shots_[2] = { 180.0f, 8.0f, 30.0f, 11.5f };   // 飛行ユニット：真後ろ
	tweenSeconds_ = 0.6f;
	fovYDegrees_ = 40.0f;
	orbitStickSpeed_ = 120.0f;
	orbitDragSpeed_ = 0.3f;
	floorRingRadius_ = 7.0f;
}

void HubLoadoutSequence::StartEnter() {
	phase_ = Phase::Entering;
	time_ = 0.0f;
	uiTime_ = -1.0f;
	swapTime_ = -1.0f;
	swapPeakPending_ = false;
	SetShot(0, true);
}

void HubLoadoutSequence::StartLeave() {
	if (phase_ != Phase::Browsing) return;
	phase_ = Phase::Leaving;
	time_ = 0.0f;
}

void HubLoadoutSequence::StartSwap() {
	// 山のてっぺんの前ならそのまま（てっぺんで最新の装備に入れ替える）
	if (swapTime_ >= 0.0f && swapTime_ < swapPeakTime_) return;
	swapTime_ = 0.0f;
}

void HubLoadoutSequence::Stop() {
	phase_ = Phase::Idle;
	time_ = 0.0f;
	uiTime_ = -1.0f;
	swapTime_ = -1.0f;
	swapPeakPending_ = false;
}

void HubLoadoutSequence::Update(float deltaTime) {
	if (phase_ == Phase::Idle || phase_ == Phase::Finished) return;
	time_ += deltaTime;
	glitchTime_ += deltaTime;

	switch (phase_) {
	case Phase::Entering:
		uiTime_ = time_ - EnterUiStart();
		if (uiTime_ >= 0.0f) phase_ = Phase::Browsing;
		break;
	case Phase::Browsing:
		uiTime_ += deltaTime;
		break;
	case Phase::Leaving:
		uiTime_ += deltaTime;
		if (time_ >= LeaveEnd()) phase_ = Phase::Finished;
		break;
	default:
		break;
	}

	// タブのカメラへ
	if (tweenTime_ >= 0.0f) {
		tweenTime_ += deltaTime;
		const float k = tweenSeconds_ > 0.0f ? SmoothStep(tweenTime_ / tweenSeconds_) : 1.0f;
		camera_ = LerpShot(tweenFrom_, tweenTo_, k);
		if (k >= 1.0f) tweenTime_ = -1.0f;
	}

	if (swapTime_ >= 0.0f) {
		const float before = swapTime_;
		swapTime_ += deltaTime;
		if (before < swapPeakTime_ && swapTime_ >= swapPeakTime_) swapPeakPending_ = true;
		if (swapTime_ > swapPeakTime_ + swapFall_) swapTime_ = -1.0f;
	}
}

bool HubLoadoutSequence::ConsumeSwapPeak() {
	const bool peak = swapPeakPending_;
	swapPeakPending_ = false;
	return peak;
}

float HubLoadoutSequence::Bump(float t, float center, float rise, float fall, float peak) {
	if (t < center) {
		if (rise <= 0.0f) return 0.0f;
		return peak * SmoothStep(1.0f - (center - t) / rise);
	}
	if (fall <= 0.0f) return t == center ? peak : 0.0f;
	return peak * SmoothStep(1.0f - (t - center) / fall);
}

HubLoadoutSequence::View HubLoadoutSequence::Evaluate() const {
	View v{};
	const float t = time_;
	float screenBump = 0.0f;
	float templeBump = 0.0f;

	switch (phase_) {
	case Phase::Entering:
		v.consoleCollapse = consoleCollapseSeconds_ > 0.0f ? Clamp01(t / consoleCollapseSeconds_) : 1.0f;
		v.exterior = t >= cutTime_;
		v.templeVisible = t < templeHideTime_;
		v.uiTime = uiTime_;
		screenBump = Bump(t, cutTime_, cutRise_, cutFall_, cutIntensity_);
		templeBump = Bump(t, templeHideTime_, templeRise_, templeFall_, templeIntensity_);
		break;
	case Phase::Browsing:
		v.consoleCollapse = 1.0f;
		v.exterior = true;
		v.templeVisible = false;
		v.uiTime = uiTime_;
		v.inputEnabled = uiTime_ >= inputDelay_;
		break;
	case Phase::Leaving:
		v.uiTime = uiTime_;
		v.uiCollapse = uiCollapseSeconds_ > 0.0f ? Clamp01(t / uiCollapseSeconds_) : 1.0f;
		v.templeVisible = t >= templeShowTime_;
		v.exterior = t < leaveCutTime_;
		v.consoleCollapse = consoleCollapseSeconds_ > 0.0f
			? 1.0f - Clamp01((t - leaveCutTime_ - consoleReturnDelay_) / consoleCollapseSeconds_) : 0.0f;
		screenBump = Bump(t, leaveCutTime_, cutRise_, cutFall_, cutIntensity_);
		templeBump = Bump(t, templeShowTime_, templeRise_, templeFall_, templeIntensity_);
		break;
	case Phase::Finished:
		v.templeVisible = true;
		break;
	default:
		break;
	}

	// 切り替えの山は画面全体、神殿の山は機体を外して神殿だけに
	if (screenBump >= templeBump) {
		v.glitch = screenBump;
		v.glitchMask = GlitchEffect::MaskMode::Screen;
	} else {
		v.glitch = templeBump;
		v.glitchMask = GlitchEffect::MaskMode::Outside;
	}
	// 付け替えは機体だけ
	if (swapTime_ >= 0.0f) {
		const float swap = Bump(swapTime_, swapPeakTime_, swapPeakTime_, swapFall_, swapIntensity_);
		if (swap > v.glitch) {
			v.glitch = swap;
			v.glitchMask = GlitchEffect::MaskMode::Inside;
		}
	}
	return v;
}

void HubLoadoutSequence::Orbit(float yawDegrees) {
	if (yawDegrees == 0.0f) return;
	tweenTime_ = -1.0f;
	camera_.yawDegrees += yawDegrees;
}

void HubLoadoutSequence::SetShot(int tab, bool immediate) {
	const Shot& target = shots_[std::clamp(tab, 0, kShotCount - 1)];
	if (immediate) {
		camera_ = target;
		tweenTime_ = -1.0f;
		return;
	}
	// 回り込む角度は差を -180..180 に詰めて、近い方を回る（ドラッグで何周回っていても）
	tweenFrom_ = camera_;
	tweenTo_ = target;
	float delta = std::fmod(target.yawDegrees - camera_.yawDegrees + 180.0f, 360.0f);
	if (delta < 0.0f) delta += 360.0f;
	tweenTo_.yawDegrees = camera_.yawDegrees + delta - 180.0f;
	tweenTime_ = 0.0f;
}

HubLoadoutSequence::Shot HubLoadoutSequence::LerpShot(const Shot& a, const Shot& b, float k) {
	return { a.yawDegrees + (b.yawDegrees - a.yawDegrees) * k, a.pitchDegrees + (b.pitchDegrees - a.pitchDegrees) * k,
		a.distance + (b.distance - a.distance) * k, a.height + (b.height - a.height) * k };
}

void HubLoadoutSequence::GetCamera(const Vector3& mechOrigin, Vector3& eye, Vector3& rotate, float& fovY) const {
	// 機体の正面（+Z）から yaw だけ回り込んだ所から、見る点を向く。エンジンの向き (pitch, yaw) の前は
	// (sin yaw cos pitch, -sin pitch, cos yaw cos pitch) なので、機体の方を向くには yaw に π を足す
	const float yaw = DegToRad(camera_.yawDegrees);
	const float pitch = DegToRad(camera_.pitchDegrees);
	const float distance = camera_.distance;
	const Vector3 target = { mechOrigin.x, mechOrigin.y + camera_.height, mechOrigin.z };
	eye = { target.x + std::sin(yaw) * std::cos(pitch) * distance,
		target.y + std::sin(pitch) * distance,
		target.z + std::cos(yaw) * std::cos(pitch) * distance };
	rotate = { pitch, yaw + kPi, 0.0f };
	fovY = DegToRad(fovYDegrees_);
}

void HubLoadoutSequence::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto read = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	read("consoleCollapseSeconds", consoleCollapseSeconds_);
	read("cutTime", cutTime_);
	read("cutRise", cutRise_);
	read("cutFall", cutFall_);
	read("templeHideTime", templeHideTime_);
	read("templeRise", templeRise_);
	read("templeFall", templeFall_);
	read("uiDelay", uiDelay_);
	read("inputDelay", inputDelay_);
	read("uiCollapseSeconds", uiCollapseSeconds_);
	read("templeShowTime", templeShowTime_);
	read("leaveCutTime", leaveCutTime_);
	read("consoleReturnDelay", consoleReturnDelay_);
	read("swapPeakTime", swapPeakTime_);
	read("swapFall", swapFall_);
	read("swapIntensity", swapIntensity_);
	read("cutIntensity", cutIntensity_);
	read("templeIntensity", templeIntensity_);
	read("tweenSeconds", tweenSeconds_);
	read("fovYDegrees", fovYDegrees_);
	const JsonValue& shots = node["shots"];
	if (shots.IsArray()) {
		for (size_t i = 0; i < shots.Size() && i < shots_.size(); ++i) {
			const JsonValue& s = shots[i];
			if (!s.IsObject()) continue;
			shots_[i].yawDegrees = static_cast<float>(s["yawDegrees"].AsDouble(shots_[i].yawDegrees));
			shots_[i].pitchDegrees = static_cast<float>(s["pitchDegrees"].AsDouble(shots_[i].pitchDegrees));
			shots_[i].distance = static_cast<float>(s["distance"].AsDouble(shots_[i].distance));
			shots_[i].height = static_cast<float>(s["height"].AsDouble(shots_[i].height));
		}
	}
	read("orbitStickSpeed", orbitStickSpeed_);
	read("orbitDragSpeed", orbitDragSpeed_);
	read("floorRingRadius", floorRingRadius_);
	const JsonValue& g = node["glitch"];
	if (g.IsObject()) {
		auto readG = [&g](const char* key, float& value) {
			value = static_cast<float>(g[key].AsDouble(value));
		};
		readG("bandSlope", glitchParams_.bandSlope);
		readG("bandCount", glitchParams_.bandCount);
		readG("bandChance", glitchParams_.bandChance);
		readG("maxShift", glitchParams_.maxShift);
		readG("rgbSplit", glitchParams_.rgbSplit);
		readG("blockSize", glitchParams_.blockSize);
		readG("blockChance", glitchParams_.blockChance);
		readG("tintStrength", glitchParams_.tintStrength);
		readG("jitterInterval", glitchParams_.jitterInterval);
	}
}

void HubLoadoutSequence::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("consoleCollapseSeconds", consoleCollapseSeconds_);
	write("cutTime", cutTime_);
	write("cutRise", cutRise_);
	write("cutFall", cutFall_);
	write("templeHideTime", templeHideTime_);
	write("templeRise", templeRise_);
	write("templeFall", templeFall_);
	write("uiDelay", uiDelay_);
	write("inputDelay", inputDelay_);
	write("uiCollapseSeconds", uiCollapseSeconds_);
	write("templeShowTime", templeShowTime_);
	write("leaveCutTime", leaveCutTime_);
	write("consoleReturnDelay", consoleReturnDelay_);
	write("swapPeakTime", swapPeakTime_);
	write("swapFall", swapFall_);
	write("swapIntensity", swapIntensity_);
	write("cutIntensity", cutIntensity_);
	write("templeIntensity", templeIntensity_);
	write("tweenSeconds", tweenSeconds_);
	write("fovYDegrees", fovYDegrees_);
	JsonValue shots = JsonValue::MakeArray();
	for (const Shot& shot : shots_) {
		JsonValue s = JsonValue::MakeObject();
		s["yawDegrees"] = JsonValue(static_cast<double>(shot.yawDegrees));
		s["pitchDegrees"] = JsonValue(static_cast<double>(shot.pitchDegrees));
		s["distance"] = JsonValue(static_cast<double>(shot.distance));
		s["height"] = JsonValue(static_cast<double>(shot.height));
		shots.Push(s);
	}
	node["shots"] = shots;
	write("orbitStickSpeed", orbitStickSpeed_);
	write("orbitDragSpeed", orbitDragSpeed_);
	write("floorRingRadius", floorRingRadius_);
	JsonValue g = JsonValue::MakeObject();
	auto writeG = [&g](const char* key, float value) {
		g[key] = JsonValue(static_cast<double>(value));
	};
	writeG("bandSlope", glitchParams_.bandSlope);
	writeG("bandCount", glitchParams_.bandCount);
	writeG("bandChance", glitchParams_.bandChance);
	writeG("maxShift", glitchParams_.maxShift);
	writeG("rgbSplit", glitchParams_.rgbSplit);
	writeG("blockSize", glitchParams_.blockSize);
	writeG("blockChance", glitchParams_.blockChance);
	writeG("tintStrength", glitchParams_.tintStrength);
	writeG("jitterInterval", glitchParams_.jitterInterval);
	node["glitch"] = g;
}

void HubLoadoutSequence::OnImGui() {
#ifdef _DEBUG
	static const char* kPhaseNames[] = { "Idle", "Entering", "Browsing", "Leaving", "Finished" };
	ImGui::Text("Phase: %s  t=%.2f  ui=%.2f", kPhaseNames[static_cast<int>(phase_)], time_, uiTime_);
	if (ImGui::Button("Reset To Defaults##Loadout")) ResetToDefaults();
	ImGui::SeparatorText("Enter");
	ImGui::DragFloat("Console Collapse (s)", &consoleCollapseSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Cut Time (s)", &cutTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Cut Rise (s)", &cutRise_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Cut Fall (s)", &cutFall_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Cut Intensity", &cutIntensity_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("Temple Hide Time (s)", &templeHideTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Temple Rise (s)", &templeRise_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Temple Fall (s)", &templeFall_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Temple Intensity", &templeIntensity_, 0.01f, 0.0f, 1.0f);
	ImGui::DragFloat("UI Delay (s)", &uiDelay_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Input Delay (s)", &inputDelay_, 0.01f, 0.0f, 3.0f);
	ImGui::SeparatorText("Leave");
	ImGui::DragFloat("UI Collapse (s)", &uiCollapseSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Temple Show Time (s)", &templeShowTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Leave Cut Time (s)", &leaveCutTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Console Return Delay (s)", &consoleReturnDelay_, 0.01f, 0.0f, 3.0f);
	ImGui::SeparatorText("Swap");
	ImGui::DragFloat("Swap Peak (s)", &swapPeakTime_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Swap Fall (s)", &swapFall_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Swap Intensity", &swapIntensity_, 0.01f, 0.0f, 1.0f);
	ImGui::SeparatorText("Glitch Look");
	ImGui::SliderFloat("Band Slope", &glitchParams_.bandSlope, -3.0f, 3.0f);
	ImGui::SliderFloat("Band Count", &glitchParams_.bandCount, 1.0f, 80.0f);
	ImGui::SliderFloat("Band Chance", &glitchParams_.bandChance, 0.0f, 1.0f);
	ImGui::SliderFloat("Max Shift", &glitchParams_.maxShift, 0.0f, 0.3f);
	ImGui::SliderFloat("RGB Split", &glitchParams_.rgbSplit, 0.0f, 0.05f);
	ImGui::SliderFloat("Block Size", &glitchParams_.blockSize, 4.0f, 256.0f);
	ImGui::SliderFloat("Block Chance", &glitchParams_.blockChance, 0.0f, 1.0f);
	ImGui::SliderFloat("Tint", &glitchParams_.tintStrength, 0.0f, 1.0f);
	ImGui::SliderFloat("Jitter Interval", &glitchParams_.jitterInterval, 0.01f, 0.3f);
	ImGui::SeparatorText("Camera");
	ImGui::DragFloat("FovY (deg)", &fovYDegrees_, 0.5f, 10.0f, 100.0f);
	ImGui::DragFloat("Tween (s)", &tweenSeconds_, 0.01f, 0.0f, 3.0f);
	static const char* kShotNames[kShotCount] = { "Main Weapon", "Shield", "Flight Unit" };
	for (int i = 0; i < kShotCount; ++i) {
		ImGui::PushID(i);
		if (ImGui::TreeNode(kShotNames[i])) {
			// 値を変えながら見るときは View で今のカメラをこの位置にする
			if (ImGui::Button("View")) SetShot(i, true);
			ImGui::DragFloat("Yaw (deg)", &shots_[i].yawDegrees, 0.5f, -360.0f, 360.0f);
			ImGui::DragFloat("Pitch (deg)", &shots_[i].pitchDegrees, 0.1f, -60.0f, 60.0f);
			ImGui::DragFloat("Distance (m)", &shots_[i].distance, 0.1f, 1.0f, 200.0f);
			ImGui::DragFloat("Height (m)", &shots_[i].height, 0.1f, 0.0f, 30.0f);
			ImGui::TreePop();
		}
		ImGui::PopID();
	}
	ImGui::Text("Camera: yaw %.1f  pitch %.1f  dist %.1f  h %.1f", camera_.yawDegrees, camera_.pitchDegrees,
		camera_.distance, camera_.height);
	ImGui::DragFloat("Orbit Stick Speed (deg/s)", &orbitStickSpeed_, 1.0f, 0.0f, 720.0f);
	ImGui::DragFloat("Orbit Drag Speed (deg/px)", &orbitDragSpeed_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("Floor Ring Radius (m)", &floorRingRadius_, 0.1f, 0.0f, 50.0f);
#endif // _DEBUG
}
