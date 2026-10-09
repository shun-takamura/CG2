#pragma once

#include <vector>
#include <memory>

#include "Vector3.h"

class Camera;
class SplineCurveActor;
class CameraRotKey;

/// <summary>
/// レールカメラ制御。位置は cameraPath（スプライン）、向きは回転キーフレーム列で決める。
///
/// 設計方針:
///   - eye = cameraPath->Sample(progress)。位置は滑らかなレール。
///   - 向きは progress 上に並べた CameraRotKey 列を Slerp（区間ごとの easeToNext で緩急）。
///   - キーが無い間は接線方向（前方）を向く保険。
///   - シューティング中はプレイヤーがカメラを操作することは禁止（オーサリングは別モード）。
/// </summary>
class RailCameraController {
public:
	void Initialize(Camera* camera);

	void SetCameraPath(const SplineCurveActor* path) { cameraPath_ = path; }

	// 向きキー列（所有は Scene 側。t 昇順前提）。
	void SetRotKeys(const std::vector<std::unique_ptr<CameraRotKey>>* keys) { rotKeys_ = keys; }

	// t の進行速度（1.0 で 1 秒かけて 0→1 を走破）
	void SetSpeed(float unitsPerSecond) { speed_ = unitsPerSecond; }
	void SetProgress(float t01)         { progress_ = t01; }
	void SetLoop(bool loop)             { loop_ = loop; }
	void SetPaused(bool paused)         { paused_ = paused; }

	// 進行度 [tStart, tEnd] の間だけ、カメラの Y を minY より下げない（水面に潜らない保険）。
	// softness[m] の幅でなめらかに押し上げる（折れ目を出さない）。
	void SetMinYWindow(bool enabled, float minY, float tStart, float tEnd, float softness) {
		minYEnabled_ = enabled; minY_ = minY; minYStartT_ = tStart; minYEndT_ = tEnd; minYSoftness_ = softness;
	}

	// 回転キーが無い時（接線を向く時）だけ、進行度 [tStart, tEnd] の間ピッチを pitchRad 足す（+ で見下ろす）。
	// 窓の内側 fadeT の幅で smoothstep で入る／抜ける。
	void SetPitchBiasWindow(float pitchRad, float tStart, float tEnd, float fadeT) {
		pitchBias_ = pitchRad; pitchStartT_ = tStart; pitchEndT_ = tEnd; pitchFadeT_ = fadeT;
	}

	float GetProgress() const { return progress_; }
	bool  IsFinished() const  { return !loop_ && progress_ >= 1.0f; }

	void Update(float deltaTime);

private:
	Camera*                  camera_     = nullptr;
	const SplineCurveActor*  cameraPath_ = nullptr;
	const std::vector<std::unique_ptr<CameraRotKey>>* rotKeys_ = nullptr;

	float progress_ = 0.0f;
	float speed_    = 0.1f;
	bool  loop_     = false;
	bool  paused_   = false;

	bool  minYEnabled_  = false;
	float minY_         = 0.0f;
	float minYStartT_   = 0.0f;
	float minYEndT_     = 0.0f;
	float minYSoftness_ = 1.0f;

	float pitchBias_   = 0.0f;
	float pitchStartT_ = 0.0f;
	float pitchEndT_   = 0.0f;
	float pitchFadeT_  = 0.0f;
};
