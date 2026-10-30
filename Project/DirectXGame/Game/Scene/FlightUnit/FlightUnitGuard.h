#pragma once
#include "FlightUnitPart.h"

class EffectPrimitiveRenderer;

/// <summary>
/// 防御系（20_Loadout.md §3.2）。
/// 常時：クールタイム付きで被ダメージを軽減する（弾は消さない＝ジャスト回避は普段どおり起きる）。
/// RB：耐久力付きのバリア（HP の上限 × 割合）。バリアの間は敵弾がバリアで消え、ジャスト回避は起きない。
/// 耐久力が尽きるか、制限時間で消える。「ジャスト回避をしなくてよい」という選び方として置く。
/// </summary>
class FlightUnitGuard : public FlightUnitSkill {
public:
	FlightUnitGuard();
	~FlightUnitGuard() override;

	void Initialize(IFlightUnitHost* host, Camera* camera) override;
	bool Activate() override;
	void Cancel(bool restoreTime) override;
	bool IsActive() const override { return active_; }
	float GetActiveRatio() const override;
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	float GetMaxHpScale() const override { return maxHpScale_[LevelIndex()]; }
	void Update(float dt, bool canAct) override;
	void Draw() override;

	int ModifyIncomingDamage(int damage) override;
	bool AbsorbHit(int damage) override;

	Vector4 GetColor() const override { return color_; }
	void LoadTuning(const JsonValue& node) override;
	void SaveTuning(JsonValue& node) const override;
	void OnImGui() override;

private:
	// 耐久力を減らす（尽きたら消す）
	void Drain(int damage);

	// ----- 常時：軽減 -----
	float passiveReduce_[3]   = { 0.5f, 0.5f, 0.5f };   // 減らす割合
	float passiveCooldown_[3] = { 8.0f, 6.0f, 5.0f };

	// ----- RB：バリア -----
	float barrierHpRatio_[3]  = { 0.50f, 0.75f, 1.00f }; // 耐久力＝HP の上限 × これ
	float barrierDuration_[3] = { 8.0f, 8.0f, 8.0f };
	float cooldown_[3]        = { 30.0f, 27.0f, 24.0f };
	float maxHpScale_[3]      = { 1.05f, 1.10f, 1.15f };   // 常時：HP の上限
	float barrierRadius_ = 2.0f;
	float contactHitInterval_ = 0.3f;   // 弾でない攻撃（突進・近接の判定）が続けて当たっても、この間は 1 回分だけ減らす
	Vector4 color_{ 0.40f, 0.70f, 1.00f, 1.0f };
	float barrierAlpha_ = 0.25f;
	float flashDuration_ = 0.25f;       // 常時の軽減が効いたときにバリアを一瞬見せる

	bool active_ = false;
	float timer_ = 0.0f;
	float durability_ = 0.0f;
	float durabilityMax_ = 1.0f;
	float contactTimer_ = 0.0f;
	float passiveTimer_ = 0.0f;   // 残り（0 で次の被弾を軽減できる）
	float flashTimer_ = 0.0f;
	float hitFlash_ = 0.0f;       // バリアが受けたときに明るくする
	std::unique_ptr<EffectPrimitiveRenderer> vis_;
};
