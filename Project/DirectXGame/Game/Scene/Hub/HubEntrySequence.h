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

	/// <summary>調整値を既定にする</summary>
	HubEntrySequence();
	/// <summary>調整値（カメラのキー・歩く上下）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>歩き始める（最後のキーを待機の視点に合わせる）</summary>
	/// <param name="home">待機の視点（HubBoardingSequence::GetHomeKey）</param>
	void Start(const HubCameraTrack::Key& home);
	/// <summary>再生をやめて時刻を 0 に戻す</summary>
	void Stop();
	/// <summary>時刻を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>再生中かを返す</summary>
	/// <returns>再生中なら true</returns>
	bool IsPlaying() const { return playing_; }
	/// <summary>待機の視点に着いたかを返す（持ち主はこのフレームで待機へ戻す）</summary>
	/// <returns>着いたら true</returns>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }

	/// <summary>今の見え方を返す。ImGui でシーク中はシークした時刻の見え方</summary>
	/// <returns>見え方</returns>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	/// <summary>ある時刻の見え方（カメラ）を求める</summary>
	/// <param name="time">入ってからの時刻 [秒]</param>
	/// <returns>見え方</returns>
	Pose Evaluate(float time) const;
	/// <summary>ImGui でシーク中かを返す</summary>
	/// <returns>シーク中なら true</returns>
	bool IsPreviewing() const { return previewing_; }

	/// <summary>調整値（Hub.json の "entry"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値とシークの ImGui を表示する</summary>
	/// <param name="home">シークで最後のキーを待機の視点に合わせるために使う</param>
	void OnImGui(const HubCameraTrack::Key& home);

private:
	/// <summary>最後のキーを待機の視点に合わせる</summary>
	/// <param name="home">待機の視点</param>
	void MatchHome(const HubCameraTrack::Key& home);

	float time_ = 0.0f;
	bool playing_ = false;

	bool previewing_ = false;
	float previewTime_ = 0.0f;

	// 調整値（Hub.json の "entry"）
	HubCameraTrack track_;
	float endTime_ = 5.6f;   // 終わり（待機へ）
};
