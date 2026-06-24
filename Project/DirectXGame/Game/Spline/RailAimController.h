#pragma once

#include "Vector3.h"

class Camera;

/// <summary>
/// レールカメラ「向き」オーサリング用の見回しローテータ。
/// 位置はレール(スプライン)に固定したまま、マウス操作で視線(オイラー角)だけ回す。
/// 出力は Camera へ SetTranslate/SetRotate で注入する（worldMatrix も更新され、
/// 記録した向きがそのまま GetForward などと整合する）。
///
/// 周回(Orbit)するデバッグカメラとは役割が別。こちらは「その場見回し」専用。
/// </summary>
class RailAimController {
public:
	// 現在の向き（pitch/yaw/roll, radian）。記録開始時に既存カメラ向きを入れる用。
	void           SetEuler(const Vector3& e) { euler_ = e; }
	const Vector3& GetEuler() const           { return euler_; }

	// 左ドラッグ：dx→yaw, dy→pitch（pitch は ±89° でクランプ）
	void AddYawPitch(float dYaw, float dPitch);
	// Alt+ドラッグ：roll
	void AddRoll(float dRoll);

	// eye(レール上の位置)に固定したまま euler_ の向きを Camera へ反映する
	void Apply(Camera* camera, const Vector3& eye) const;

	// 感度（既定は DebugCamera の Orbit/Pan と同等）
	void SetSensitivity(float yawPitch, float roll) { yawPitchSens_ = yawPitch; rollSens_ = roll; }

private:
	Vector3 euler_{ 0.0f, 0.0f, 0.0f };
	float   yawPitchSens_ = 0.005f;
	float   rollSens_     = 0.01f;
};
