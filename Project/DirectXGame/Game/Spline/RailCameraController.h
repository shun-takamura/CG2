#pragma once

#include "Vector3.h"

class Camera;
class SplineCurveActor;

/// <summary>
/// レールカメラ制御。位置スプライン(cameraPath) と注視点スプライン(lookAtPath) を
/// 同じ進行度 t で評価し、eye と target を毎フレーム算出して Camera へ反映する。
///
/// 設計方針:
///   - 進行方向と視線方向を独立に制御するため、注視点は別パスで定義する。
///   - 前進カメラ:        lookAtPath = cameraPath を +Z 方向に平行移動
///   - 右スクロール演出:  lookAtPath = cameraPath を +X 方向に平行移動
///   - ボス導入演出:      cameraPath を横にオフセット、lookAtPath は別形状
///   - シューティング中はプレイヤーがカメラを操作することは禁止。
/// </summary>
class RailCameraController {
public:
	void Initialize(Camera* camera);

	void SetCameraPath(const SplineCurveActor* path) { cameraPath_ = path; }
	void SetLookAtPath(const SplineCurveActor* path) { lookAtPath_ = path; }

	// t の進行速度（1.0 で 1 秒かけて 0→1 を走破）
	void SetSpeed(float unitsPerSecond) { speed_ = unitsPerSecond; }
	void SetProgress(float t01)         { progress_ = t01; }
	void SetLoop(bool loop)             { loop_ = loop; }
	void SetPaused(bool paused)         { paused_ = paused; }

	float GetProgress() const { return progress_; }
	bool  IsFinished() const  { return !loop_ && progress_ >= 1.0f; }

	void Update(float deltaTime);

private:
	Camera*                  camera_     = nullptr;
	const SplineCurveActor*  cameraPath_ = nullptr;
	const SplineCurveActor*  lookAtPath_ = nullptr;

	float progress_ = 0.0f;
	float speed_    = 0.1f;
	bool  loop_     = false;
	bool  paused_   = false;
};
