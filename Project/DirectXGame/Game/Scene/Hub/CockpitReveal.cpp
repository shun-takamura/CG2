#include "CockpitReveal.h"

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

	float SmoothStep01(float t) {
		t = Clamp01(t);
		return t * t * (3.0f - 2.0f * t);
	}

	// 線形の上げ下げ：rise の間に 0→1、hold の間 1、fall の間に 1→0
	float Trapezoid(float t, float riseStart, float riseEnd, float fallStart, float fallEnd) {
		if (t < riseStart || t >= fallEnd) return 0.0f;
		if (t < riseEnd) return (t - riseStart) / (std::max)(riseEnd - riseStart, 1e-4f);
		if (t < fallStart) return 1.0f;
		return 1.0f - (t - fallStart) / (std::max)(fallEnd - fallStart, 1e-4f);
	}
}

CockpitReveal::CockpitReveal() {
	ResetToDefaults();
}

void CockpitReveal::ResetToDefaults() {
	// フェーズと時刻は触らない（ImGui から再生中に戻しても止まらない）
	idleLightLevel_ = 0.25f;
	lockTime_ = 0.15f;
	signHideTime_ = 0.75f;
	signHideSeconds_ = 0.2f;
	lightsOnTime_ = 0.25f;
	lightsOnSeconds_ = 0.6f;
	hexStartTime_ = 1.0f;
	hexSeconds_ = 1.4f;
	backgroundTime_ = 2.5f;
	blurRiseSeconds_ = 0.15f;
	menuTime_ = 2.8f;
	blurFallSeconds_ = 0.2f;
	blurMax_ = 0.035f;
	inputDelay_ = 0.35f;
	collapseSeconds_ = 0.4f;
	hexCloseStartTime_ = 0.3f;
	hexCloseSeconds_ = 0.6f;
	signShowTime_ = 0.9f;
	lightsOffTime_ = 0.9f;
	lightsOffSeconds_ = 0.3f;
	shutdownEndTime_ = 1.3f;
	arriveLightsOnTime_ = 0.4f;
	arriveSignTime_ = 0.7f;
	arriveLightsDownTime_ = 1.5f;
	arriveEndTime_ = 1.9f;
}

void CockpitReveal::Reset() {
	phase_ = Phase::Idle;
	time_ = 0.0f;
}

void CockpitReveal::StartBoot() {
	phase_ = Phase::Booting;
	time_ = 0.0f;
}

void CockpitReveal::StartShutdown() {
	phase_ = Phase::ShuttingDown;
	time_ = 0.0f;
}

void CockpitReveal::StartArrive() {
	phase_ = Phase::Arriving;
	time_ = 0.0f;
}

void CockpitReveal::Update(float deltaTime) {
	if (phase_ == Phase::Idle || phase_ == Phase::Off) return;
	time_ += deltaTime;
	if ((phase_ == Phase::ShuttingDown && time_ >= shutdownEndTime_) ||
		(phase_ == Phase::Arriving && time_ >= arriveEndTime_)) {
		phase_ = Phase::Off;
	}
}

float CockpitReveal::Flicker(float t, float seconds) {
	if (t < 0.0f) return 0.0f;
	if (seconds <= 0.0f || t >= seconds) return 1.0f;
	// 点く → 消える → 少し点く → 消えかける → だんだん点き切る
	const float u = t / seconds;
	if (u < 0.10f) return 0.7f;
	if (u < 0.22f) return 0.0f;
	if (u < 0.30f) return 0.9f;
	if (u < 0.45f) return 0.15f;
	return 0.5f + 0.5f * SmoothStep01((u - 0.45f) / 0.55f);
}

CockpitReveal::State CockpitReveal::Evaluate() const {
	State s{};
	switch (phase_) {
	case Phase::Idle:
		s.lightLevel = idleLightLevel_;
		break;

	case Phase::Booting: {
		const float t = time_;
		s.signLocked = t >= lockTime_;
		// 六角形が広がる前に消す（点くときのチカチカを裏返して消える）
		s.signAlpha = 1.0f - Flicker(t - signHideTime_, signHideSeconds_);
		// 閉じ切りで一度落ちて、チカチカしながら点く
		const float idle = idleLightLevel_ * (1.0f - SmoothStep01(t / 0.15f));
		s.lightLevel = (std::max)(idle, Flicker(t - lightsOnTime_, lightsOnSeconds_));
		s.reveal = SmoothStep01((t - hexStartTime_) / (std::max)(hexSeconds_, 1e-4f));
		// ラジアルブラー：背景を出す直前に線形に上げ、文字を出すときに線形に下げる
		s.blurWidth = blurMax_ * Trapezoid(t, backgroundTime_ - blurRiseSeconds_, backgroundTime_,
			menuTime_, menuTime_ + blurFallSeconds_);
		s.backgroundAlpha = Flicker(t - backgroundTime_, 0.25f);
		s.menuTime = t - menuTime_;
		s.inputEnabled = t >= menuTime_ + inputDelay_;
		break;
	}

	case Phase::ShuttingDown:
	case Phase::Off: {
		const float t = (phase_ == Phase::Off) ? shutdownEndTime_ : time_;
		s.collapse = Clamp01(t / (std::max)(collapseSeconds_, 1e-4f));
		s.backgroundAlpha = 1.0f;
		s.menuTime = 1000.0f;   // メニューは出し切った状態のまま潰す
		s.reveal = 1.0f - SmoothStep01((t - hexCloseStartTime_) / (std::max)(hexCloseSeconds_, 1e-4f));
		// ハッチはまだ閉じているので LOCKED のまま。外を映す壁が戻ったら出す（UNLOCK はハッチが開くとき）
		s.signLocked = true;
		s.signAlpha = Flicker(t - signShowTime_, signHideSeconds_);
		// 落ちる途中で一度だけ点き直す
		const float off = Clamp01((t - lightsOffTime_) / (std::max)(lightsOffSeconds_, 1e-4f));
		const float blink = (off > 0.35f && off < 0.5f) ? 0.4f : 0.0f;
		s.lightLevel = (std::max)(idleLightLevel_, 1.0f - off) * (1.0f - blink);
		break;
	}

	case Phase::Arriving: {
		// 真っ暗 → 灯りがチカチカして点く → LOCKED が出る → シャットダウンの終わりと同じく待機の明るさへ落ちる。
		// 終わった所（Off）の見え方とつながるので、そのまま降りる（ハッチが開いて UNLOCK）へ進める
		const float t = time_;
		s.backgroundAlpha = 0.0f;
		s.collapse = 1.0f;   // ステージ選択の UI は出さない
		s.signLocked = true;
		s.signAlpha = Flicker(t - arriveSignTime_, signHideSeconds_);
		const float on = Flicker(t - arriveLightsOnTime_, lightsOnSeconds_);
		const float off = Clamp01((t - arriveLightsDownTime_) / (std::max)(lightsOffSeconds_, 1e-4f));
		const float blink = (off > 0.35f && off < 0.5f) ? 0.4f : 0.0f;
		s.lightLevel = (std::min)(on, (std::max)(idleLightLevel_, 1.0f - off) * (1.0f - blink));
		// 外の光も真っ暗の間は 0（閉じたコクピットの壁をうっすら照らさない）
		s.sunScale = SmoothStep01((t - arriveLightsOnTime_) / (std::max)(lightsOnSeconds_, 1e-4f));
		break;
	}
	}
	return s;
}

void CockpitReveal::LoadTuning(const JsonValue& node) {
	if (!node.IsObject()) return;
	auto readFloat = [&node](const char* key, float& value) {
		value = static_cast<float>(node[key].AsDouble(value));
	};
	readFloat("idleLightLevel", idleLightLevel_);
	readFloat("lockTime", lockTime_);
	readFloat("signHideTime", signHideTime_);
	readFloat("signHideSeconds", signHideSeconds_);
	readFloat("lightsOnTime", lightsOnTime_);
	readFloat("lightsOnSeconds", lightsOnSeconds_);
	readFloat("hexStartTime", hexStartTime_);
	readFloat("hexSeconds", hexSeconds_);
	readFloat("backgroundTime", backgroundTime_);
	readFloat("blurRiseSeconds", blurRiseSeconds_);
	readFloat("menuTime", menuTime_);
	readFloat("blurFallSeconds", blurFallSeconds_);
	readFloat("blurMax", blurMax_);
	readFloat("inputDelay", inputDelay_);
	readFloat("collapseSeconds", collapseSeconds_);
	readFloat("hexCloseStartTime", hexCloseStartTime_);
	readFloat("hexCloseSeconds", hexCloseSeconds_);
	readFloat("signShowTime", signShowTime_);
	readFloat("lightsOffTime", lightsOffTime_);
	readFloat("lightsOffSeconds", lightsOffSeconds_);
	readFloat("shutdownEndTime", shutdownEndTime_);
	readFloat("arriveLightsOnTime", arriveLightsOnTime_);
	readFloat("arriveSignTime", arriveSignTime_);
	readFloat("arriveLightsDownTime", arriveLightsDownTime_);
	readFloat("arriveEndTime", arriveEndTime_);
}

void CockpitReveal::SaveTuning(JsonValue& node) const {
	node = JsonValue::MakeObject();
	auto write = [&node](const char* key, float value) {
		node[key] = JsonValue(static_cast<double>(value));
	};
	write("idleLightLevel", idleLightLevel_);
	write("lockTime", lockTime_);
	write("signHideTime", signHideTime_);
	write("signHideSeconds", signHideSeconds_);
	write("lightsOnTime", lightsOnTime_);
	write("lightsOnSeconds", lightsOnSeconds_);
	write("hexStartTime", hexStartTime_);
	write("hexSeconds", hexSeconds_);
	write("backgroundTime", backgroundTime_);
	write("blurRiseSeconds", blurRiseSeconds_);
	write("menuTime", menuTime_);
	write("blurFallSeconds", blurFallSeconds_);
	write("blurMax", blurMax_);
	write("inputDelay", inputDelay_);
	write("collapseSeconds", collapseSeconds_);
	write("hexCloseStartTime", hexCloseStartTime_);
	write("hexCloseSeconds", hexCloseSeconds_);
	write("signShowTime", signShowTime_);
	write("lightsOffTime", lightsOffTime_);
	write("lightsOffSeconds", lightsOffSeconds_);
	write("shutdownEndTime", shutdownEndTime_);
	write("arriveLightsOnTime", arriveLightsOnTime_);
	write("arriveSignTime", arriveSignTime_);
	write("arriveLightsDownTime", arriveLightsDownTime_);
	write("arriveEndTime", arriveEndTime_);
}

void CockpitReveal::OnImGui() {
#ifdef _DEBUG
	static const char* kPhaseNames[] = { "Idle", "Booting", "ShuttingDown", "Off", "Arriving" };
	ImGui::Text("Phase: %s  t=%.2f s", kPhaseNames[static_cast<int>(phase_)], time_);
	if (ImGui::Button("Reset To Defaults")) ResetToDefaults();

	ImGui::SeparatorText("Idle");
	ImGui::DragFloat("Idle Light", &idleLightLevel_, 0.01f, 0.0f, 1.0f);

	ImGui::SeparatorText("Boot (s from hatch closed)");
	ImGui::DragFloat("Lock", &lockTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Sign Hide", &signHideTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Sign Hide (s)", &signHideSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Lights On", &lightsOnTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Lights On (s)", &lightsOnSeconds_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Hex Start", &hexStartTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Hex (s)", &hexSeconds_, 0.01f, 0.05f, 10.0f);
	ImGui::DragFloat("Background", &backgroundTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Blur Rise (s)", &blurRiseSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Menu", &menuTime_, 0.01f, 0.0f, 10.0f);
	ImGui::DragFloat("Blur Fall (s)", &blurFallSeconds_, 0.01f, 0.0f, 2.0f);
	ImGui::DragFloat("Blur Max", &blurMax_, 0.001f, 0.0f, 0.3f);
	ImGui::DragFloat("Input Delay (s)", &inputDelay_, 0.01f, 0.0f, 3.0f);

	ImGui::SeparatorText("Shutdown (s from start)");
	ImGui::DragFloat("Collapse (s)", &collapseSeconds_, 0.01f, 0.05f, 3.0f);
	ImGui::DragFloat("Hex Close Start", &hexCloseStartTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Hex Close (s)", &hexCloseSeconds_, 0.01f, 0.05f, 5.0f);
	ImGui::DragFloat("Sign Show", &signShowTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Lights Off", &lightsOffTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Lights Off (s)", &lightsOffSeconds_, 0.01f, 0.0f, 3.0f);
	ImGui::DragFloat("End", &shutdownEndTime_, 0.01f, 0.0f, 5.0f);

	ImGui::SeparatorText("Arrive (s from ink cleared, 18-7)");
	ImGui::DragFloat("Arrive Lights On", &arriveLightsOnTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Arrive Sign", &arriveSignTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Arrive Lights Down", &arriveLightsDownTime_, 0.01f, 0.0f, 5.0f);
	ImGui::DragFloat("Arrive End", &arriveEndTime_, 0.01f, 0.0f, 5.0f);
	ImGui::TextDisabled("Flicker uses Lights On (s) / Sign Hide (s); fall uses Lights Off (s).");
#endif // _DEBUG
}
