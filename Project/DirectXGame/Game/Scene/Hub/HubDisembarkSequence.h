#pragma once
#include "HubCameraTrack.h"
#include "Vector3.h"

class JsonValue;

/// <summary>
/// 降りる（18_HubTransitions.md ②の IN）のタイムライン。逆再生にはしない（後ろ向きに歩くことになるため）。
/// 座ったまま正面を向いてハッチが開く → 立ち上がる → 機体に背を向けたまま乗るときと同じ道（開口部 → 手のひら → 足場）を
/// 前向きに歩いて待機の位置まで戻る → 振り返って機体を見る（＝待機の視点）。
/// 最後の 2 つのキー（歩き終わり・振り返り）は待機の視点（乗り込みのキー 0）に合わせて Start で上書きする。
/// 調整値は Hub.json の "disembark"。
/// </summary>
class HubDisembarkSequence {
public:
	/// <summary>ある時刻の見え方</summary>
	struct Pose {
		Vector3 eye;          // 機体の原点からの相対
		float yaw = 0.0f;     // ラジアン
		float pitch = 0.0f;   // ラジアン
		float hatchOpenRatio = 0.0f;  // 1 = 開いている、0 = 閉じ切り
		float lightScale = 1.0f;      // 外の光の強さの倍率（隙間が広がると明るくなる）
	};

	HubDisembarkSequence();
	void ResetToDefaults();

	/// <summary>降り始める。home は待機の視点（HubBoardingSequence::GetHomeKey）。minLightScale は閉じ切りの外の光の倍率</summary>
	void Start(const HubCameraTrack::Key& home, float minLightScale);
	void Stop();
	void Update(float deltaTime);

	bool IsPlaying() const { return playing_; }
	/// <summary>振り返り終えた（持ち主はこのフレームで待機へ戻す）</summary>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }

	/// <summary>今の見え方。ImGui でシーク中はシークした時刻の見え方</summary>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	Pose Evaluate(float time) const;
	bool IsPreviewing() const { return previewing_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	/// <summary>home はシークで最後のキーを待機の視点に合わせるために使う</summary>
	void OnImGui(const HubCameraTrack::Key& home, float minLightScale);

private:
	// 最後の 2 つのキーを待機の視点に合わせる（振り返りは 180 度先まで回す）
	void MatchHome(const HubCameraTrack::Key& home);

	float time_ = 0.0f;
	bool playing_ = false;
	float minLightScale_ = 0.1f;

	bool previewing_ = false;
	float previewTime_ = 0.0f;

	// 調整値（Hub.json の "disembark"）
	HubCameraTrack track_;
	float hatchStartTime_ = 0.2f;     // ハッチが開き始める
	float hatchOpenTime_ = 2.0f;      // 開き切る
	float lightDimStartRatio_ = 0.4f; // ハッチの開き具合がこれを超えるまで外の光を弱めておく
	float endTime_ = 8.3f;            // 終わり（待機へ）
};
