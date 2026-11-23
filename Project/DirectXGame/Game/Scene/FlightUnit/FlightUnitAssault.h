#pragma once
#include "FlightUnitPart.h"
#include "Matrix4x4.h"

/// <summary>
/// 攻撃系（20_Loadout.md §3.1）。背中のバックパックに、上へファンネル大 2 基・下の左右へファンネル小 2 基を付ける。
/// 常時：ファンネル大 2 基の砲口から左右交互に、レティクルの位置へ支援射撃（レティクルが敵に重なっていればその敵へ強ホーミング）。
///       ファンネル大の砲身は付いたままレティクルの方へ向く。
/// RB：4 基がバックパックから離れて自機のまわりの輪を回り、数秒撃つ。レティクルが重なっている敵を優先し、
///     重なっていなければ画面上の敵を撃つ。終わるとバックパックへ戻り、戻るまで常時の支援射撃は止まる。
/// ロックはしない（必殺技の Storm Lock と役割を分ける）。
/// </summary>
class FlightUnitAssault : public FlightUnitSkill {
public:
	/// <summary>4 基のファンネルの付ける所の初期値を決める</summary>
	FlightUnitAssault();
	~FlightUnitAssault() override;

	/// <summary>シーンへの窓口とカメラを受け取る</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">カメラ</param>
	void Initialize(IFlightUnitHost* host, Camera* camera) override;
	/// <summary>4 基をバックパックから離して輪を回らせ、撃ち始める（戻る途中なら今の離れ具合から出ていく）</summary>
	/// <returns>出せたら true</returns>
	bool Activate() override;
	/// <summary>撃つのをやめ、ファンネルをバックパックへ戻し始める</summary>
	/// <param name="restoreTime">使わない（攻撃系は時間の倍率に触らない）</param>
	void Cancel(bool restoreTime) override;
	/// <summary>ファンネルが撃っている途中かを返す</summary>
	/// <returns>途中なら true</returns>
	bool IsActive() const override { return active_; }
	/// <summary>技の残りを返す</summary>
	/// <returns>1→0</returns>
	float GetActiveRatio() const override;
	/// <summary>今の段階のクールタイムを返す</summary>
	/// <returns>クールタイム [秒]</returns>
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	/// <summary>今の段階の攻撃力の補正を返す</summary>
	/// <returns>攻撃力に掛ける倍率</returns>
	float GetAttackScale() const override { return attackScale_[LevelIndex()]; }
	/// <summary>常時の支援射撃とファンネルを進める</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">操作できるか</param>
	void Update(float dt, bool canAct) override;

	/// <summary>バックパックを背負うかを返す</summary>
	/// <returns>常に true</returns>
	bool UsesBackpack() const override { return true; }
	/// <summary>ファンネル 4 基のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) override;
	/// <summary>ファンネルの行列を作る（付いている間はバックパックの上、離れている間は輪の上）</summary>
	/// <param name="backpackWorld">バックパックのワールド行列</param>
	void UpdateModels(const Matrix4x4& backpackWorld) override;
	/// <summary>ファンネルを描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="idPass">true なら ID マスクへ描く</param>
	void DrawModels(DirectXCore* dxCore, bool idPass) override;

	/// <summary>クールタイムのバーの色を返す</summary>
	/// <returns>色（RGBA 0〜1）</returns>
	Vector4 GetColor() const override { return color_; }
	/// <summary>調整値を JSON から読む</summary>
	/// <param name="node">この系統の欄</param>
	void LoadTuning(const JsonValue& node) override;
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先（この系統の欄）</param>
	void SaveTuning(JsonValue& node) const override;
	/// <summary>調整値の ImGui を表示する</summary>
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

	/// <summary>ファンネル大の砲身をレティクルへ向け、付いている間は砲口から左右交互にレティクルへ支援射撃する</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">操作できるか（false なら撃たない）</param>
	void UpdatePassive(float dt, bool canAct);
	/// <summary>離れたファンネルを輪の上で回して狙った敵を撃たせ、時間が来たら戻す</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">操作できるか</param>
	void UpdateFunnels(float dt, bool canAct);
	/// <summary>ファンネル i の輪の位置（自機のまわり。カメラの面に沿う）を求める</summary>
	/// <param name="i">ファンネルの番号 0〜3</param>
	/// <returns>ワールド座標</returns>
	Vector3 FunnelHome(int i) const;
	/// <summary>ファンネル i が狙う敵を選ぶ（レティクルが重なっている敵を優先し、無ければ画面上の敵へ散らす）</summary>
	/// <param name="i">ファンネルの番号 0〜3</param>
	/// <returns>狙う敵。画面上に敵がいなければ nullptr</returns>
	IImGuiEditable* PickFunnelTarget(int i);
	/// <summary>ファンネルの離れ具合を返す</summary>
	/// <returns>0＝バックパックに付いている 〜 1＝輪を回っている</returns>
	float ReleaseRatio() const;
	/// <summary>砲口（モデルのローカル +Z の先）のワールド位置を求める</summary>
	/// <param name="f">ファンネル</param>
	/// <returns>ワールド座標</returns>
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
