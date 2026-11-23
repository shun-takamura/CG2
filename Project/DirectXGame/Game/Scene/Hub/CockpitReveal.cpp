#include "CockpitReveal.h"

#include "Json/JsonValue.h"
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// smoothstep（3t² − 2t³）の 2 乗の項の係数
	constexpr float kSmoothStepSquareCoef = 3.0f;
	// smoothstep（3t² − 2t³）の 3 乗の項の係数
	constexpr float kSmoothStepCubeCoef = 2.0f;
	// 待機中の灯りの明るさの初期値
	constexpr float kDefaultIdleLightLevel = 0.25f;
	// 表示板が LOCKED になる時刻の初期値 [秒]
	constexpr float kDefaultLockTime = 0.15f;
	// 表示板を消し始める時刻の初期値 [秒]
	constexpr float kDefaultSignHideTime = 0.75f;
	// 表示板がチカチカして消える長さの初期値 [秒]
	constexpr float kDefaultSignHideSeconds = 0.2f;
	// 灯りが点き始める時刻の初期値 [秒]
	constexpr float kDefaultLightsOnTime = 0.25f;
	// 灯りがチカチカして点き切る長さの初期値 [秒]
	constexpr float kDefaultLightsOnSeconds = 0.6f;
	// 六角形が広がり切る長さの初期値 [秒]
	constexpr float kDefaultHexSeconds = 1.4f;
	// メニューが出始める時刻の初期値 [秒]
	constexpr float kDefaultMenuTime = 1.1f;
	// メニューが出てから入力を受け付けるまでの初期値 [秒]
	constexpr float kDefaultInputDelay = 0.15f;
	// UI がたたまれる長さの初期値 [秒]
	constexpr float kDefaultCollapseSeconds = 0.4f;
	// 六角形が閉じ始める時刻の初期値 [秒]
	constexpr float kDefaultHexCloseStartTime = 0.3f;
	// 六角形が閉じ切る長さの初期値 [秒]
	constexpr float kDefaultHexCloseSeconds = 0.6f;
	// シャットダウンで表示板を出す時刻の初期値 [秒]
	constexpr float kDefaultSignShowTime = 0.9f;
	// 灯りが落ち始める時刻の初期値 [秒]
	constexpr float kDefaultLightsOffTime = 0.9f;
	// 灯りが落ち切る長さの初期値 [秒]
	constexpr float kDefaultLightsOffSeconds = 0.3f;
	// シャットダウンが終わる時刻の初期値 [秒]
	constexpr float kDefaultShutdownEndTime = 1.3f;
	// 到着で灯りが点き始める時刻の初期値 [秒]
	constexpr float kDefaultArriveLightsOnTime = 0.4f;
	// 到着で LOCKED を出す時刻の初期値 [秒]
	constexpr float kDefaultArriveSignTime = 0.7f;
	// 到着で待機の明るさへ落ち始める時刻の初期値 [秒]
	constexpr float kDefaultArriveLightsDownTime = 1.5f;
	// 到着が終わる時刻の初期値 [秒]
	constexpr float kDefaultArriveEndTime = 1.9f;
	// チカチカ：最初に点く区間の終わり（進み 0〜1）
	constexpr float kFlickerFirstOnEnd = 0.10f;
	// チカチカ：最初に点く明るさ
	constexpr float kFlickerFirstOnLevel = 0.7f;
	// チカチカ：消える区間の終わり（進み 0〜1）
	constexpr float kFlickerOffEnd = 0.22f;
	// チカチカ：少し点く区間の終わり（進み 0〜1）
	constexpr float kFlickerSecondOnEnd = 0.30f;
	// チカチカ：少し点く明るさ
	constexpr float kFlickerSecondOnLevel = 0.9f;
	// チカチカ：消えかける区間の終わり＝点き切り始める進み（0〜1）
	constexpr float kFlickerDimEnd = 0.45f;
	// チカチカ：消えかける明るさ
	constexpr float kFlickerDimLevel = 0.15f;
	// チカチカ：だんだん点き切り始める明るさ
	constexpr float kFlickerRiseStartLevel = 0.5f;
	// チカチカ：だんだん点き切る間に上がる明るさの幅
	constexpr float kFlickerRiseRange = 0.5f;
	// チカチカ：だんだん点き切る区間の長さ（進み 0〜1）
	constexpr float kFlickerRiseSpan = 0.55f;
	// 起動で待機の灯りが落ち切る長さ [秒]
	constexpr float kIdleLightDropSeconds = 0.15f;
	// 時間の長さの下限 [秒]（0 で割らないように）
	constexpr float kMinSeconds = 1e-4f;
	// 背景がチカチカして出る長さ [秒]
	constexpr float kBackgroundFlickerSeconds = 0.25f;
	// メニューを出し切った状態にするための十分先の時刻 [秒]
	constexpr float kMenuFullyShownTime = 1000.0f;
	// 灯りが落ちる途中で点き直す区間の始まり（進み 0〜1）
	constexpr float kBlinkStart = 0.35f;
	// 灯りが落ちる途中で点き直す区間の終わり（進み 0〜1）
	constexpr float kBlinkEnd = 0.5f;
	// 点き直す区間で明るさを引く割合
	constexpr float kBlinkDim = 0.4f;

	/// <summary>0〜1 に切る</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 の値</returns>
	float Clamp01(float t) {
		return std::clamp(t, 0.0f, 1.0f);
	}

	/// <summary>0〜1 に切ってから smoothstep をかける</summary>
	/// <param name="t">入力</param>
	/// <returns>0〜1 のなめらかな値</returns>
	float SmoothStep01(float t) {
		t = Clamp01(t);
		return t * t * (kSmoothStepSquareCoef - kSmoothStepCubeCoef * t);
	}

	// 線形の上げ下げ：rise の間に 0→1、hold の間 1、fall の間に 1→0
}

CockpitReveal::CockpitReveal() {
	ResetToDefaults();
}

void CockpitReveal::ResetToDefaults() {
	// フェーズと時刻は触らない（ImGui から再生中に戻しても止まらない）
	idleLightLevel_ = kDefaultIdleLightLevel;
	lockTime_ = kDefaultLockTime;
	signHideTime_ = kDefaultSignHideTime;
	signHideSeconds_ = kDefaultSignHideSeconds;
	lightsOnTime_ = kDefaultLightsOnTime;
	lightsOnSeconds_ = kDefaultLightsOnSeconds;
	hexStartTime_ = 1.0f;
	hexSeconds_ = kDefaultHexSeconds;
	backgroundTime_ = 1.0f;
	menuTime_ = kDefaultMenuTime;
	inputDelay_ = kDefaultInputDelay;
	collapseSeconds_ = kDefaultCollapseSeconds;
	hexCloseStartTime_ = kDefaultHexCloseStartTime;
	hexCloseSeconds_ = kDefaultHexCloseSeconds;
	signShowTime_ = kDefaultSignShowTime;
	lightsOffTime_ = kDefaultLightsOffTime;
	lightsOffSeconds_ = kDefaultLightsOffSeconds;
	shutdownEndTime_ = kDefaultShutdownEndTime;
	arriveLightsOnTime_ = kDefaultArriveLightsOnTime;
	arriveSignTime_ = kDefaultArriveSignTime;
	arriveLightsDownTime_ = kDefaultArriveLightsDownTime;
	arriveEndTime_ = kDefaultArriveEndTime;
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
	if (u < kFlickerFirstOnEnd) return kFlickerFirstOnLevel;
	if (u < kFlickerOffEnd) return 0.0f;
	if (u < kFlickerSecondOnEnd) return kFlickerSecondOnLevel;
	if (u < kFlickerDimEnd) return kFlickerDimLevel;
	return kFlickerRiseStartLevel + kFlickerRiseRange * SmoothStep01((u - kFlickerDimEnd) / kFlickerRiseSpan);
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
		const float idle = idleLightLevel_ * (1.0f - SmoothStep01(t / kIdleLightDropSeconds));
		s.lightLevel = (std::max)(idle, Flicker(t - lightsOnTime_, lightsOnSeconds_));
		s.reveal = SmoothStep01((t - hexStartTime_) / (std::max)(hexSeconds_, kMinSeconds));
		s.backgroundAlpha = Flicker(t - backgroundTime_, kBackgroundFlickerSeconds);
		s.menuTime = t - menuTime_;
		s.inputEnabled = t >= menuTime_ + inputDelay_;
		break;
	}

	case Phase::ShuttingDown:
	case Phase::Off: {
		const float t = (phase_ == Phase::Off) ? shutdownEndTime_ : time_;
		s.collapse = Clamp01(t / (std::max)(collapseSeconds_, kMinSeconds));
		s.backgroundAlpha = 1.0f;
		s.menuTime = kMenuFullyShownTime;   // メニューは出し切った状態のまま潰す
		s.reveal = 1.0f - SmoothStep01((t - hexCloseStartTime_) / (std::max)(hexCloseSeconds_, kMinSeconds));
		// ハッチはまだ閉じているので LOCKED のまま。外を映す壁が戻ったら出す（UNLOCK はハッチが開くとき）
		s.signLocked = true;
		s.signAlpha = Flicker(t - signShowTime_, signHideSeconds_);
		// 落ちる途中で一度だけ点き直す
		const float off = Clamp01((t - lightsOffTime_) / (std::max)(lightsOffSeconds_, kMinSeconds));
		const float blink = (off > kBlinkStart && off < kBlinkEnd) ? kBlinkDim : 0.0f;
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
		const float off = Clamp01((t - arriveLightsDownTime_) / (std::max)(lightsOffSeconds_, kMinSeconds));
		const float blink = (off > kBlinkStart && off < kBlinkEnd) ? kBlinkDim : 0.0f;
		s.lightLevel = (std::min)(on, (std::max)(idleLightLevel_, 1.0f - off) * (1.0f - blink));
		// 外の光も真っ暗の間は 0（閉じたコクピットの壁をうっすら照らさない）
		s.sunScale = SmoothStep01((t - arriveLightsOnTime_) / (std::max)(lightsOnSeconds_, kMinSeconds));
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
	readFloat("menuTime", menuTime_);
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
	write("menuTime", menuTime_);
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
	ImGui::DragFloat("Menu", &menuTime_, 0.01f, 0.0f, 10.0f);
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
