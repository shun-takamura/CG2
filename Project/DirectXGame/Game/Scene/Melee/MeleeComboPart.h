#pragma once
#include "IMeleeComboHost.h"
#include <array>
#include <string>
#include <utility>
#include <vector>

class InputActionMap;

/// <summary>近接のボタン（B=近接弱 MeleeWeak / Y=近接強 MeleeStrong）</summary>
enum class MeleeButton {
	None,
	B,
	Y,
};

/// <summary>
/// 近接の技 1 つ分。判定の形と時間（出るまで・当たっている間・スキ・次を待つ時間）はプレハブの MeleeParams、
/// 威力・ゲージ・アニメはここで持つ（1 つのプレハブを威力違いで使い回せる）
/// </summary>
struct MeleeMove {
	std::string prefab;          // 判定のプレハブ名（空なら既定のプレハブ）
	float damageScale = 1.0f;    // 攻撃力に掛ける倍率
	float gaugeGain   = 3.0f;    // 当てた時に増える必殺技ゲージ（遅れて当てた時はシーンの倍率を掛ける）
	std::string clip;            // 再生する .anim のパス（空なら姿勢はそのまま）
	float clipSpeed   = 1.0f;    // クリップの再生速度
};

/// <summary>溜め攻撃の 1 段階分</summary>
struct MeleeChargeLevel {
	float holdTime = 0.0f;       // 溜め始めてからこの段階になるまでの時間 [秒]
	MeleeMove move;              // この段階で放つ技
};

/// <summary>溜め攻撃 1 種類分（単独 / B4 段目から / Y4 段目から）</summary>
struct MeleeChargeSet {
	static constexpr int kLevelCount = 3;
	std::string holdClip;                              // 溜めている間のクリップ（空なら姿勢はそのまま）
	std::array<MeleeChargeLevel, kLevelCount> levels{}; // 段階 1〜3
};

/// <summary>
/// 自機の近接コンボ（24 番）。B/Y どちらでも段が進む 4 段コンボと、Y+B の溜め攻撃（単独・B4 段目から・Y4 段目から）。
/// 技は「押した順番（routes）→ 無ければボタン × 段（base）」の順に探す。
/// 調整値は Resources/Json/Tuning/MeleeCombo.json。
/// </summary>
class MeleeComboPart {
public:
	static constexpr int kMaxStage = 4;

	/// <summary>溜め攻撃の種類</summary>
	enum class ChargeKind {
		Solo,     // コンボ外・1〜3 段目の途中から
		AfterB4,  // 4 段目が B の後
		AfterY4,  // 4 段目が Y の後
		Count,
	};

	/// <summary>シーンへの窓口を受け取り、調整値を読む</summary>
	/// <param name="host">シーンへの窓口</param>
	void Initialize(IMeleeComboHost* host);
	/// <summary>入力を読み、発生・コンボ・溜め・先行入力のタイマーを進める</summary>
	/// <param name="actions">アクションの入力（nullptr なら入力なし）</param>
	/// <param name="dt">経過時間 [秒]（自機の時間）</param>
	/// <param name="acceptInput">新しい入力を受け付けるか（分身カウンターの派生中は false）</param>
	void Update(const InputActionMap* actions, float dt, bool acceptInput);
	/// <summary>入力を通さずに指定の段を出す（分身カウンターの自動連撃用）</summary>
	/// <param name="button">ボタン</param>
	/// <param name="stage">段（1〜kMaxStage）</param>
	void StartStep(MeleeButton button, int stage);
	/// <summary>コンボの段と順番だけを 1 段目に戻す（出ている攻撃・スキはそのまま）</summary>
	void ResetCombo();
	/// <summary>攻撃・溜め・先行入力をすべて取り消す（シーク・ステージの作り直し）</summary>
	void Reset();

	/// <summary>攻撃の発生〜スキ中か、溜め中か（他の行動を禁止する）</summary>
	/// <returns>禁止中なら true</returns>
	bool IsLocked() const { return lockTimer_ > 0.0f || charging_; }
	/// <summary>コンボの途中か（攻撃中・溜め中・次の入力を待つ間。剣を出しておく判定）</summary>
	/// <returns>途中なら true</returns>
	bool IsEngaged() const { return IsLocked() || comboTimer_ > 0.0f; }
	/// <summary>溜め中か</summary>
	/// <returns>溜め中なら true</returns>
	bool IsCharging() const { return charging_; }
	/// <summary>いま出ている判定の、当てた時に増える必殺技ゲージ</summary>
	/// <returns>ゲージの量</returns>
	float GetCurrentGaugeGain() const { return currentGaugeGain_; }
	/// <summary>近接が届くかの表示に使うプレハブ（基本技 B1 段目）</summary>
	/// <returns>プレハブ名</returns>
	std::string GetReachPrefab() const;

	/// <summary>調整値を JSON から読む</summary>
	/// <returns>読めたら true（ファイルが無ければ既定値のまま false）</returns>
	bool LoadTuning();
	/// <summary>調整値を JSON へ書く</summary>
	void SaveTuning() const;
	/// <summary>状態と調整値の ImGui（Debug だけ）</summary>
	void OnImGui();

private:
	/// <summary>技を探す（押した順番 → ボタン × 段）</summary>
	/// <param name="button">ボタン</param>
	/// <param name="stage">段（1〜kMaxStage）</param>
	/// <param name="route">ここまで押した順番（例 "BBY"）</param>
	/// <returns>技</returns>
	const MeleeMove& FindMove(MeleeButton button, int stage, const std::string& route) const;
	/// <summary>次の段を出す（4 段目の次と、次を待つ時間が切れた後は 1 段目から）</summary>
	/// <param name="button">ボタン</param>
	/// <param name="cancelable">同時押しで溜めへ取り消せるか</param>
	void BeginStep(MeleeButton button, bool cancelable);
	/// <summary>技を出す（発生の予約・行動の禁止・アニメ）</summary>
	/// <param name="move">技</param>
	void BeginMove(const MeleeMove& move);
	/// <summary>予約していた判定を出す</summary>
	void SpawnPending();
	/// <summary>溜めに入る（種類は今の段と順番で決める）</summary>
	void BeginCharge();
	/// <summary>溜めを放つ</summary>
	void ReleaseCharge();
	/// <summary>溜めを進める（段階を上げ、離したか最後の段階に着いたら放つ）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateCharge(const InputActionMap* actions, float dt);
	/// <summary>先行入力を空にする</summary>
	void ClearBuffer();

	IMeleeComboHost* host_ = nullptr;

	// ----- 調整値 -----
	float inputBuffer_ = 0.2f;   // スキの終わりからこの秒数前までの入力を覚えておく
	float chordWindow_ = 0.06f;  // 押してからこの秒数以内にもう片方を押したら同時押し（溜め）
	float clipFade_    = 0.08f;  // 技のクリップへのフェード [秒]
	std::array<MeleeMove, kMaxStage> baseB_{};
	std::array<MeleeMove, kMaxStage> baseY_{};
	std::vector<std::pair<std::string, MeleeMove>> routes_; // 押した順番 → 技
	std::array<MeleeChargeSet, static_cast<size_t>(ChargeKind::Count)> charges_{};

	// ----- コンボの状態 -----
	int   stage_        = 0;     // 0=コンボ無し, 1..kMaxStage=いま出した段
	std::string route_;          // ここまで押した順番
	float comboTimer_   = 0.0f;  // 次の段を受け付ける残り [秒]（0 で 1 段目に戻る・姿勢を戻す）
	float lockTimer_    = 0.0f;  // 発生〜スキの残り [秒]
	float startupTimer_ = 0.0f;  // 判定が出るまでの残り [秒]
	bool  pending_      = false; // 判定の出待ちがあるか
	std::string pendingPrefab_;
	float pendingDamageScale_ = 1.0f;
	float pendingGaugeGain_   = 0.0f;
	float currentGaugeGain_   = 0.0f;
	bool  clipPlayed_   = false; // 技のクリップを再生したか（終わったら姿勢を戻す）

	// ----- 同時押しで溜めへ取り消すための、出す前の状態 -----
	bool  cancelable_       = false;
	MeleeButton stepButton_ = MeleeButton::None;
	float sinceStepStart_   = 0.0f;
	int   prevStage_        = 0;
	std::string prevRoute_;
	float prevComboTimer_   = 0.0f;

	// ----- 先行入力 -----
	bool  bufferedB_ = false;
	bool  bufferedY_ = false;
	float bufferedBAge_ = 0.0f;  // 押してからの時間 [秒]
	float bufferedYAge_ = 0.0f;
	MeleeButton lastBuffered_ = MeleeButton::None;

	// ----- 溜め -----
	bool  charging_    = false;
	ChargeKind chargeKind_ = ChargeKind::Solo;
	float chargeTimer_ = 0.0f;
	int   chargeLevel_ = 0;      // 1..kLevelCount
};
