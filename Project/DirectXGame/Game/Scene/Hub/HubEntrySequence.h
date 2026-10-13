#pragma once
#include "HubCameraTrack.h"
#include "Vector3.h"

class JsonValue;

/// <summary>
/// タイトルから HUB に入るときのタイムライン（18_HubTransitions.md ⑧）。
/// 白フェードが晴れていく間に、時計の門の通り道（閉じたタイトルの扉のすぐ手前）から歩き出し、
/// 踊り場 → 足場を進んで待機の視点で止まる。歩くので頭が上下する。
/// 最後のキーは待機の視点（乗り込みのキー 0）に Start で合わせる。調整値は Hub.json の "entry"。
/// </summary>
class HubEntrySequence {
public:
	/// <summary>ある時刻の見え方</summary>
	struct Pose {
		Vector3 eye;          // 機体の原点からの相対
		float yaw = 0.0f;     // ラジアン
		float pitch = 0.0f;   // ラジアン
	};

	HubEntrySequence();
	void ResetToDefaults();

	/// <summary>歩き始める。home は待機の視点（HubBoardingSequence::GetHomeKey）</summary>
	void Start(const HubCameraTrack::Key& home);
	void Stop();
	void Update(float deltaTime);

	bool IsPlaying() const { return playing_; }
	/// <summary>待機の視点に着いた（持ち主はこのフレームで待機へ戻す）</summary>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }

	/// <summary>今の見え方。ImGui でシーク中はシークした時刻の見え方</summary>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	Pose Evaluate(float time) const;
	bool IsPreviewing() const { return previewing_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	/// <summary>home はシークで最後のキーを待機の視点に合わせるために使う</summary>
	void OnImGui(const HubCameraTrack::Key& home);

private:
	// 最後のキーを待機の視点に合わせる
	void MatchHome(const HubCameraTrack::Key& home);

	float time_ = 0.0f;
	bool playing_ = false;

	bool previewing_ = false;
	float previewTime_ = 0.0f;

	// 調整値（Hub.json の "entry"）
	HubCameraTrack track_;
	float endTime_ = 5.6f;   // 終わり（待機へ）
};
