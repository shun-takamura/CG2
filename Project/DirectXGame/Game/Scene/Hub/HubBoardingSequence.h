#pragma once
#include "HubCameraTrack.h"
#include "Vector3.h"

class JsonValue;

/// <summary>
/// 乗り込み（18_HubTransitions.md ①の OUT）のタイムライン。
/// 回廊 → 手のひら → 開口部 → 振り返って座る → 正面を向いたままハッチが降りて閉じるまでを、
/// 時刻から一人称のカメラ・ハッチの開き具合・外の光の強さに変える（描画と、そのあとのコクピットは持ち主の HubScene がする）。
/// 時刻 0 の姿勢は HUB の待機の視点を兼ねる（降りる HubDisembarkSequence もここへ戻ってくる）。
/// 調整値は Hub.json の "boarding"（ImGui の「Hub Tuning」でシークしながら詰める）。
/// </summary>
class HubBoardingSequence {
public:
	/// <summary>ある時刻の見え方</summary>
	struct Pose {
		Vector3 eye;          // 機体の原点からの相対
		float yaw = 0.0f;     // ラジアン
		float pitch = 0.0f;   // ラジアン
		float hatchOpenRatio = 1.0f;  // 1 = 開いている、0 = 閉じ切り
		float lightScale = 1.0f;      // 外の光の強さの倍率（隙間が狭まると暗くなる）
	};

	HubBoardingSequence();

	void ResetToDefaults();

	void Start();
	void Stop();          // 待機（時刻 0）に戻す
	void Update(float deltaTime);

	bool IsPlaying() const { return playing_; }
	/// <summary>閉じ切って少し待ち終えた（持ち主はこのフレームでコクピットの状態へ移る）</summary>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }
	/// <summary>乗り込みの最後の時刻（コクピットの視点＝この時刻の姿勢）</summary>
	float GetEndTime() const { return endTime_; }
	/// <summary>閉じ切ったときの外の光の倍率（コクピットで外が透けると 1 へ戻す）</summary>
	float GetMinLightScale() const { return minLightScale_; }
	/// <summary>待機の視点（キー 0）。降りる最後はここへ戻る</summary>
	HubCameraTrack::Key GetHomeKey() const;

	/// <summary>座った・閉じ切った瞬間を一度だけ返す（カメラを揺らす）。返したら下ろす</summary>
	bool ConsumeSitImpact();
	bool ConsumeCloseImpact();
	float GetSitShakeIntensity() const { return sitShakeIntensity_; }
	float GetSitShakeSeconds() const { return sitShakeSeconds_; }
	float GetCloseShakeIntensity() const { return closeShakeIntensity_; }
	float GetCloseShakeSeconds() const { return closeShakeSeconds_; }

	/// <summary>今の見え方。ImGui でシーク中はシークした時刻の見え方</summary>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	Pose Evaluate(float time) const;

	float GetTime() const { return time_; }
	bool IsPreviewing() const { return previewing_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	float time_ = 0.0f;
	bool playing_ = false;
	bool sitImpactPending_ = false;
	bool closeImpactPending_ = false;

	// デバッグ：時刻を手で動かして見る
	bool previewing_ = false;
	float previewTime_ = 0.0f;

	// 調整値（Hub.json の "boarding"）
	HubCameraTrack track_;
	float sitTime_ = 5.2f;             // 座った瞬間（揺れ）
	float sitShakeIntensity_ = 0.025f;
	float sitShakeSeconds_ = 0.25f;
	float hatchStartTime_ = 5.6f;      // ハッチが降り始める
	float hatchCloseTime_ = 7.8f;      // 閉じ切る（揺れ）
	float hatchCloseExponent_ = 2.0f;  // 降り方。1 = 等速、大きいほどゆっくり出て最後に勢いよく閉まる
	float closeShakeIntensity_ = 0.04f;
	float closeShakeSeconds_ = 0.35f;
	float lightDimStartRatio_ = 0.4f;  // ハッチの開き具合がこれを切ると外の光が弱まり始める
	float minLightScale_ = 0.1f;       // 閉じ切る直前の外の光の倍率
	float endTime_ = 7.9f;             // 乗り込みの終わり（コクピットの状態へ。18 番 ①の IN）
};
