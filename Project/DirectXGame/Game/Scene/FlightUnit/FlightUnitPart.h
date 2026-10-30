#pragma once
#include "Score/LoadoutState.h"
#include "TimeGroup.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <array>
#include <memory>
#include <vector>

class Camera;
class IImGuiEditable;
class JsonValue;
class SpriteInstance;
class SpriteManager;

/// <summary>
/// 飛行ユニットがシーンへ頼むことだけを並べた窓口（IRailStageHost と同じ流儀。公開 API は増やさない）。
/// GetPlayer / GetTimeScale / SetTimeScale は IRailStageHost と同じ形なので、StagePlayScene の 1 つの override で両方を満たす。
/// </summary>
class IFlightUnitHost {
public:
	virtual ~IFlightUnitHost() = default;
	virtual IImGuiEditable* GetPlayer() const = 0;
	virtual float GetTimeScale(TimeGroup g) const = 0;
	virtual void SetTimeScale(TimeGroup g, float scale) = 0;
	// 自機の体の中心（ファンネル・バリアの基準）
	virtual Vector3 GetFlightUnitAnchor() const = 0;
	// レティクルが重なっている敵（無ければ null）と、レティクルの狙いの点
	virtual IImGuiEditable* GetReticleTarget() const = 0;
	virtual Vector3 GetReticleAimPoint() const = 0;
	// 画面に映っている敵（Enemy / Boss。敵弾は含めない）
	virtual void CollectOnScreenEnemies(std::vector<IImGuiEditable*>& out) = 0;
	virtual bool IsEnemyAlive(IImGuiEditable* e) const = 0;
	// 自機の通常弾と同じ弾を撃つ。damageScale は自機の攻撃力に掛ける。homingTarget があれば強ホーミング
	virtual void FireSupportBullet(const Vector3& from, const Vector3& dir, IImGuiEditable* homingTarget, float damageScale) = 0;
	// 球の中の敵弾を消し、そのダメージ（難易度の倍率の前）の合計を返す
	virtual int AbsorbEnemyBulletsInSphere(const Vector3& center, float radius) = 0;
};

/// <summary>
/// 飛行ユニットの系統ごとの中身（常時の効果＋RB の技）。系統ごとに 1 つ。段階（1..3）で値が変わる。
/// dt はすべて実時間（UI グループ）。RB のスローで World が遅くなっても技の時間・クールタイムは等速で減る。
/// </summary>
class FlightUnitSkill {
public:
	virtual ~FlightUnitSkill() = default;

	virtual void Initialize(IFlightUnitHost* host, Camera* camera) { host_ = host; camera_ = camera; }
	void SetLevel(int level) { level_ = level; }

	// RB の技を出す。出せたら true（クールタイムは技が終わってから数える）
	virtual bool Activate() = 0;
	// 技を途中で終える。restoreTime=false のときは時間の倍率に触らない（ジャスト回避・必殺技が握っている）
	virtual void Cancel(bool restoreTime) = 0;
	virtual bool IsActive() const = 0;
	// 技が時間の倍率を使う（ジャスト回避・必殺技が起きたら終わらせる）
	virtual bool UsesTimeScale() const { return false; }
	// 技の残り（1→0）。表示用
	virtual float GetActiveRatio() const = 0;
	virtual float GetCooldown() const = 0;
	// canAct＝操作できる（必殺技・ジャスト回避・死亡演出・着地の間は false）
	virtual void Update(float dt, bool canAct) = 0;
	virtual void Draw() {}

	// ----- 常時の効果 -----
	// ステータスの補正（攻撃系＝攻撃力・防御系＝HP の上限・機動系＝移動速度。20 番 §3.4）
	virtual float GetAttackScale() const { return 1.0f; }
	virtual float GetMaxHpScale() const { return 1.0f; }
	virtual float GetMoveSpeedScale() const { return 1.0f; }
	virtual float GetJustDodgeGaugeBonus() const { return 0.0f; }
	// ジャスト回避の受付を延ばす秒（StagePlay.json の dodge.justWindow に足す）
	virtual float GetJustDodgeWindowBonus() const { return 0.0f; }
	// 被弾のダメージ（難易度の倍率の後）を補正する
	virtual int ModifyIncomingDamage(int damage) { return damage; }
	// 被弾を吸う（true＝吸った。ジャスト回避もダメージも起きない）
	virtual bool AbsorbHit(int damage) { (void)damage; return false; }

	// クールタイムのバーの色
	virtual Vector4 GetColor() const = 0;
	virtual void LoadTuning(const JsonValue& node) = 0;
	virtual void SaveTuning(JsonValue& node) const = 0;
	virtual void OnImGui() {}

	// 段階 1..3 の値の JSON・ImGui（[段階1, 段階2, 段階3] の配列）。色は FlightUnitPart の表示でも使う
	static void ReadLevels(const JsonValue& node, const char* key, float (&v)[3]);
	static void WriteLevels(JsonValue& node, const char* key, const float (&v)[3]);
	static void DragLevels(const char* label, float (&v)[3], float speed, float minV, float maxV);
	static void ReadVec4(const JsonValue& node, const char* key, Vector4& v);
	static void WriteVec4(JsonValue& node, const char* key, const Vector4& v);

protected:
	int LevelIndex() const { return level_ < 1 ? 0 : (level_ > 3 ? 2 : level_ - 1); }
	// カメラの右・上・前（ワールド）
	void GetCameraBasis(Vector3& right, Vector3& up, Vector3& forward) const;

	IFlightUnitHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	int level_ = 1;
};

/// <summary>
/// 飛行ユニット（20_Loadout.md §3。サブ武器を兼ねる）の共通部分：付けている系統の切り替え・RB の入力・クールタイム・
/// 常時の効果の問い合わせ口・クールタイムの表示。中身は系統ごとの FlightUnitSkill（Assault / Guard / Mobility）。
/// 付けている系統と段階は LoadoutState が持つ（HUB の装備画面ができたらそこが書く）。シューティングとボス戦の両方で使う。
/// StagePlayScene は IFlightUnitHost を渡すだけで、技の中身は知らない。
/// </summary>
class FlightUnitPart {
public:
	FlightUnitPart();
	~FlightUnitPart();

	void Initialize(IFlightUnitHost* host, Camera* camera, SpriteManager* spriteManager);
	bool IsInitialized() const { return host_ != nullptr; }
	// LoadoutState の系統・段階に付け直す（技は止める）
	void SyncLoadout();

	/// <summary>
	/// 毎フレーム。dt は実時間（UI）。triggered＝RB が押されたフレーム。
	/// canAct＝操作できる。timeTaken＝ジャスト回避・必殺技が時間を握っている（スローの技はそこで終わる。20 番 §6）
	/// </summary>
	void Update(float dt, bool triggered, bool canAct, bool timeTaken);
	void Draw3D();
	void DrawUI();
	// 技を止め、クールタイムを 0 にする（フェーズの切り替え）
	void Reset();

	// ----- 常時の効果（付けていなければ素の値）-----
	float GetAttackScale() const;
	float GetMaxHpScale() const;
	float GetMoveSpeedScale() const;
	float GetJustDodgeGaugeBonus() const;
	float GetJustDodgeWindowBonus() const;
	int ModifyIncomingDamage(int damage);
	bool TryAbsorbHit(int damage);

	FlightUnitKind GetKind() const { return kind_; }
	// 機動系の RB のスロー中（自機の弾は World でなく Player の時間で進める）
	bool IsTimeSlowActive() const;

	bool LoadTuning();
	void SaveTuning() const;
	void OnImGui();

private:
	FlightUnitSkill* Current() const;

	IFlightUnitHost* host_ = nullptr;
	SpriteManager* spriteManager_ = nullptr;
	std::array<std::unique_ptr<FlightUnitSkill>, static_cast<size_t>(FlightUnitKind::Count)> skills_;
	FlightUnitKind kind_ = FlightUnitKind::Wings;
	float cooldownTimer_ = 0.0f;   // 残り（0 で撃てる）
	bool wasActive_ = false;

	// ----- クールタイムのバー（必殺技ゲージの下）-----
	std::unique_ptr<SpriteInstance> barBg_;
	std::unique_ptr<SpriteInstance> barFg_;
	Vector2 barPos_{ 60.0f, 80.0f };
	Vector2 barSize_{ 180.0f, 8.0f };
	Vector4 barBgColor_{ 0.10f, 0.10f, 0.20f, 0.8f };
	float barCoolingAlpha_ = 0.35f;   // クールタイム中は系統の色を薄く
	float readyPulse_ = 0.0f;         // 撃てるようになった瞬間に一度光らせる
	float readyPulseDuration_ = 0.4f;
};
