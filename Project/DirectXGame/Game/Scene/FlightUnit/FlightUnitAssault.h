#pragma once
#include "FlightUnitPart.h"

class EffectPrimitiveRenderer;

/// <summary>
/// 攻撃系（20_Loadout.md §3.1）。
/// 常時：レティクルの位置へ支援射撃（レティクルが敵に重なっていればその敵へ強ホーミング）。
/// RB：ファンネルを数秒展開する。ファンネルはレティクルが重なっている敵を優先し、重なっていなければ画面上の敵を撃つ。
/// ロックはしない（必殺技の傲慢サンダーと役割を分ける）。羽根の板が外れて飛ぶ見せ方は後で（今は小さな箱）。
/// </summary>
class FlightUnitAssault : public FlightUnitSkill {
public:
	FlightUnitAssault();
	~FlightUnitAssault() override;

	void Initialize(IFlightUnitHost* host, Camera* camera) override;
	bool Activate() override;
	void Cancel(bool restoreTime) override;
	bool IsActive() const override { return active_; }
	float GetActiveRatio() const override;
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	float GetAttackScale() const override { return attackScale_[LevelIndex()]; }
	void Update(float dt, bool canAct) override;
	void Draw() override;

	Vector4 GetColor() const override { return color_; }
	void LoadTuning(const JsonValue& node) override;
	void SaveTuning(JsonValue& node) const override;
	void OnImGui() override;

private:
	static constexpr int kMaxFunnels = 6;

	struct Funnel {
		std::unique_ptr<EffectPrimitiveRenderer> vis;
		Vector3 pos{ 0.0f, 0.0f, 0.0f };
		Vector3 aimDir{ 0.0f, 0.0f, 1.0f };
		float fireTimer = 0.0f;
	};

	void UpdatePassive(float dt);
	void UpdateFunnels(float dt, bool canAct);
	// ファンネル i の待機位置（自機のまわりの輪。カメラの面に沿う）
	Vector3 FunnelHome(int i, int count) const;
	IImGuiEditable* PickFunnelTarget(int i);

	// ----- 常時：支援射撃 -----
	float passiveInterval_[3]    = { 0.80f, 0.65f, 0.50f };
	float passiveDamageScale_[3] = { 0.30f, 0.40f, 0.50f };
	Vector2 passiveMuzzle_{ 0.9f, 0.6f };   // 撃ち出す所（カメラの右・上へずらす。左右交互）

	// ----- RB：ファンネル -----
	int   funnelCount_ = 4;
	float funnelDuration_[3]    = { 6.0f, 7.0f, 8.0f };
	float funnelDamageScale_[3] = { 0.50f, 0.65f, 0.80f };
	float cooldown_[3]          = { 25.0f, 23.0f, 20.0f };
	float attackScale_[3]       = { 1.05f, 1.10f, 1.15f };   // 常時：自機の攻撃力（射撃・近接・支援射撃）
	float funnelFireInterval_ = 0.35f;
	float funnelRadius_ = 2.2f;       // 自機のまわりの輪の半径
	float funnelSpin_ = 1.2f;         // 輪の回る速さ（rad/s）
	float funnelDeploy_ = 0.25f;      // 自機から輪へ出ていく時間
	Vector3 funnelScale_{ 0.12f, 0.12f, 0.45f };
	Vector4 color_{ 1.0f, 0.55f, 0.25f, 1.0f };

	bool active_ = false;
	float timer_ = 0.0f;
	float spinAngle_ = 0.0f;
	float passiveTimer_ = 0.0f;
	bool passiveRight_ = true;
	std::array<Funnel, kMaxFunnels> funnels_;
	std::vector<IImGuiEditable*> screenEnemies_;
	unsigned int pickSeed_ = 1;
};
