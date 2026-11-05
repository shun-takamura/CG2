#pragma once
#include "Vector3.h"
#include "GlitchEffect.h"
#include <array>

class JsonValue;

/// <summary>
/// コクピット ↔ 機体設定（装備画面。20_Loadout.md）の行き来の時間軸とカメラ。描画は持たない（HubScene が見え方を受け取って反映する）。
///  入る：コンソールが潰れる → 画面全体のノイズの山でカメラを外（機体の斜め前）へ切り替え
///        → 神殿だけにノイズ（機体は ID マスクで外す）の山で神殿を消す → 装備の UI が出る
///  出る：UI が潰れる → 神殿にノイズの山で神殿を戻す → 画面全体のノイズの山でコクピットへ切り替え → コンソールが戻る
///  付け替え：機体だけにノイズの山。山のてっぺんでモデルを入れ替える（ConsumeSwapPeak）
/// ノイズは GlitchEffect（強さ・かける所・見た目の値はここが決めて HubScene が渡す）。調整値は Hub.json の "loadout"。
/// </summary>
class HubLoadoutSequence {
public:
	enum class Phase {
		Idle,
		Entering,   // コクピット → 機体設定
		Browsing,   // 装備を選んでいる
		Leaving,    // 機体設定 → コクピット
		Finished,   // コクピットに戻り切った（持ち主がコクピットの状態へ戻す）
	};

	/// <summary>ある時刻の見え方</summary>
	struct View {
		bool exterior = false;          // カメラが外（機体を外から見る）。false はコクピットの中の視点
		bool templeVisible = true;      // 神殿（空・床・リフト・表示板も）を描く
		float consoleCollapse = 0.0f;   // コクピットのステージ選択の UI の潰れ 0..1（1 で消え切った）
		float uiTime = -1.0f;           // 装備の UI が出始めてからの秒（負はまだ）
		float uiCollapse = 0.0f;        // 装備の UI の潰れ 0..1（出るとき）
		float glitch = 0.0f;            // ノイズの強さ 0..1
		GlitchEffect::MaskMode glitchMask = GlitchEffect::MaskMode::Screen;
		bool inputEnabled = false;      // 装備の UI の入力を受け付ける
	};

	HubLoadoutSequence();
	void ResetToDefaults();

	void StartEnter();
	void StartLeave();
	/// <summary>付け替えのノイズ（入れ替えは ConsumeSwapPeak が true になったフレーム）</summary>
	void StartSwap();
	void Stop();
	void Update(float deltaTime);

	Phase GetPhase() const { return phase_; }
	bool IsActive() const { return phase_ != Phase::Idle && phase_ != Phase::Finished; }
	bool IsFinished() const { return phase_ == Phase::Finished; }
	View Evaluate() const;
	/// <summary>付け替えのノイズが山のてっぺんを越えたフレームだけ true（モデルを入れ替える）</summary>
	bool ConsumeSwapPeak();

	/// <summary>機体のまわりを回す（右スティック・ドラッグ）。yaw は度。タブのカメラへ動いている途中ならそこで止める</summary>
	void Orbit(float yawDegrees);
	/// <summary>
	/// タブ（0 メイン武器 / 1 盾 / 2 飛行ユニット）の見やすい位置へカメラを動かす。回り込む角度は近い方を回る。
	/// immediate なら補間せずにその位置へ
	/// </summary>
	void SetShot(int tab, bool immediate);
	float GetOrbitStickSpeed() const { return orbitStickSpeed_; }
	float GetOrbitDragSpeed() const { return orbitDragSpeed_; }
	/// <summary>外のカメラ。mechOrigin は機体の原点（足元の中央）、正面は +Z</summary>
	void GetCamera(const Vector3& mechOrigin, Vector3& eye, Vector3& rotate, float& fovY) const;
	/// <summary>足元の輪（背景）の中心の高さと半径 [m]（機体の原点から）</summary>
	float GetFloorRingRadius() const { return floorRingRadius_; }
	static constexpr int kShotCount = 3;
	float GetGlitchTime() const { return glitchTime_; }
	const GlitchEffect::Params& GetGlitchParams() const { return glitchParams_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	// カメラの位置（機体の原点から。18m の機体）
	struct Shot {
		float yawDegrees = 0.0f;    // 正面（+Z）から回り込む角度（+ で機体の右手＝武器の側、180 で真後ろ）
		float pitchDegrees = 4.0f;  // 見下ろす角度（+ で下を向く）
		float distance = 30.0f;     // 見る点からの距離 [m]
		float height = 10.5f;       // 見る点の高さ [m]
	};
	static Shot LerpShot(const Shot& a, const Shot& b, float k);

	// center を山のてっぺんにした強さ（rise 秒で 0→peak、fall 秒で peak→0）
	static float Bump(float t, float center, float rise, float fall, float peak);
	float EnterUiStart() const { return templeHideTime_ + uiDelay_; }
	float LeaveEnd() const { return leaveCutTime_ + consoleReturnDelay_ + consoleCollapseSeconds_; }

	Phase phase_ = Phase::Idle;
	float time_ = 0.0f;         // 今の段階に入ってからの秒
	float uiTime_ = -1.0f;      // 装備の UI が出てからの秒（出る段階でも数え続ける）
	float swapTime_ = -1.0f;    // 付け替えのノイズの経過（負は無し）
	bool swapPeakPending_ = false;
	float glitchTime_ = 0.0f;   // ノイズのずれ方の時計
	Shot camera_{};             // 今のカメラ
	Shot tweenFrom_{};
	Shot tweenTo_{};
	float tweenTime_ = -1.0f;   // タブのカメラへ動いている経過（負は止まっている）

	// 調整値：入る（コンソールを選んでからの秒）
	float consoleCollapseSeconds_ = 0.35f;   // ステージ選択の UI が潰れるまで（戻るときも同じ秒で開く）
	float cutTime_ = 0.45f;                  // 画面全体のノイズの山。ここで外へ切り替える
	float cutRise_ = 0.2f;
	float cutFall_ = 0.3f;
	float templeHideTime_ = 1.05f;           // 神殿のノイズの山。ここで神殿を消す
	float templeRise_ = 0.35f;
	float templeFall_ = 0.35f;
	float uiDelay_ = 0.25f;                  // 神殿を消してから UI が出るまで
	float inputDelay_ = 0.35f;               // UI が出てから入力を受け付けるまで
	// 調整値：出る（戻るを押してからの秒）
	float uiCollapseSeconds_ = 0.3f;         // UI が潰れるまで
	float templeShowTime_ = 0.55f;           // 神殿のノイズの山。ここで神殿を戻す
	float leaveCutTime_ = 1.05f;             // 画面全体のノイズの山。ここでコクピットへ切り替える
	float consoleReturnDelay_ = 0.1f;        // 切り替えてからステージ選択の UI が開き始めるまで
	// 調整値：付け替え
	float swapPeakTime_ = 0.12f;
	float swapFall_ = 0.3f;
	float swapIntensity_ = 0.85f;
	// 調整値：ノイズ
	float cutIntensity_ = 1.0f;
	float templeIntensity_ = 1.0f;
	GlitchEffect::Params glitchParams_{};
	// 調整値：外のカメラ。タブごとの位置（メイン武器＝右前・盾＝左前・飛行ユニット＝真後ろ。2026-11-05 ユーザーのスケッチ）
	std::array<Shot, kShotCount> shots_{};
	float tweenSeconds_ = 0.6f;       // タブを替えたときにカメラが動く秒
	float fovYDegrees_ = 40.0f;
	float orbitStickSpeed_ = 120.0f;  // 右スティックを倒し切ったときの回る速さ [度/秒]
	float orbitDragSpeed_ = 0.3f;     // ドラッグ 1px あたりの回転 [度]
	float floorRingRadius_ = 7.0f;    // 足元の輪の半径 [m]
};
