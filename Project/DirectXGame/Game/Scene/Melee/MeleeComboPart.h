#pragma once
#include "IMeleeComboHost.h"
#include "Vector3.h"
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

/// <summary>技の中で 2 回目以降に出す判定（1 回目は技のプレハブの startup で出る）</summary>
struct MeleeExtraHit {
	float time = 0.0f;           // 技の始まりからの時間 [秒]
	std::string prefab;          // 判定のプレハブ名（空なら技のプレハブ。このプレハブの startup は使わない）
	float damageScale = 1.0f;    // 攻撃力に掛ける倍率
};

/// <summary>攻撃中の移動の種類</summary>
enum class MeleeMotionMode {
	None,     // 動かない（攻撃中の移動の倍率で、スティックで少し動ける今までどおり）
	Forward,  // 自機の向いている方へ自動で進む（ボス戦だけ。STG は奥へ進めないので振りだけ）
	Back,     // 自機の向きの反対へ自動で下がる（ボス戦だけ）
	Free,     // スティックで自由に動ける（攻撃中でも遅くしない）
};

/// <summary>攻撃中の移動 1 区間</summary>
struct MeleeMotion {
	float start = 0.0f;          // 技の始まりからの時間 [秒]
	float end   = 0.0f;
	MeleeMotionMode mode = MeleeMotionMode::None;
	float speed = 1.0f;          // Forward / Back は速さ [m/秒]、Free はいつもの移動の速さに掛ける倍率
};

/// <summary>攻撃中の前方ガード（end が start 以下なら無し）</summary>
struct MeleeGuard {
	float start = 0.0f;          // 技の始まりからの時間 [秒]
	float end   = 0.0f;
	float angle = 0.0f;          // 正面から左右それぞれ何度までを守るか [度]
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
	// 2 回目以降の判定（時間の順）。技全体の長さはプレハブの startup + 当たっている間 + スキなので、最後の判定まで収める
	std::vector<MeleeExtraHit> hits;
	std::vector<MeleeMotion> motions;  // 攻撃中の移動（重なる時は先に書いた方）
	MeleeGuard guard;                  // 前方ガード
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

/// <summary>Y+B の単独の溜めで掛かる強化の 1 段階分</summary>
struct MeleeBuffLevel {
	float attackScale   = 1.0f;  // 攻撃力に掛ける倍率（射撃・近接・支援とも）
	float moveScale     = 1.0f;  // 移動の速さに掛ける倍率
	float justWindowAdd = 0.0f;  // ジャスト回避の受付に足す時間 [秒]
	float duration      = 0.0f;  // 続く時間 [秒]
};

/// <summary>
/// 自機の近接コンボ（24 番）。B/Y どちらでも段が進む 4 段コンボと、Y+B の溜め（単独＝強化・B4 段目から・Y4 段目から＝溜め攻撃）。
/// 単独の溜めは攻撃を出さず、溜めた段階の強化を掛ける。強化中にもう一度溜めて高い段階で放つと、段階が上がり続く時間の差分が足される。
/// 強化が切れた後は buffLockout 秒だけ単独の溜めに入れない（その間の Y+B は普通の段）。
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
		Burst,    // RT+Y+B の大技（押している長さで威力、銃のチャージで回転斬りの多段の間隔）
		Count,
	};
	// 銃のチャージの段階の数（無し / 1 段階 / 2 段階）
	static constexpr int kGunChargeLevels = 3;

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
	/// <summary>入力を通さずに、溜めの構えを速く流して溜め攻撃を放つ（ジャスト回避の近接派生用。構えは autoChargeTime 秒）</summary>
	/// <param name="kind">溜めの種類（Solo なら強化を掛ける）</param>
	/// <param name="level">段階（1〜MeleeChargeSet::kLevelCount）</param>
	void StartChargeAttack(ChargeKind kind, int level);
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
	/// <summary>スーパーアーマー中か（RT+Y+B の大技の溜め〜振り切り。被弾してもひるまず吹き飛ばない。ダメージはそのまま）</summary>
	/// <returns>スーパーアーマー中なら true</returns>
	bool HasSuperArmor() const {
		return (charging_ && chargeKind_ == ChargeKind::Burst) || (moveActive_ && moveSuperArmor_);
	}
	/// <summary>いま出ている判定の、当てた時に増える必殺技ゲージ</summary>
	/// <returns>ゲージの量</returns>
	float GetCurrentGaugeGain() const { return currentGaugeGain_; }
	/// <summary>いま出している技の攻撃中の移動</summary>
	/// <returns>今の区間（無ければ mode が None）</returns>
	MeleeMotion GetCurrentMotion() const;
	/// <summary>いま前方ガード中か</summary>
	/// <param name="outAngle">守る角度 [度]（ガード中だけ書く）</param>
	/// <returns>ガード中なら true</returns>
	bool IsGuarding(float& outAngle) const;
	/// <summary>同時押しとみなす時間（RT+Y+B の見分けで、シーンが射撃を待つ時間にも使う）</summary>
	/// <returns>時間 [秒]</returns>
	float GetChordWindow() const { return chordWindow_; }
	/// <summary>近接が届くかの表示に使うプレハブ（基本技 B1 段目）</summary>
	/// <returns>プレハブ名</returns>
	std::string GetReachPrefab() const;

	/// <summary>いま掛かっている強化の段階</summary>
	/// <returns>0＝強化なし、1〜MeleeChargeSet::kLevelCount</returns>
	int GetBuffLevel() const { return buffLevel_; }
	/// <summary>強化の残り時間</summary>
	/// <returns>残り [秒]（強化なしなら 0）</returns>
	float GetBuffRemaining() const { return buffTimer_; }
	/// <summary>強化の攻撃力の倍率</summary>
	/// <returns>倍率（強化なしなら 1）</returns>
	float GetBuffAttackScale() const { return buffLevel_ > 0 ? buffLevels_[static_cast<size_t>(buffLevel_ - 1)].attackScale : 1.0f; }
	/// <summary>強化の移動の速さの倍率</summary>
	/// <returns>倍率（強化なしなら 1）</returns>
	float GetBuffMoveScale() const { return buffLevel_ > 0 ? buffLevels_[static_cast<size_t>(buffLevel_ - 1)].moveScale : 1.0f; }
	/// <summary>強化でジャスト回避の受付に足す時間</summary>
	/// <returns>足す時間 [秒]（強化なしなら 0）</returns>
	float GetBuffJustWindowAdd() const { return buffLevel_ > 0 ? buffLevels_[static_cast<size_t>(buffLevel_ - 1)].justWindowAdd : 0.0f; }
	/// <summary>強化中のビームの刃の面の色</summary>
	/// <returns>色（HDR 可）</returns>
	const Vector3& GetBuffBladeCoreColor() const { return buffBladeCoreColor_; }
	/// <summary>強化中のビームの刃の縁の色</summary>
	/// <returns>色（HDR 可）</returns>
	const Vector3& GetBuffBladeEdgeColor() const { return buffBladeEdgeColor_; }

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
	/// <summary>技の時間を進め、時間の来た 2 回目以降の判定を出す（スキが明けたら技を終える）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateMoveTimeline(float dt);
	/// <summary>段と順番から溜めの種類を決める（4 段目を出した後なら 4 段目のボタン、それ以外は単独）</summary>
	/// <param name="stage">いまの段</param>
	/// <param name="route">ここまで押した順番</param>
	/// <returns>溜めの種類</returns>
	static ChargeKind ResolveChargeKind(int stage, const std::string& route);
	/// <summary>Y+B の同時押しで入る溜めの種類（RT も押していれば大技、そうでなければ段と順番で決める）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="stage">いまの段</param>
	/// <param name="route">ここまで押した順番</param>
	/// <returns>溜めの種類</returns>
	static ChargeKind ChordChargeKind(const InputActionMap* actions, int stage, const std::string& route);
	/// <summary>大技の溜めに入った時に銃のチャージを使い、回転斬りがもう一度当たれるまでの間隔を決める</summary>
	void TakeBurstGunCharge();
	/// <summary>その種類の溜めに今入れるか（単独＝強化は、最後の段階の強化中と切れた後の待ちの間は入れない）</summary>
	/// <param name="kind">溜めの種類</param>
	/// <returns>入れるなら true</returns>
	bool CanCharge(ChargeKind kind) const;
	/// <summary>溜めに入る</summary>
	/// <param name="kind">溜めの種類</param>
	void BeginCharge(ChargeKind kind);
	/// <summary>単独の溜めを放つ：強化を掛け（強化中なら段階を上げて時間の差分を足す）、かける時のクリップを流す</summary>
	/// <param name="level">溜めた段階（1〜MeleeChargeSet::kLevelCount）</param>
	void ReleaseBuff(int level);
	/// <summary>強化の残り時間と、切れた後の待ちを進める</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateBuff(float dt);
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
	float chordWindow_ = 0.06f;  // 技を出してからこの秒数以内にもう片方を押したら、出した段を取り消して溜め
	float clipFade_    = 0.08f;  // 技のクリップへのフェード [秒]
	float autoChargeTime_ = 0.3f; // 入力を通さない溜め（ジャスト回避の近接派生）で、溜めのクリップを速く流す時間 [秒]
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
	// ----- いま出している技（2 回目以降の判定・移動・ガード） -----
	bool  moveActive_   = false; // 技の発生〜スキの間
	float moveElapsed_  = 0.0f;  // 技の始まりからの時間 [秒]
	size_t nextHit_     = 0;     // 次に出す 2 回目以降の判定
	MeleeMove currentMove_;      // 技の写し（表を編集しても出ている技は変わらない）
	std::string currentPrefab_;  // 技のプレハブ（2 回目以降の判定の prefab が空の時に使う）

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
	// 段を出した後、動けるようになった最初のフレームに Y と B を両方押していたら溜めにする（押しっぱなしで受付を待てる）。
	// 溜めを放った後は立てない（最大まで溜めて自動で放った後に押し続けていても、もう一度溜めない）
	bool  holdChordOnUnlock_ = false;

	// ----- 溜め -----
	bool  charging_    = false;
	ChargeKind chargeKind_ = ChargeKind::Solo;
	float chargeTimer_ = 0.0f;
	int   chargeLevel_ = 0;      // 1..kLevelCount
	bool  autoCharge_  = false;  // 入力を通さない溜め中か（autoChargeTime_ 経ったら autoLevel_ で放つ）
	int   autoLevel_   = 0;

	// ----- 大技（RT+Y+B） -----
	// 銃のチャージ（無し / 1 / 2 段階）ごとの、回転斬りが同じ相手にもう一度当たれるまでの実時間 [秒]（調整値）
	std::array<float, kGunChargeLevels> burstRehitIntervals_{};
	float burstRehitInterval_ = 0.0f; // 今の大技の回転斬りの間隔（溜めに入った時に決める）
	float moveRehitInterval_  = 0.0f; // いま出している技の 2 回目以降の判定に付ける間隔（0＝1 回だけ。大技の回転斬りだけ使う）
	bool  moveSuperArmor_     = false; // いま出している技がスーパーアーマー付きか（大技だけ）

	// ----- 強化（単独の溜め） -----
	std::array<MeleeBuffLevel, MeleeChargeSet::kLevelCount> buffLevels_{}; // 段階 1〜3（調整値）
	float buffLockout_     = 0.0f; // 切れた後に単独の溜めに入れない時間 [秒]（調整値）
	float buffReleaseLock_ = 0.0f; // かける時のクリップの間、他の行動を禁止する時間 [秒]（調整値）
	Vector3 buffBladeCoreColor_{}; // 強化中の刃の面の色（調整値。いつもの色は武器の bladeLook）
	Vector3 buffBladeEdgeColor_{}; // 強化中の刃の縁の色（調整値）
	int   buffLevel_       = 0;    // 0＝強化なし
	float buffTimer_       = 0.0f; // 強化の残り [秒]
	float buffLockoutTimer_ = 0.0f; // 切れた後の待ちの残り [秒]
};
