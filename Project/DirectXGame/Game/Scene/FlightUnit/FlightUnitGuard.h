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
	/// <summary>パーツの開いた形・閉じた形と、背中の 2 基の付ける所の初期値を決める</summary>
	FlightUnitGuard();
	~FlightUnitGuard() override;

	/// <summary>シーンへの窓口とカメラを受け取り、バリアの球の描画物を作る</summary>
	/// <param name="host">シーンへの窓口</param>
	/// <param name="camera">カメラ</param>
	void Initialize(IFlightUnitHost* host, Camera* camera) override;
	/// <summary>耐久力付きのバリアを張る（耐久力＝HP の上限 × 割合。戻る途中なら今の開き具合から出ていく）</summary>
	/// <returns>張れたら true</returns>
	bool Activate() override;
	/// <summary>バリアを消し、ユニットを閉じながら背中へ戻し始める</summary>
	/// <param name="restoreTime">使わない（防御系は時間の倍率に触らない）</param>
	void Cancel(bool restoreTime) override;
	/// <summary>バリアを張っている途中かを返す</summary>
	/// <returns>途中なら true</returns>
	bool IsActive() const override { return active_; }
	/// <summary>技の残りを返す（時間と耐久力の少ない方）</summary>
	/// <returns>1→0</returns>
	float GetActiveRatio() const override;
	/// <summary>今の段階のクールタイムを返す</summary>
	/// <returns>クールタイム [秒]</returns>
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	/// <summary>今の段階の HP の上限の補正を返す</summary>
	/// <returns>HP の上限に掛ける倍率</returns>
	float GetMaxHpScale() const override { return maxHpScale_[LevelIndex()]; }
	/// <summary>タイマーを進め、バリアの間は入った敵弾を消して耐久力を減らし、バリアの球の見た目を更新する</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">使わない</param>
	void Update(float dt, bool canAct) override;
	/// <summary>バリアの間か軽減の光りの間だけ、バリアの球を描く</summary>
	void Draw() override;

	/// <summary>バックパックを背負うかを返す（位置の基準にだけ使う）</summary>
	/// <returns>常に true</returns>
	bool UsesBackpack() const override { return true; }
	/// <summary>バックパック本体を描くかを返す</summary>
	/// <returns>常に false（本体は描かない）</returns>
	bool DrawsBackpack() const override { return false; }
	/// <summary>2 基のユニットのパーツのモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) override;
	/// <summary>ユニットの行列を作る（背中で閉じた形と、開いて回る形の間を離れ具合で補間する）</summary>
	/// <param name="backpackWorld">バックパックのワールド行列</param>
	void UpdateModels(const Matrix4x4& backpackWorld) override;
	/// <summary>ユニットを描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="idPass">true なら ID マスクへ描く</param>
	void DrawModels(DirectXCore* dxCore, bool idPass) override;

	/// <summary>クールタイムが明けていれば被ダメージを軽減する（弾は消さない＝ジャスト回避は普段どおり起きる）</summary>
	/// <param name="damage">難易度の倍率の後のダメージ</param>
	/// <returns>軽減後のダメージ</returns>
	int ModifyIncomingDamage(int damage) override;
	/// <summary>バリアの間は被弾を吸い、耐久力を減らす（弾でない攻撃は当たり続けるので、間隔を空けて 1 回分だけ減らす）</summary>
	/// <param name="damage">被弾のダメージ</param>
	/// <returns>バリアの間なら true</returns>
	bool AbsorbHit(int damage) override;

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
	/// <summary>耐久力を減らす（尽きたらバリアを消す）</summary>
	/// <param name="damage">減らす量</param>
	void Drain(int damage);
	/// <summary>ユニットの開き具合を返す</summary>
	/// <returns>0＝背中で閉じている、1＝開いて回っている</returns>
	float ReleaseRatio() const;
	/// <summary>ユニットの回る位置（自機のまわりの、奥が上がるように傾けた輪）を求める</summary>
	/// <param name="unit">ユニットの番号 0/1</param>
	/// <returns>ワールド座標</returns>
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
