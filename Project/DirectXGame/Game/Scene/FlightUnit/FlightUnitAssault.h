#pragma once
#include "FlightUnitPart.h"
#include "Matrix4x4.h"

/// <summary>
/// 攻撃系（20_Loadout.md §3.1）。背中のバックパックに、上へファンネル大 2 基・下の左右へファンネル小 2 基を付ける。
/// 常時：ファンネル大 2 基の砲口から左右交互に、レティクルの位置へ支援射撃（レティクルが敵に重なっていればその敵へ強ホーミング）。
///       ファンネル大の砲身は付いたままレティクルの方へ向く。
/// RB：4 基がバックパックから離れて自機のまわりの輪を回り、数秒撃つ。レティクルが重なっている敵を優先し、
///     重なっていなければ画面上の敵を撃つ。終わるとバックパックへ戻り、戻るまで常時の支援射撃は止まる。
/// ロックはしない（必殺技の傲慢サンダーと役割を分ける）。
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

	bool UsesBackpack() const override { return true; }
	void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) override;
	void UpdateModels(const Matrix4x4& backpackWorld) override;
	void DrawModels(DirectXCore* dxCore, bool idPass) override;

	Vector4 GetColor() const override { return color_; }
	void LoadTuning(const JsonValue& node) override;
	void SaveTuning(JsonValue& node) const override;
	void OnImGui() override;

private:
	// 0,1 = 大（バックパックの上・常時の支援射撃の砲台）、2,3 = 小（下の左右）
	static constexpr int kFunnelCount = 4;
	static constexpr int kLargeCount = 2;

	struct Funnel {
		std::unique_ptr<Object3DInstance> model;
		Transform mount{ { 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } };   // バックパックのローカル
		Vector3 aimDir{ 0.0f, 0.0f, 1.0f };      // 飛んでいる間の向き（ワールド）
		Vector3 turretDir{ 0.0f, 0.0f, 1.0f };   // 付いている間の砲身の向き（ワールド。大だけレティクルへ向く）
		Vector3 mountForward{ 0.0f, 0.0f, 1.0f };// 付けた所の前（ワールド。最後の UpdateModels）
		Matrix4x4 world{};                       // 最後に作った行列
		bool hasWorld = false;
		float fireTimer = 0.0f;
	};

	void UpdatePassive(float dt, bool canAct);
	void UpdateFunnels(float dt, bool canAct);
	// ファンネル i の輪の位置（自機のまわり。カメラの面に沿う）
	Vector3 FunnelHome(int i) const;
	IImGuiEditable* PickFunnelTarget(int i);
	// 0＝バックパックに付いている 〜 1＝輪を回っている
	float ReleaseRatio() const;
	// 砲口（モデルのローカル +Z の先）のワールド位置
	Vector3 MuzzlePosition(const Funnel& f) const;

	// ----- 常時：支援射撃 -----
	float passiveInterval_[3]    = { 0.80f, 0.65f, 0.50f };
	float passiveDamageScale_[3] = { 0.30f, 0.40f, 0.50f };
	float turretTurnSpeed_ = 8.0f;   // 砲身がレティクルへ向く速さ（大きいほど速い）
	float muzzleZ_ = 0.5f;           // 砲口（モデルのローカル Z。モデルは長い辺が約 1）

	// ----- RB：ファンネル -----
	float funnelDuration_[3]    = { 6.0f, 7.0f, 8.0f };
	float funnelDamageScale_[3] = { 0.50f, 0.65f, 0.80f };
	float cooldown_[3]          = { 25.0f, 23.0f, 20.0f };
	float attackScale_[3]       = { 1.05f, 1.10f, 1.15f };   // 常時：自機の攻撃力（射撃・近接・支援射撃）
	float funnelFireInterval_ = 0.35f;
	float funnelRadius_ = 2.2f;       // 自機のまわりの輪の半径
	float funnelSpin_ = 1.2f;         // 輪の回る速さ（rad/s）
	float funnelDeploy_ = 0.25f;      // バックパックから輪へ出ていく時間
	float funnelReturn_ = 0.4f;       // 輪からバックパックへ戻る時間
	Vector4 color_{ 1.0f, 0.55f, 0.25f, 1.0f };

	bool active_ = false;
	bool returning_ = false;
	float timer_ = 0.0f;
	float returnTimer_ = 0.0f;
	float spinAngle_ = 0.0f;
	float passiveTimer_ = 0.0f;
	int passiveNext_ = 0;             // 次に撃つファンネル大
	std::array<Funnel, kFunnelCount> funnels_;
	std::vector<IImGuiEditable*> screenEnemies_;
	unsigned int pickSeed_ = 1;
};
