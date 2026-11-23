#pragma once
#include "Vector3.h"
#include <vector>

class JsonValue;

/// <summary>
/// HUB の一人称のカメラのキー列（乗り込み HubBoardingSequence と降りる HubDisembarkSequence が使う）。
/// キーを Catmull-Rom でつなぎ、キーごとに入り方の Ease と「歩く」（頭が上下する）を持つ。
/// 位置は機体の原点（足元の中央）からの相対、角度は度。
/// </summary>
class HubCameraTrack {
public:
	// 区間の進み方（キーに入るまでの区間に付ける）
	enum class Ease : int {
		Linear = 0,
		InOut,
		In,     // ゆっくり出て加速
		Out,    // 減速して止まる
		kCount,
	};

	/// <summary>カメラのキー。pitch は + で下を向く</summary>
	struct Key {
		float time = 0.0f;
		Vector3 eye = { 0.0f, 0.0f, 0.0f };
		float yawDegrees = 0.0f;     // 0 = +Z（機体の正面の向き）。振り返りは 180 → 360 のように巻き戻さずに書く
		float pitchDegrees = 0.0f;
		Ease ease = Ease::Linear;
		bool walk = false;           // このキーに入るまでの区間は歩く（頭が上下する）
	};

	/// <summary>ある時刻のカメラ</summary>
	struct Sample {
		Vector3 eye;          // 機体の原点からの相対
		float yaw = 0.0f;     // ラジアン
		float pitch = 0.0f;   // ラジアン
	};

	/// <summary>キー列を差し替えて時刻順に並べる</summary>
	/// <param name="keys">キー列</param>
	void SetKeys(std::vector<Key> keys);
	/// <summary>キー列を返す（書き換え用）</summary>
	/// <returns>キー列</returns>
	std::vector<Key>& GetKeys() { return keys_; }
	/// <summary>キー列を返す</summary>
	/// <returns>キー列</returns>
	const std::vector<Key>& GetKeys() const { return keys_; }

	/// <summary>ある時刻のカメラを求める（キーを Catmull-Rom でつなぎ、区間の Ease と歩く上下を掛ける）</summary>
	/// <param name="time">時刻 [秒]</param>
	/// <returns>カメラ。キーが無ければ既定値</returns>
	Sample Evaluate(float time) const;

	/// <summary>歩くときの頭の上下を設定する</summary>
	/// <param name="amplitude">上下の幅 [m]</param>
	/// <param name="stepsPerSecond">1 秒あたりの歩数</param>
	void SetBob(float amplitude, float stepsPerSecond) { bobAmplitude_ = amplitude; stepsPerSecond_ = stepsPerSecond; }

	/// <summary>node（オブジェクト）の "keys" / "bobAmplitude" / "stepsPerSecond" を読む（持ち主の調整値の中に置く）</summary>
	/// <param name="node">読み込み元の JSON オブジェクト</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>node（オブジェクト）へ "keys" / "bobAmplitude" / "stepsPerSecond" を書く</summary>
	/// <param name="node">書き込み先の JSON オブジェクト</param>
	void SaveTuning(JsonValue& node) const;

	/// <summary>キーを ImGui で編集する</summary>
	/// <param name="previewTime">「Preview Here」が押されたらその時刻を入れる</param>
	/// <returns>「Preview Here」が押されたら true</returns>
	bool OnImGui(float& previewTime);

private:
	/// <summary>キーを時刻順に並べる（同じ時刻は順を保つ）</summary>
	void SortKeys();

	std::vector<Key> keys_;
	float bobAmplitude_ = 0.04f;   // 歩くときの頭の上下 [m]
	float stepsPerSecond_ = 1.8f;
};
