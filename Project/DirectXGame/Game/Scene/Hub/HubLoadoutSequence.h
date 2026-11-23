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

	/// <summary>調整値を既定にする</summary>
	HubLoadoutSequence();
	/// <summary>調整値（入る・出る・付け替えの秒数、ノイズ、カメラ）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>コクピットから機体設定へ入り始める（カメラはメイン武器のタブの位置から）</summary>
	void StartEnter();
	/// <summary>機体設定からコクピットへ出始める</summary>
	void StartLeave();
	/// <summary>付け替えのノイズを始める（入れ替えは ConsumeSwapPeak が true になったフレーム）</summary>
	void StartSwap();
	/// <summary>Idle に戻す</summary>
	void Stop();
	/// <summary>時刻を進め、段階を移し、カメラをタブの位置へ寄せる</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>今の段階を返す</summary>
	/// <returns>段階</returns>
	Phase GetPhase() const { return phase_; }
	/// <summary>行き来か機体設定の途中かを返す</summary>
	/// <returns>Idle・Finished 以外なら true</returns>
	bool IsActive() const { return phase_ != Phase::Idle && phase_ != Phase::Finished; }
	/// <summary>コクピットに戻り切ったかを返す（持ち主がコクピットの状態へ戻す）</summary>
	/// <returns>Finished なら true</returns>
	bool IsFinished() const { return phase_ == Phase::Finished; }
	/// <summary>今の時刻の見え方（カメラの内外・神殿の表示・UI の出と潰れ・ノイズ）を求める</summary>
	/// <returns>見え方</returns>
	View Evaluate() const;
	/// <summary>付け替えのノイズが山のてっぺんを越えたフレームだけ true を返す（モデルを入れ替える）。返したら下ろす</summary>
	/// <returns>越えたフレームなら true</returns>
	bool ConsumeSwapPeak();

	/// <summary>機体のまわりを回す（右スティック・ドラッグ）。タブのカメラへ動いている途中ならそこで止める</summary>
	/// <param name="yawDegrees">回す角度 [度]</param>
	void Orbit(float yawDegrees);
	/// <summary>タブの見やすい位置へカメラを動かす。回り込む角度は近い方を回る</summary>
	/// <param name="tab">0 メイン武器 / 1 盾 / 2 飛行ユニット</param>
	/// <param name="immediate">true なら補間せずにその位置へ</param>
	void SetShot(int tab, bool immediate);
	/// <summary>右スティックで回す速さを返す</summary>
	/// <returns>速さ [度/秒]</returns>
	float GetOrbitStickSpeed() const { return orbitStickSpeed_; }
	/// <summary>ドラッグで回す速さを返す</summary>
	/// <returns>速さ [度/px]</returns>
	float GetOrbitDragSpeed() const { return orbitDragSpeed_; }
	/// <summary>外のカメラを求める（機体の正面は +Z）</summary>
	/// <param name="mechOrigin">機体の原点（足元の中央）のワールド位置</param>
	/// <param name="eye">カメラの位置の受け取り先</param>
	/// <param name="rotate">カメラの回転 [ラジアン] の受け取り先</param>
	/// <param name="fovY">縦の画角 [ラジアン] の受け取り先</param>
	void GetCamera(const Vector3& mechOrigin, Vector3& eye, Vector3& rotate, float& fovY) const;
	/// <summary>足元の輪（背景）の半径を返す（機体の原点から）</summary>
	/// <returns>半径 [m]</returns>
	float GetFloorRingRadius() const { return floorRingRadius_; }
	static constexpr int kShotCount = 3;
	/// <summary>ノイズの模様を動かす経過時間を返す</summary>
	/// <returns>経過 [秒]</returns>
	float GetGlitchTime() const { return glitchTime_; }
	/// <summary>ノイズの見た目の値を返す（HubScene が GlitchEffect へ渡す）</summary>
	/// <returns>ノイズの値</returns>
	const GlitchEffect::Params& GetGlitchParams() const { return glitchParams_; }

	/// <summary>調整値（Hub.json の "loadout"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	// カメラの位置（機体の原点から。18m の機体）
	struct Shot {
		float yawDegrees = 0.0f;    // 正面（+Z）から回り込む角度（+ で機体の右手＝武器の側、180 で真後ろ）
		float pitchDegrees = 4.0f;  // 見下ろす角度（+ で下を向く）
		float distance = 30.0f;     // 見る点からの距離 [m]
		float height = 10.5f;       // 見る点の高さ [m]
	};
	/// <summary>2 つのカメラの位置を補間する</summary>
	/// <param name="a">k = 0 の位置</param>
	/// <param name="b">k = 1 の位置</param>
	/// <param name="k">補間の割合</param>
	/// <returns>補間した位置</returns>
	static Shot LerpShot(const Shot& a, const Shot& b, float k);

	/// <summary>center を山のてっぺんにしたノイズの強さを求める（rise 秒で 0→peak、fall 秒で peak→0）</summary>
	/// <param name="t">時刻 [秒]</param>
	/// <param name="center">てっぺんの時刻 [秒]</param>
	/// <param name="rise">上がる秒数 [秒]</param>
	/// <param name="fall">下がる秒数 [秒]</param>
	/// <param name="peak">てっぺんの強さ</param>
	/// <returns>強さ</returns>
	static float Bump(float t, float center, float rise, float fall, float peak);
	/// <summary>入るときに装備の UI が出始める時刻を返す</summary>
	/// <returns>時刻 [秒]</returns>
	float EnterUiStart() const { return templeHideTime_ + uiDelay_; }
	/// <summary>出るときにコンソールが戻り切る時刻を返す</summary>
	/// <returns>時刻 [秒]</returns>
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
