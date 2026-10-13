#pragma once
#include "Vector3.h"
#include <vector>

class JsonValue;

/// <summary>
/// 乗り込み（18_HubTransitions.md ①の OUT）のタイムライン。
/// 回廊 → 手のひら → 開口部 → 振り返って座る → ハッチが降りて閉じ、真っ暗になるまでを、
/// 時刻から一人称のカメラ・ハッチの開き具合・暗さに変える（描画やシーン切り替えは持ち主の HubScene がする）。
/// 時刻 0 の姿勢は HUB の待機の視点を兼ねる。降りる（②の IN）は同じキーを逆にたどる想定。
/// 調整値は Hub.json の "boarding"（ImGui の「Hub Tuning」でシークしながら詰める）。
/// </summary>
class HubBoardingSequence {
public:
	// 区間の進み方（キーに入るまでの区間に付ける）
	enum class Ease : int {
		Linear = 0,
		InOut,
		In,     // ゆっくり出て加速
		Out,    // 減速して止まる
		kCount,
	};

	/// <summary>カメラのキー。位置は機体の原点（足元の中央）からの相対、角度は度（pitch は + で下を向く）</summary>
	struct CameraKey {
		float time = 0.0f;
		Vector3 eye = { 0.0f, 0.0f, 0.0f };
		float yawDegrees = 0.0f;     // 0 = +Z（機体の正面の向き）。振り返りは 180 → 360 のように巻き戻さずに書く
		float pitchDegrees = 0.0f;
		Ease ease = Ease::Linear;
		bool walk = false;           // このキーに入るまでの区間は歩く（頭が上下する）
	};

	/// <summary>ある時刻の見え方</summary>
	struct Pose {
		Vector3 eye;          // 機体の原点からの相対
		float yaw = 0.0f;     // ラジアン
		float pitch = 0.0f;   // ラジアン
		float hatchOpenRatio = 1.0f;  // 1 = 開いている、0 = 閉じ切り
		float lightScale = 1.0f;      // 外の光の強さの倍率（隙間が狭まると暗くなる）
		float blackout = 0.0f;        // 黒い板の濃さ 0..1
	};

	HubBoardingSequence();

	void ResetToDefaults();

	void Start();
	void Stop();          // 待機（時刻 0）に戻す
	void Update(float deltaTime);

	bool IsPlaying() const { return playing_; }
	/// <summary>真っ暗のまま待ち終えた（持ち主はこのフレームでシーンを切り替える）</summary>
	bool IsFinished() const { return playing_ && time_ >= changeSceneTime_; }

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
	std::vector<CameraKey> keys_;
	float sitTime_ = 6.2f;             // 座った瞬間（揺れ）
	float sitShakeIntensity_ = 0.025f;
	float sitShakeSeconds_ = 0.25f;
	float hatchStartTime_ = 6.6f;      // ハッチが降り始める
	float hatchCloseTime_ = 8.8f;      // 閉じ切る（揺れ）
	float hatchCloseExponent_ = 2.0f;  // 降り方。1 = 等速、大きいほどゆっくり出て最後に勢いよく閉まる
	float closeShakeIntensity_ = 0.04f;
	float closeShakeSeconds_ = 0.35f;
	float lightDimStartRatio_ = 0.4f;  // ハッチの開き具合がこれを切ると外の光が弱まり始める
	float minLightScale_ = 0.1f;       // 閉じ切る直前の外の光の倍率
	float blackoutStartTime_ = 8.55f;  // 黒い板が濃くなり始める（閉じ切りで真っ暗）
	float changeSceneTime_ = 9.1f;     // 真っ暗のまま待って STAGESELECT へ
	float bobAmplitude_ = 0.04f;       // 歩くときの頭の上下 [m]
	float stepsPerSecond_ = 1.8f;
};
