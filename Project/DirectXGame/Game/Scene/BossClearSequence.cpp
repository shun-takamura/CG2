#include "BossClearSequence.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

#include "Camera.h"
#include "MathUtility.h"
#include "Text/TextRenderer.h"
#include "Vector2.h"
#include "Vector4.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// なめらかな補間 3x²−2x³ の x² の係数
	constexpr float kSmoothStepCubicCoeff = 3.0f;
	// これより小さい長さ・秒数は 0 とみなす（0 割りを避ける）
	constexpr float kNearZero = 1e-3f;
	// 小さな爆発の間隔の下限 [秒]（0 で無限に出ないように）
	constexpr float kMinBurstInterval = 0.02f;
	// xorshift32 の 1 回目の左シフト量 [bit]
	constexpr int kXorShiftShiftA = 13;
	// xorshift32 の右シフト量 [bit]
	constexpr int kXorShiftShiftB = 17;
	// xorshift32 の 2 回目の左シフト量 [bit]
	constexpr int kXorShiftShiftC = 5;
	// 乱数の下位 24 bit（float で正確に表せる範囲）を取るマスク兼最大値
	constexpr uint32_t kRand24BitMask = 0xFFFFFFu;
	// 見る向きの長さがこれ以下なら向きを決めない
	constexpr float kLookDirEpsilon = 1e-4f;
	// カメラの揺れ：X の速さ [rad/秒]
	constexpr float kShakeFreqX = 53.0f;
	// カメラの揺れ：Y の速さ [rad/秒]
	constexpr float kShakeFreqY = 61.0f;
	// カメラの揺れ：Y の位相のずれ [rad]
	constexpr float kShakePhaseY = 1.3f;
	// カメラの揺れ：Z の速さ [rad/秒]
	constexpr float kShakeFreqZ = 47.0f;
	// カメラの揺れ：Z の位相のずれ [rad]
	constexpr float kShakePhaseZ = 2.1f;
	// 崩壊のフラッシュは最初のフラッシュの何倍の長さで消すか
	constexpr float kCollapseFlashTimeScale = 1.5f;
	/// <summary>0〜1 に収める</summary>
	/// <param name="x">値</param>
	/// <returns>0〜1 に収めた値</returns>
	float Clamp01(float x) { return (std::clamp)(x, 0.0f, 1.0f); }
	/// <summary>0〜1 のなめらかな補間（3x²−2x³）</summary>
	/// <param name="x">進み具合（0〜1 に収める）</param>
	/// <returns>補間した値</returns>
	float SmoothStep01(float x) { x = Clamp01(x); return x * x * (kSmoothStepCubicCoeff - 2.0f * x); }
	/// <summary>3 次のイーズアウト</summary>
	/// <param name="x">進み具合（0〜1 に収める）</param>
	/// <returns>補間した値</returns>
	float EaseOutCubic(float x) { x = Clamp01(x); const float u = 1.0f - x; return 1.0f - u * u * u; }
	/// <summary>a から b へ、角度の近い回りで t だけ進める</summary>
	/// <param name="a">始めの角度 [rad]</param>
	/// <param name="b">目標の角度 [rad]</param>
	/// <param name="t">進める割合（0〜1）</param>
	/// <returns>補間した角度 [rad]</returns>
	float LerpAngle(float a, float b, float t) {
		float d = std::fmod(b - a, 2.0f * kPi);
		if (d > kPi) d -= 2.0f * kPi;
		if (d < -kPi) d += 2.0f * kPi;
		return a + d * t;
	}
}

void BossClearSequence::Begin(const Vector3& bossCenter, float groundY, const Camera& camera, float clearTimeSec) {
	stage_ = Stage::HitStop;
	time_ = 0.0f;
	stageTime_ = 0.0f;
	captureRequest_ = true;   // このフレーム（UI を描かない最初のフレーム）を撮る
	collapseEvent_ = false;
	finishedEvent_ = false;
	collapseTime_ = -1.0f;
	burstTimer_ = 0.0f;
	pending_.clear();
	banner_.Reset();
	bossCenter_ = bossCenter;
	groundY_ = groundY;
	clearTime_ = clearTimeSec;
	startEye_ = camera.GetTranslate();
	startRot_ = camera.GetRotate();

	// 寄る目の水平の向き：ボスから今のカメラへの向きを shotYawOffset_ だけ回す（真正面より斜め前）
	float dx = startEye_.x - bossCenter_.x;
	float dz = startEye_.z - bossCenter_.z;
	const float len = std::sqrt(dx * dx + dz * dz);
	if (len < kNearZero) { dx = 0.0f; dz = -1.0f; } else { dx /= len; dz /= len; }
	const float c = std::cos(shotYawOffset_), s = std::sin(shotYawOffset_);
	shotDir_ = { dx * c + dz * s, 0.0f, -dx * s + dz * c };
}

void BossClearSequence::Reset() {
	stage_ = Stage::Idle;
	time_ = 0.0f;
	stageTime_ = 0.0f;
	captureRequest_ = false;
	collapseEvent_ = false;
	finishedEvent_ = false;
	collapseTime_ = -1.0f;
	pending_.clear();
	banner_.Reset();
}

void BossClearSequence::Update(float uiDt) {
	if (stage_ == Stage::Idle) return;
	time_ += uiDt;
	stageTime_ += uiDt;

	switch (stage_) {
	case Stage::HitStop:
		if (stageTime_ >= hitStopTime_) {
			stage_ = Stage::Slow;
			stageTime_ = 0.0f;
			burstTimer_ = 0.0f;
		}
		break;
	case Stage::Slow:
		burstTimer_ -= uiDt;
		if (burstTimer_ <= 0.0f) {
			pending_.push_back({ time_, RandomAround(burstRadius_, burstHeight_), false });
			burstTimer_ += (std::max)(burstInterval_, kMinBurstInterval);
		}
		if (stageTime_ >= slowTime_) {
			stage_ = Stage::Collapse;
			stageTime_ = 0.0f;
			collapseEvent_ = true;
			collapseTime_ = time_;
			// 本体の大きな爆発（死亡エフェクト）はホストのボスの破棄で出る。周りの数発をずらして出す
			const int n = (std::max)(collapseBurstCount_, 0);
			for (int i = 0; i < n; ++i) {
				const float at = time_ + collapseBurstSpan_ * (static_cast<float>(i + 1) / static_cast<float>(n));
				pending_.push_back({ at, RandomAround(collapseBurstRadius_, burstHeight_), true });
			}
		}
		break;
	case Stage::Collapse:
		if (stageTime_ >= bannerDelay_) {
			stage_ = Stage::Banner;
			stageTime_ = 0.0f;
			banner_.Start(clearTime_);
		}
		break;
	case Stage::Banner:
		banner_.Update(uiDt);
		if (stageTime_ >= bannerHold_) {
			stage_ = Stage::Done;
			stageTime_ = 0.0f;
			finishedEvent_ = true;
		}
		break;
	case Stage::Done:
		banner_.Update(uiDt);
		break;
	default:
		break;
	}
}

float BossClearSequence::GetWorldTimeScale() const {
	switch (stage_) {
	case Stage::HitStop: return 0.0f;
	case Stage::Slow:    return slowScale_;
	case Stage::Collapse:
	case Stage::Banner:
	case Stage::Done: {
		if (collapseTime_ < 0.0f || restoreTime_ <= kNearZero) return 1.0f;
		const float p = SmoothStep01((time_ - collapseTime_) / restoreTime_);
		return slowScale_ + (1.0f - slowScale_) * p;
	}
	default: return 1.0f;
	}
}

bool BossClearSequence::ConsumeCaptureRequest() {
	if (!captureRequest_) return false;
	captureRequest_ = false;
	return true;
}

bool BossClearSequence::ConsumeCollapse() {
	if (!collapseEvent_) return false;
	collapseEvent_ = false;
	return true;
}

bool BossClearSequence::ConsumeFinished() {
	if (!finishedEvent_) return false;
	finishedEvent_ = false;
	return true;
}

bool BossClearSequence::PopBurst(Vector3& outPos, std::string& outEffect) {
	for (size_t i = 0; i < pending_.size(); ++i) {
		if (pending_[i].at <= time_) {
			outPos = pending_[i].pos;
			outEffect = pending_[i].big ? bigBurstEffect_ : smallBurstEffect_;
			pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(i));
			return true;
		}
	}
	return false;
}

float BossClearSequence::Rand01() {
	// xorshift32（演出の散らばりだけなので軽いもので足りる）
	rng_ ^= rng_ << kXorShiftShiftA;
	rng_ ^= rng_ >> kXorShiftShiftB;
	rng_ ^= rng_ << kXorShiftShiftC;
	return static_cast<float>(rng_ & kRand24BitMask) / static_cast<float>(kRand24BitMask);
}

Vector3 BossClearSequence::RandomAround(float radius, float height) {
	const float a = Rand01() * 2.0f * kPi;
	const float r = radius * std::sqrt(Rand01());
	const float h = (Rand01() * 2.0f - 1.0f) * height;
	return { bossCenter_.x + std::cos(a) * r, bossCenter_.y + h, bossCenter_.z + std::sin(a) * r };
}

float BossClearSequence::ShakeAmplitude() const {
	float amp = 0.0f;
	if (stage_ == Stage::HitStop && time_ > 0.0f) amp = hitStopShake_;
	if (collapseTime_ >= 0.0f && collapseShakeTime_ > kNearZero) {
		const float p = (time_ - collapseTime_) / collapseShakeTime_;
		if (p < 1.0f) amp = (std::max)(amp, collapseShake_ * (1.0f - p) * (1.0f - p));
	}
	return amp;
}

void BossClearSequence::ComputeShot(Vector3& outEye, Vector3& outLook, float t) const {
	// 寄った後はゆっくり回り、崩壊の後は少し引く
	const float orbit = orbitSpeed_ * (std::max)(t - hitStopTime_, 0.0f);
	const float c = std::cos(orbit), s = std::sin(orbit);
	const Vector3 dir{ shotDir_.x * c + shotDir_.z * s, 0.0f, -shotDir_.x * s + shotDir_.z * c };
	float dist = shotDistance_;
	if (collapseTime_ >= 0.0f && pullBackTime_ > kNearZero) {
		dist += pullBack_ * EaseOutCubic((t - collapseTime_) / pullBackTime_);
	}
	outEye = { bossCenter_.x + dir.x * dist, bossCenter_.y + shotHeight_, bossCenter_.z + dir.z * dist };
	outLook = { bossCenter_.x, bossCenter_.y + lookUp_, bossCenter_.z };
}

void BossClearSequence::ApplyCamera(Camera& camera) const {
	if (stage_ == Stage::Idle) return;

	// ① はその場（撮った絵と同じ構図）、② で寄る、③ 以降は寄った構図のまま
	float blend = 0.0f;
	if (stage_ == Stage::Slow) blend = SmoothStep01(slowTime_ > kNearZero ? stageTime_ / slowTime_ : 1.0f);
	else if (stage_ != Stage::HitStop) blend = 1.0f;

	Vector3 shotEye, look;
	ComputeShot(shotEye, look, time_);
	Vector3 eye{
		startEye_.x + (shotEye.x - startEye_.x) * blend,
		startEye_.y + (shotEye.y - startEye_.y) * blend,
		startEye_.z + (shotEye.z - startEye_.z) * blend };

	// 寄った目からボスを見る向き（回転の決まり：pitch=下向き＋、yaw=atan2(x,z)）
	Vector3 d{ look.x - shotEye.x, look.y - shotEye.y, look.z - shotEye.z };
	const float dl = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	float shotPitch = 0.0f, shotYaw = startRot_.y;
	if (dl > kLookDirEpsilon) {
		d = { d.x / dl, d.y / dl, d.z / dl };
		shotPitch = -std::asin((std::clamp)(d.y, -1.0f, 1.0f));
		shotYaw = std::atan2(d.x, d.z);
	}
	const Vector3 rot{
		LerpAngle(startRot_.x, shotPitch, blend),
		LerpAngle(startRot_.y, shotYaw, blend),
		startRot_.z * (1.0f - blend) };

	// 揺れ（世界が止まっていても実時間で揺れる）
	const float amp = ShakeAmplitude();
	if (amp > 0.0f) {
		eye.x += amp * std::sin(time_ * kShakeFreqX);
		eye.y += amp * std::sin(time_ * kShakeFreqY + kShakePhaseY);
		eye.z += amp * std::sin(time_ * kShakeFreqZ + kShakePhaseZ);
	}
	eye.y = (std::max)(eye.y, groundY_ + minEyeAboveGround_);

	camera.SetTranslate(eye);
	camera.SetRotate(rot);
	camera.Update();
}

void BossClearSequence::DrawOverlay(float screenW, float screenH) const {
	if (stage_ == Stage::Idle) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	// 白いフラッシュ。撮るフレーム（time_ == 0）は出さない
	float flash = 0.0f;
	if (time_ > 0.0f && flashTime_ > kNearZero && time_ < flashTime_) flash = flashAlpha_ * (1.0f - time_ / flashTime_);
	if (collapseTime_ >= 0.0f && flashTime_ > kNearZero) {
		const float ct = (time_ - collapseTime_) / (flashTime_ * kCollapseFlashTimeScale);
		if (ct < 1.0f) flash = (std::max)(flash, collapseFlashAlpha_ * (1.0f - ct));
	}
	if (flash > 0.0f) tr->DrawRect({ 0.0f, 0.0f }, { screenW, screenH }, { 1.0f, 1.0f, 1.0f, flash });

	banner_.Draw(screenW, screenH);
}

void BossClearSequence::OnImGui() {
#ifdef _DEBUG
	if (!ImGui::TreeNode("Clear Sequence (23 B)")) return;
	static const char* kStageNames[] = { "Idle", "HitStop", "Slow", "Collapse", "Banner", "Done" };
	ImGui::Text("Stage: %s  t=%.2f", kStageNames[static_cast<int>(stage_)], time_);
	ImGui::DragFloat("HitStop Time", &hitStopTime_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Slow Time", &slowTime_, 0.05f, 0.0f, 6.0f, "%.2f");
	ImGui::DragFloat("Slow Scale", &slowScale_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Restore Time", &restoreTime_, 0.05f, 0.0f, 4.0f, "%.2f");
	ImGui::DragFloat("Banner Delay", &bannerDelay_, 0.05f, 0.0f, 4.0f, "%.2f");
	ImGui::DragFloat("Banner Hold", &bannerHold_, 0.05f, 0.5f, 10.0f, "%.2f");
	ImGui::DragFloat("Flash Time", &flashTime_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Flash Alpha", &flashAlpha_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::DragFloat("Collapse Flash Alpha", &collapseFlashAlpha_, 0.01f, 0.0f, 1.0f, "%.2f");
	ImGui::SeparatorText("Camera");
	ImGui::DragFloat("Shot Distance", &shotDistance_, 0.1f, 2.0f, 40.0f, "%.1f");
	ImGui::DragFloat("Shot Height", &shotHeight_, 0.05f, -5.0f, 10.0f, "%.2f");
	ImGui::DragFloat("Shot Yaw Offset", &shotYawOffset_, 0.01f, -3.14f, 3.14f, "%.2f");
	ImGui::DragFloat("Look Up", &lookUp_, 0.05f, -5.0f, 5.0f, "%.2f");
	ImGui::DragFloat("Orbit Speed", &orbitSpeed_, 0.005f, -1.0f, 1.0f, "%.3f");
	ImGui::DragFloat("Pull Back", &pullBack_, 0.1f, 0.0f, 30.0f, "%.1f");
	ImGui::DragFloat("Pull Back Time", &pullBackTime_, 0.05f, 0.1f, 6.0f, "%.2f");
	ImGui::DragFloat("HitStop Shake", &hitStopShake_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Collapse Shake", &collapseShake_, 0.01f, 0.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Collapse Shake Time", &collapseShakeTime_, 0.05f, 0.0f, 3.0f, "%.2f");
	ImGui::SeparatorText("Bursts");
	ImGui::DragFloat("Burst Interval", &burstInterval_, 0.01f, 0.02f, 2.0f, "%.2f");
	ImGui::DragFloat("Burst Radius", &burstRadius_, 0.05f, 0.0f, 10.0f, "%.2f");
	ImGui::DragFloat("Burst Height", &burstHeight_, 0.05f, 0.0f, 10.0f, "%.2f");
	ImGui::DragInt("Collapse Bursts", &collapseBurstCount_, 1.0f, 0, 20);
	ImGui::DragFloat("Collapse Burst Span", &collapseBurstSpan_, 0.01f, 0.0f, 3.0f, "%.2f");
	ImGui::DragFloat("Collapse Burst Radius", &collapseBurstRadius_, 0.05f, 0.0f, 15.0f, "%.2f");
	banner_.OnImGui();
	ImGui::TreePop();
#endif
}
