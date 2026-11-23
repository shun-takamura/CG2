#pragma once
#include "HubCameraTrack.h"
#include "HubMechModel.h"
#include "Vector3.h"

class JsonValue;

/// <summary>
/// 降りる（18_HubTransitions.md ②の IN）のタイムライン。逆再生にはしない（後ろ向きに歩くことになるため）。
/// 座ったまま正面を向いてハッチが開く → 立ち上がる → 機体に背を向けたまま乗るときと同じ道（開口部 → 手のひら → 足場）を
/// 前向きに歩いて待機の位置まで戻る → 振り返って機体を見る（＝待機の視点）。
/// 2026-10-18 機体は差し出した手のまま始まり、ハッチが開いた後に胸の装甲が光って開き、手のひらへ出たら閉じ、
/// 振り返る間に手を戻す（Retract）。
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
		HubMechModel::Pose mech;      // 機体の腕と胸の入口
	};

	/// <summary>調整値を既定にする</summary>
	HubDisembarkSequence();
	/// <summary>調整値（カメラのキー・各動作の時刻）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>降り始める（最後の 2 つのキーを待機の視点に合わせる）</summary>
	/// <param name="home">待機の視点（HubBoardingSequence::GetHomeKey）</param>
	/// <param name="minLightScale">閉じ切りの外の光の倍率</param>
	void Start(const HubCameraTrack::Key& home, float minLightScale);
	/// <summary>再生をやめて時刻を 0 に戻す</summary>
	void Stop();
	/// <summary>時刻を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>再生中かを返す</summary>
	/// <returns>再生中なら true</returns>
	bool IsPlaying() const { return playing_; }
	/// <summary>振り返り終えたかを返す（持ち主はこのフレームで待機へ戻す）</summary>
	/// <returns>終わったら true</returns>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }

	/// <summary>今の見え方を返す。ImGui でシーク中はシークした時刻の見え方</summary>
	/// <returns>見え方</returns>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	/// <summary>ある時刻の見え方（カメラ・手と胸の装甲・ハッチの開き具合・外の光）を求める</summary>
	/// <param name="time">降りる時刻 [秒]</param>
	/// <returns>見え方</returns>
	Pose Evaluate(float time) const;
	/// <summary>ImGui でシーク中かを返す</summary>
	/// <returns>シーク中なら true</returns>
	bool IsPreviewing() const { return previewing_; }

	/// <summary>調整値（Hub.json の "disembark"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値とシークの ImGui を表示する</summary>
	/// <param name="home">シークで最後のキーを待機の視点に合わせるために使う</param>
	/// <param name="minLightScale">閉じ切りの外の光の倍率</param>
	void OnImGui(const HubCameraTrack::Key& home, float minLightScale);

private:
	/// <summary>最後の 2 つのキーを待機の視点に合わせる（振り返りは 180 度先まで回す）</summary>
	/// <param name="home">待機の視点</param>
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
	float chestOpenStartTime_ = 1.6f; // 胸の装甲が開き始める
	float chestOpenEndTime_ = 2.6f;   // 開き切る
	float chestCloseStartTime_ = 4.6f;// 手のひらへ出た後、閉じ始める
	float chestCloseEndTime_ = 5.4f;  // 閉じ切る
	float retractStartTime_ = 7.0f;   // 手を戻し始める（Retract のクリップ 1.4 秒。振り返る間）
	float endTime_ = 8.3f;            // 終わり（待機へ）
};
