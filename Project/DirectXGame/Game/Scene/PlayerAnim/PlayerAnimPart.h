#pragma once
#include "Vector2.h"
#include <array>
#include <cstdint>
#include <string>
#include <vector>

class AnimatedObject3DInstance;
class JsonValue;

/// <summary>
/// 自機のアニメの状態。JSON（Resources/Json/Tuning/PlayerAnim/*.json の "states"）のキーと 1 対 1（kStateKeys）。
/// 土台（いつも 1 つ）・押している間（SetHeld）・1 回きり（Trigger）の 3 種類
/// </summary>
enum class PlayerAnimState : int {
	// ----- 土台（モードと移動から PlayerAnimPart が選ぶ）-----
	RailStand,           // STG の立ち姿勢（射撃中）
	RailGlide,           // STG の滑空・前へ倒れた姿勢（チャージ 1 段目が溜まった後。8 番 §2.2.1）
	BossIdle,            // ボス戦の待機
	BossRunForward,      // ボス戦の走り（自機の向きから見て前）
	BossRunBack,         // 後ろ
	BossRunLeft,         // 左
	BossRunRight,        // 右
	// ----- 押している間 -----
	ChargeHold,          // チャージ中（ボス戦だけ。STG は滑空がチャージの見た目）
	PrecisionAim,        // 精密射撃の構え
	ShieldGuard,         // 盾を構える（構える操作は今は無い。D で呼ぶ所を決める）
	StormLock,           // Storm Lock の発動中
	ShatterlineCharge,   // Shatterline の構え
	ShatterlineSlash,    // Shatterline の振り抜き（崩壊の間も）
	ShatterlineRecover,  // Shatterline の硬直
	// ----- 1 回きり -----
	Shoot,               // 通常の射撃
	ChargeRelease,       // チャージを放つ
	RailDodgeNeutral,    // STG の回避（入力なし）
	RailDodgeLeft,       // STG の回避（画面の左）
	RailDodgeRight,      // 右
	RailDodgeUp,         // 上
	RailDodgeDown,       // 下
	GroundDodgeNeutral,  // 地上の回避（入力なし）
	GroundDodgeForward,  // 地上の回避（自機の向きから見て前）
	GroundDodgeBack,     // 後ろ
	GroundDodgeLeft,     // 左
	GroundDodgeRight,    // 右
	RailHit,             // STG の被弾
	GroundHit,           // 地上の被弾
	Heal,                // 回復
	LandingTouchdown,    // Landing の着地（後で）
	Victory,             // クリア・勝利（後で）
	RailDeath,           // STG の死亡（後で。鳴らした後は SetSuspended(true) と同じく止まる）
	Count,
};

/// <summary>土台を選ぶモード</summary>
enum class PlayerAnimMode {
	Rail,    // STG（レール・雲の上・着陸の途中）
	Ground,  // 地上（ボス戦・着陸で足場に着いた後）
};

/// <summary>
/// 自機（PlayerModel）のアニメを決める（25_PlayerAnimClips.md）。シーンは「行動が始まった・終わった」を伝えるだけで、
/// どのクリップをどう鳴らすか（ループ・速度・フェード・優先度）は JSON が決める。
/// 鳴っている候補（土台・押している間・1 回きり・近接）のうち優先度のいちばん高いものを鳴らす（同じなら後から来た方）。
/// クリップが空の状態は fallback をたどり、それでも無ければ候補にならない（姿勢はそのまま）。
/// 近接（MeleeComboPart）のクリップは MeleeCombo.json が決め、ここでは優先度（common.json の meleePriority）だけ持つ。
/// 1 回きりと近接は、上に隠れている間も時間が進み、また一番上に戻ったら続きのコマから鳴らす
/// </summary>
class PlayerAnimPart {
public:
	/// <summary>鳴らす相手を受け取り、JSON を読む</summary>
	/// <param name="player">自機（nullptr なら何もしない）</param>
	void Initialize(AnimatedObject3DInstance* player);
	/// <summary>鳴らす相手を替える（シーンの読み込みで自機を作り直した時）</summary>
	/// <param name="player">自機</param>
	void SetPlayer(AnimatedObject3DInstance* player) { player_ = player; currentKey_ = kNoKey; }
	/// <summary>Resources/Json/Tuning/PlayerAnim/*.json を全部読み直す（読み直した後は今のクリップを鳴らし直す）</summary>
	void LoadTuning();

	/// <summary>毎フレーム：1 回きりと近接の時間を進め、いちばん優先度の高いものが替わったら鳴らす</summary>
	/// <param name="dt">経過時間 [秒]（自機の時間 TimeGroup::Player）</param>
	void Update(float dt);
	/// <summary>今の候補から選び直して、同じクリップでも最初から鳴らし直す（フェーズが替わった・リトライ・読み込みの後）</summary>
	void Replay();

	/// <summary>土台を選ぶモードを替える</summary>
	/// <param name="mode">モード</param>
	void SetMode(PlayerAnimMode mode) { mode_ = mode; }
	/// <summary>STG で滑空するか（チャージ 1 段目が溜まった後）</summary>
	/// <param name="glide">滑空するなら true</param>
	void SetRailGlide(bool glide) { railGlide_ = glide; }
	/// <summary>地上の移動（自機の向きから見た向き。x=右、y=前、長さ 0〜1）。走りの向きを選ぶのに使う</summary>
	/// <param name="localMove">移動</param>
	void SetGroundMove(const Vector2& localMove) { groundMove_ = localMove; }

	/// <summary>押している間の状態を始める・終える（毎フレーム呼んでよい）</summary>
	/// <param name="state">状態（押している間の種類）</param>
	/// <param name="on">押しているなら true</param>
	void SetHeld(PlayerAnimState state, bool on);
	/// <summary>1 回きりの状態を鳴らす（鳴っている途中に同じ状態が来たら最初から）</summary>
	/// <param name="state">状態（1 回きりの種類）</param>
	void Trigger(PlayerAnimState state);
	/// <summary>鳴っている 1 回きりの状態をやめる（隠れて残っている続きも捨てる）</summary>
	/// <param name="state">状態（1 回きりの種類）</param>
	void Stop(PlayerAnimState state);
	/// <summary>1 回きり・押している間を全部やめる（リトライ・Seek の後）</summary>
	void ClearActions();

	/// <summary>近接（MeleeComboPart）のクリップを鳴らす</summary>
	/// <param name="clip">.anim のパス</param>
	/// <param name="speed">再生速度</param>
	/// <param name="fade">フェードの時間 [秒]</param>
	/// <param name="loop">繰り返すか（溜めの構え）</param>
	void PlayMelee(const std::string& clip, float speed, float fade, bool loop);
	/// <summary>近接が終わった（次の段を待つ時間が切れた・溜めをやめた）</summary>
	void EndMelee();

	/// <summary>
	/// 自機のアニメを外（死亡演出など）に任せる間は止める。止めている間は何も鳴らさない。
	/// 止めるのをやめたら今の候補から鳴らし直す
	/// </summary>
	/// <param name="suspended">止めるなら true</param>
	void SetSuspended(bool suspended);
	/// <summary>状態を今すぐ鳴らしてから止める（STG の死亡など、外に任せる前の最後のクリップ）</summary>
	/// <param name="state">状態</param>
	void PlayAndSuspend(PlayerAnimState state);

	/// <summary>今の状態の表示と JSON の読み直し（Debug の StagePlay Tuning から）</summary>
	void OnImGui();

private:
	/// <summary>状態 1 つ分の設定（JSON の "states" の 1 項目）</summary>
	struct StateDef {
		std::string clip;                // .anim のパス（"/" を含まなければ clipPrefix + 名前 + ".anim"）。空なら fallback
		std::string railClip;            // STG で clip の代わりに鳴らす（空なら clip）
		std::string groundClip;          // 地上で clip の代わりに鳴らす（空なら clip）
		bool loop = true;                // 繰り返すか
		float speed = 1.0f;              // 再生速度
		float fade = -1.0f;              // この状態へ移るフェード [秒]（負なら common.json の defaultFade）
		int priority = 0;                // 優先度（大きいほど勝つ）
		float startTime = 0.0f;          // 最初のコマの時刻 [秒]
		bool startAtEnd = false;         // 最後のコマで止める（"startAt": "end"）
		float length = -1.0f;            // 1 回きりの長さ [秒]（負ならクリップの長さ ÷ 速度）
		PlayerAnimState fallback = PlayerAnimState::Count;  // クリップが空の時に代わりに使う状態
	};
	/// <summary>鳴っている 1 回きりの状態</summary>
	struct OneShot {
		PlayerAnimState state = PlayerAnimState::Count;
		float elapsed = 0.0f;            // 鳴り始めてから [秒]
		float length = 0.0f;             // 長さ [秒]
		uint32_t order = 0;              // 来た順（優先度が同じなら後の方が勝つ）
	};
	/// <summary>選んだ候補（鳴らすクリップと時刻）</summary>
	struct Choice {
		uint64_t key = 0;                // 替わったかを見る印（状態の番号・近接・来た順）
		std::string clip;
		bool loop = true;
		float speed = 1.0f;
		float fade = 0.0f;
		float time = 0.0f;               // 鳴らし始める時刻 [秒]（隠れていた間に進んだ分を含む）
		bool atEnd = false;              // 最後のコマで止める
		int priority = 0;
		uint32_t order = 0;
	};
	static constexpr uint64_t kNoKey = ~0ull;
	static constexpr size_t kStateCount = static_cast<size_t>(PlayerAnimState::Count);

	/// <summary>JSON の 1 ファイルの "states" と設定を読む</summary>
	/// <param name="root">ファイルの中身</param>
	/// <param name="fileName">ファイル名（重なりの警告に使う）</param>
	void ReadFile(const JsonValue& root, const std::string& fileName);
	/// <summary>状態のクリップが空なら fallback をたどる</summary>
	/// <param name="state">状態</param>
	/// <returns>クリップのある状態（無ければ Count）</returns>
	PlayerAnimState Resolve(PlayerAnimState state) const;
	/// <summary>モードと移動から土台の状態を選ぶ</summary>
	/// <returns>土台の状態</returns>
	PlayerAnimState ChooseBase() const;
	/// <summary>候補から優先度のいちばん高いものを選ぶ</summary>
	/// <param name="out">選んだ候補</param>
	/// <returns>候補があれば true</returns>
	bool Choose(Choice& out) const;
	/// <summary>状態を候補にする（クリップが無ければ false）</summary>
	/// <param name="state">状態</param>
	/// <param name="elapsed">鳴り始めてから [秒]</param>
	/// <param name="order">来た順</param>
	/// <param name="out">候補</param>
	/// <returns>候補にできれば true</returns>
	bool MakeChoice(PlayerAnimState state, float elapsed, uint32_t order, Choice& out) const;
	/// <summary>選び直し、替わっていれば（force なら替わっていなくても）鳴らす</summary>
	/// <param name="force">同じでも鳴らし直すなら true</param>
	void Apply(bool force);
	/// <summary>クリップのパスを作る（名前だけなら clipPrefix を付ける）</summary>
	/// <param name="clip">JSON に書いたクリップ</param>
	/// <returns>.anim のパス</returns>
	std::string ClipPath(const std::string& clip) const;
	/// <summary>今のモードで鳴らすクリップ（railClip / groundClip があればそちら、無ければ clip）</summary>
	/// <param name="d">状態の設定</param>
	/// <returns>JSON に書いたクリップ（空なら鳴らせない）</returns>
	const std::string& ModeClip(const StateDef& d) const;
	/// <summary>クリップの長さ [秒]（読めなければ 0）</summary>
	/// <param name="path">.anim のパス</param>
	/// <returns>長さ</returns>
	static float ClipDuration(const std::string& path);

	AnimatedObject3DInstance* player_ = nullptr;
	std::array<StateDef, kStateCount> defs_{};
	std::array<bool, kStateCount> held_{};
	std::array<uint32_t, kStateCount> heldOrder_{};
	std::vector<OneShot> oneShots_;
	uint32_t nextOrder_ = 1;

	PlayerAnimMode mode_ = PlayerAnimMode::Rail;
	bool railGlide_ = false;
	Vector2 groundMove_{ 0.0f, 0.0f };

	// 近接（MeleeComboPart）
	bool meleeActive_ = false;
	std::string meleeClip_;
	float meleeSpeed_ = 1.0f;
	float meleeFade_ = 0.0f;
	bool meleeLoop_ = false;
	float meleeElapsed_ = 0.0f;
	uint32_t meleeOrder_ = 0;

	// common.json の設定
	int meleePriority_ = 50;             // 近接の優先度
	float runThreshold_ = 0.2f;          // 地上でこの長さ以上動いたら走り
	float defaultFade_ = 0.15f;          // 状態に fade が無い時のフェード [秒]
	std::string clipPrefix_ = "Resources/Models/Player/PlayerModel/PlayerModel_";

	bool suspended_ = false;
	uint64_t currentKey_ = kNoKey;       // 今鳴らしている候補の印
	std::string currentClip_;            // 今鳴らしているクリップ（同じ候補でもモードが替わってクリップが替われば鳴らし直す）
	PlayerAnimState currentState_ = PlayerAnimState::Count;  // 表示用（近接なら Count）
	std::vector<std::string> loadMessages_;   // 読み込みの警告（ImGui に出す）
	int imguiTestState_ = 0;                  // ImGui の試し鳴らしで選んでいる状態
};
