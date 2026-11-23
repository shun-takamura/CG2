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
	/// <summary>動かすカメラを受け取る</summary>
	/// <param name="camera">動かすカメラ</param>
	void Initialize(Camera* camera);

	/// <summary>カメラの位置を決めるスプラインを設定する</summary>
	/// <param name="path">カメラパス（nullptr なら位置を動かさない）</param>
	void SetCameraPath(const SplineCurveActor* path) { cameraPath_ = path; }

	/// <summary>向きのキー列を設定する（所有は Scene 側。t 昇順前提）</summary>
	/// <param name="keys">キー列（nullptr・空なら接線方向を向く）</param>
	void SetRotKeys(const std::vector<std::unique_ptr<CameraRotKey>>* keys) { rotKeys_ = keys; }

	/// <summary>進行度の速さを設定する</summary>
	/// <param name="unitsPerSecond">進行度の速さ [1/秒]（1.0 で 1 秒かけて 0→1 を走破）</param>
	void SetSpeed(float unitsPerSecond) { speed_ = unitsPerSecond; }
	/// <summary>進行度を設定する</summary>
	/// <param name="t01">進行度（0..1）</param>
	void SetProgress(float t01)         { progress_ = t01; }
	/// <summary>終端で先頭へ戻るかを設定する</summary>
	/// <param name="loop">戻るなら true</param>
	void SetLoop(bool loop)             { loop_ = loop; }
	/// <summary>進行度を止めるかを設定する</summary>
	/// <param name="paused">止めるなら true</param>
	void SetPaused(bool paused)         { paused_ = paused; }

	/// <summary>
	/// 進行度 [tStart, tEnd] の間だけ、カメラの Y を minY より下げない（水面に潜らない保険）。
	/// softness [m] の幅でなめらかに押し上げる（折れ目を出さない）
	/// </summary>
	/// <param name="enabled">有効にするか</param>
	/// <param name="minY">下限の高さ [m]</param>
	/// <param name="tStart">効かせ始める進行度</param>
	/// <param name="tEnd">効かせ終わる進行度</param>
	/// <param name="softness">なめらかに押し上げる幅 [m]</param>
	void SetMinYWindow(bool enabled, float minY, float tStart, float tEnd, float softness) {
		minYEnabled_ = enabled; minY_ = minY; minYStartT_ = tStart; minYEndT_ = tEnd; minYSoftness_ = softness;
	}

	/// <summary>
	/// 回転キーが無い時（接線を向く時）だけ、進行度 [tStart, tEnd] の間ピッチを pitchRad 足す（+ で見下ろす）。
	/// 窓の内側 fadeT の幅で smoothstep で入る／抜ける
	/// </summary>
	/// <param name="pitchRad">足すピッチ [ラジアン]</param>
	/// <param name="tStart">窓の始まりの進行度</param>
	/// <param name="tEnd">窓の終わりの進行度</param>
	/// <param name="fadeT">入る／抜ける幅（進行度）</param>
	void SetPitchBiasWindow(float pitchRad, float tStart, float tEnd, float fadeT) {
		pitchBias_ = pitchRad; pitchStartT_ = tStart; pitchEndT_ = tEnd; pitchFadeT_ = fadeT;
	}

	/// <summary>今の進行度を取得する</summary>
	/// <returns>進行度（0..1）</returns>
	float GetProgress() const { return progress_; }
	/// <summary>終端まで進んだか</summary>
	/// <returns>ループせず進行度が 1 に達したら true</returns>
	bool  IsFinished() const  { return !loop_ && progress_ >= 1.0f; }

	/// <summary>進行度を進め、カメラの位置をスプラインから、向きを回転キー列（無ければ接線）から決めて反映する</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
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
