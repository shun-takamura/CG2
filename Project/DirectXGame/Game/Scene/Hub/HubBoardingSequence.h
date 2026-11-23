#pragma once
#include "HubCameraTrack.h"
#include "HubMechModel.h"
#include "Vector3.h"

class JsonValue;

/// <summary>
/// 乗り込み（18_HubTransitions.md ①の OUT）のタイムライン。
/// 機体が手を差し出す → 足場 → 手のひら → 胸の装甲が光って開いた入口 → 振り返って座る → 装甲が閉じ、
/// 正面を向いたままハッチが降りて閉じるまでを、時刻から一人称のカメラ・機体の腕と胸の入口・ハッチの開き具合・外の光の強さに変える（描画と、そのあとのコクピットは持ち主の HubScene がする）。
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
		HubMechModel::Pose mech;      // 機体の腕（手を差し出す）と胸の入口
	};

	/// <summary>調整値を既定にする</summary>
	HubBoardingSequence();

	/// <summary>調整値（カメラのキー・各動作の時刻・揺れ）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>時刻 0 から乗り込みを再生する</summary>
	void Start();
	/// <summary>再生をやめて待機（時刻 0）に戻す</summary>
	void Stop();
	/// <summary>時刻を進め、座った・閉じ切った瞬間を記録する</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>再生中かを返す</summary>
	/// <returns>再生中なら true</returns>
	bool IsPlaying() const { return playing_; }
	/// <summary>閉じ切って少し待ち終えたかを返す（持ち主はこのフレームでコクピットの状態へ移る）</summary>
	/// <returns>終わったら true</returns>
	bool IsFinished() const { return playing_ && time_ >= endTime_; }
	/// <summary>乗り込みの最後の時刻を返す（コクピットの視点＝この時刻の姿勢）</summary>
	/// <returns>時刻 [秒]</returns>
	float GetEndTime() const { return endTime_; }
	/// <summary>閉じ切ったときの外の光の倍率を返す（コクピットで外が透けると 1 へ戻す）</summary>
	/// <returns>倍率</returns>
	float GetMinLightScale() const { return minLightScale_; }
	/// <summary>待機の視点（キー 0）を返す。降りる最後はここへ戻る</summary>
	/// <returns>カメラのキー</returns>
	HubCameraTrack::Key GetHomeKey() const;

	/// <summary>座った瞬間を一度だけ返す（カメラを揺らす）。返したら下ろす</summary>
	/// <returns>座った瞬間が来ていれば true</returns>
	bool ConsumeSitImpact();
	/// <summary>ハッチが閉じ切った瞬間を一度だけ返す（カメラを揺らす）。返したら下ろす</summary>
	/// <returns>閉じ切った瞬間が来ていれば true</returns>
	bool ConsumeCloseImpact();
	/// <summary>座ったときの揺れの強さを返す</summary>
	/// <returns>揺れの強さ</returns>
	float GetSitShakeIntensity() const { return sitShakeIntensity_; }
	/// <summary>座ったときの揺れの長さを返す</summary>
	/// <returns>長さ [秒]</returns>
	float GetSitShakeSeconds() const { return sitShakeSeconds_; }
	/// <summary>閉じ切ったときの揺れの強さを返す</summary>
	/// <returns>揺れの強さ</returns>
	float GetCloseShakeIntensity() const { return closeShakeIntensity_; }
	/// <summary>閉じ切ったときの揺れの長さを返す</summary>
	/// <returns>長さ [秒]</returns>
	float GetCloseShakeSeconds() const { return closeShakeSeconds_; }

	/// <summary>今の見え方を返す。ImGui でシーク中はシークした時刻の見え方</summary>
	/// <returns>見え方</returns>
	Pose GetCurrentPose() const { return Evaluate(previewing_ ? previewTime_ : time_); }
	/// <summary>ある時刻の見え方（カメラ・腕と胸の入口・ハッチの開き具合・外の光）を求める</summary>
	/// <param name="time">乗り込みの時刻 [秒]</param>
	/// <returns>見え方</returns>
	Pose Evaluate(float time) const;

	/// <summary>再生の時刻を返す</summary>
	/// <returns>時刻 [秒]</returns>
	float GetTime() const { return time_; }
	/// <summary>ImGui でシーク中かを返す</summary>
	/// <returns>シーク中なら true</returns>
	bool IsPreviewing() const { return previewing_; }

	/// <summary>調整値（Hub.json の "boarding"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値とシークの ImGui を表示する</summary>
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
	float offerStartTime_ = 0.0f;      // 機体が手を差し出し始める（Offer のクリップ 1.6 秒。待機＝時刻 0 は直立）
	float chestOpenStartTime_ = 2.6f;  // 胸の装甲が開き始める
	float chestOpenEndTime_ = 3.6f;    // 開き切る
	float chestCloseStartTime_ = 6.8f; // 座った後、閉じ始める
	float chestCloseEndTime_ = 7.6f;   // 閉じ切る（このあとハッチが降りる）
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
