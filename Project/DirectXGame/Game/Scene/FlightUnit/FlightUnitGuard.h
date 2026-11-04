#pragma once
#include "FlightUnitPart.h"
#include <array>

class EffectPrimitiveRenderer;

/// <summary>
/// 防御系（20_Loadout.md §3.2）。
/// 常時：クールタイム付きで被ダメージを軽減する（弾は消さない＝ジャスト回避は普段どおり起きる）。
/// RB：耐久力付きのバリア（HP の上限 × 割合）。バリアの間は敵弾がバリアで消え、ジャスト回避は起きない。
/// 耐久力が尽きるか、制限時間で消える。「ジャスト回避をしなくてよい」という選び方として置く。
/// モデル：背中に閉じたユニットを 2 基（バックパックの位置が基準・本体は描かない）。バリアを張ると、離れながら開いて
/// 自機のまわりを回り、終わると閉じながら背中へ戻る。背中の位置は 2 基別々、ユニットの中の左右のパーツは鏡写し。
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

	bool UsesBackpack() const override { return true; }
	bool DrawsBackpack() const override { return false; }
	void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) override;
	void UpdateModels(const Matrix4x4& backpackWorld) override;
	void DrawModels(DirectXCore* dxCore) override;

	int ModifyIncomingDamage(int damage) override;
	bool AbsorbHit(int damage) override;

	Vector4 GetColor() const override { return color_; }
	void LoadTuning(const JsonValue& node) override;
	void SaveTuning(JsonValue& node) const override;
	void OnImGui() override;

private:
	// 耐久力を減らす（尽きたら消す）
	void Drain(int damage);
	// 0＝背中で閉じている、1＝開いて回っている
	float ReleaseRatio() const;
	// ユニット unit（0/1）の回る位置
	Vector3 OrbitPosition(int unit) const;

	// ----- モデル -----
	// パーツ：0=Main, 1=FrontRight, 2=BackRight, 3=FrontLeft, 4=BackLeft（3・4 は 1・2 の鏡写しで置く）
	static constexpr int kPartCount = 5;
	static constexpr int kTunedParts = 3;
	static constexpr int kUnitCount = 2;
	struct Unit {
		std::array<std::unique_ptr<Object3DInstance>, kPartCount> parts;
		bool placed = false;
	};
	std::array<Unit, kUnitCount> units_;
	Transform stowMounts_[kUnitCount];         // 背中に付ける所（[自機の左, 右]。バックパックのローカル。左右は別々に調整する）
	Transform closedParts_[kTunedParts];       // 閉じたときのパーツ（ユニットのローカル。+Z が表、自機から見て右が -X）
	Transform openParts_[kTunedParts];         // 開いたときのパーツ
	float orbitRadius_ = 2.1f;
	float orbitHeight_ = 0.0f;
	float orbitTilt_ = 0.35f;     // 輪の傾き（ラジアン。カメラから見て奥が上がる）
	float orbitSpin_ = 1.2f;      // 回る速さ（ラジアン/秒）
	float orbitScale_ = 0.9f;     // 回っている間のユニットの大きさ（Main の長さ≒これ m）
	float deployTime_ = 0.35f;
	float returnTime_ = 0.45f;
	float deployTimer_ = 0.0f;
	float returnTimer_ = 0.0f;
	bool returning_ = false;
	float orbitAngle_ = 0.0f;
	bool preview_ = false;        // ImGui：止めたまま開き具合を見る
	float previewRatio_ = 1.0f;

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
