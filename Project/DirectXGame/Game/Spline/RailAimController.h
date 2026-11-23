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
	/// <summary>現在の向きを設定する（記録開始時に既存カメラ向きを入れる用）</summary>
	/// <param name="e">向き（pitch/yaw/roll [ラジアン]）</param>
	void           SetEuler(const Vector3& e) { euler_ = e; }
	/// <summary>現在の向きを取得する</summary>
	/// <returns>向き（pitch/yaw/roll [ラジアン]）</returns>
	const Vector3& GetEuler() const           { return euler_; }

	/// <summary>左ドラッグの量で yaw と pitch を回す（pitch は ±89° でクランプ）</summary>
	/// <param name="dYaw">yaw の入力量（マウスの dx。感度を掛ける）</param>
	/// <param name="dPitch">pitch の入力量（マウスの dy。感度を掛ける）</param>
	void AddYawPitch(float dYaw, float dPitch);
	/// <summary>Alt+ドラッグの量で roll を回す</summary>
	/// <param name="dRoll">roll の入力量（感度を掛ける）</param>
	void AddRoll(float dRoll);

	/// <summary>eye（レール上の位置）に固定したまま euler_ の向きを Camera へ反映する</summary>
	/// <param name="camera">反映先のカメラ（nullptr なら何もしない）</param>
	/// <param name="eye">カメラの位置（ワールド）</param>
	void Apply(Camera* camera, const Vector3& eye) const;

	/// <summary>感度を設定する（既定は DebugCamera の Orbit/Pan と同等）</summary>
	/// <param name="yawPitch">yaw・pitch の感度 [ラジアン/入力量]</param>
	/// <param name="roll">roll の感度 [ラジアン/入力量]</param>
	void SetSensitivity(float yawPitch, float roll) { yawPitchSens_ = yawPitch; rollSens_ = roll; }

private:
	Vector3 euler_{ 0.0f, 0.0f, 0.0f };
	float   yawPitchSens_ = 0.005f;
	float   rollSens_     = 0.01f;
};
