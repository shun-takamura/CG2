#pragma once
#include "FlightUnitPart.h"
#include <array>

/// <summary>
/// 機動系（20_Loadout.md §3.3。ジャスト回避を軸にする）。
/// 常時：移動が速くなる・ジャスト回避のときに必殺技ゲージを足す。
/// RB：数秒だけ World をスローにする（自分は Player グループなので普通の速さ）。カウンターは無い。
/// ジャスト回避・必殺技が起きたら、スローはそこで終わる（時間の倍率はそちらに任せる）。
/// モデル：背中の左右に 3 枚重ねの可変ウィング（バックパックの位置が基準・本体は描かない）。スローの間は、
/// 板の上の端を支点に扇のように開き、終わると閉じる。束の位置は左右別々、束の中の板は右が左の鏡写し。
/// </summary>
class FlightUnitMobility : public FlightUnitSkill {
public:
	/// <summary>板の閉じた形・開いた形と、左右の束の付ける所の初期値を決める</summary>
	FlightUnitMobility();
	~FlightUnitMobility() override;

	/// <summary>World をスローにし始める（ほかの演出が World を握っている間は出さない）</summary>
	/// <returns>出せたら true</returns>
	bool Activate() override;
	/// <summary>スローを終える</summary>
	/// <param name="restoreTime">true なら World の倍率を 1 に戻す（false はジャスト回避・必殺技が握っているとき）</param>
	void Cancel(bool restoreTime) override;
	/// <summary>スローの途中かを返す</summary>
	/// <returns>途中なら true</returns>
	bool IsActive() const override { return active_; }
	/// <summary>時間の倍率を使うかを返す</summary>
	/// <returns>常に true</returns>
	bool UsesTimeScale() const override { return true; }
	/// <summary>技の残りを返す</summary>
	/// <returns>1→0</returns>
	float GetActiveRatio() const override;
	/// <summary>今の段階のクールタイムを返す</summary>
	/// <returns>クールタイム [秒]</returns>
	float GetCooldown() const override { return cooldown_[LevelIndex()]; }
	/// <summary>ウィングを開閉し、スローの間は入り・抜けを補間しながら World の倍率を書き、時間が来たら終える</summary>
	/// <param name="dt">実時間の経過 [秒]</param>
	/// <param name="canAct">使わない</param>
	void Update(float dt, bool canAct) override;

	/// <summary>バックパックを背負うかを返す（位置の基準にだけ使う）</summary>
	/// <returns>常に true</returns>
	bool UsesBackpack() const override { return true; }
	/// <summary>バックパック本体を描くかを返す</summary>
	/// <returns>常に false（本体は描かない）</returns>
	bool DrawsBackpack() const override { return false; }
	/// <summary>左右の束の板のモデルを読み込む</summary>
	/// <param name="object3DManager">3D オブジェクトの管理</param>
	/// <param name="dxCore">DirectX の中核</param>
	void InitializeModels(Object3DManager* object3DManager, DirectXCore* dxCore) override;
	/// <summary>板の行列を作る（閉じた形と扇に開いた形の間を開き具合で補間する。右の束は左の鏡写し）</summary>
	/// <param name="backpackWorld">バックパックのワールド行列</param>
	void UpdateModels(const Matrix4x4& backpackWorld) override;
	/// <summary>板を描く</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="idPass">true なら ID マスクへ描く</param>
	void DrawModels(DirectXCore* dxCore, bool idPass) override;

	/// <summary>今の段階の移動速度の補正を返す</summary>
	/// <returns>移動速度に掛ける倍率</returns>
	float GetMoveSpeedScale() const override { return moveSpeedScale_[LevelIndex()]; }
	/// <summary>今の段階のジャスト回避で足す必殺技ゲージを返す</summary>
	/// <returns>足す量</returns>
	float GetJustDodgeGaugeBonus() const override { return justDodgeGaugeBonus_[LevelIndex()]; }
	/// <summary>今の段階のジャスト回避の受付の延長を返す</summary>
	/// <returns>延ばす秒 [秒]</returns>
	float GetJustDodgeWindowBonus() const override { return justDodgeWindowBonus_[LevelIndex()]; }

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
	// 段階 1..3 の値
	float moveSpeedScale_[3]       = { 1.05f, 1.10f, 1.15f };
	float justDodgeGaugeBonus_[3]  = { 4.0f, 6.0f, 8.0f };
	float justDodgeWindowBonus_[3] = { 0.01f, 0.025f, 0.05f }; // ジャスト回避の受付を延ばす秒
	float slowDuration_[3]        = { 3.0f, 3.5f, 4.0f };
	float cooldown_[3]            = { 25.0f, 22.0f, 20.0f };
	float slowWorld_ = 0.35f;     // スロー中の World の倍率
	float easeIn_ = 0.12f;        // 入りと抜けの時間（いきなり変えない）
	float easeOut_ = 0.25f;
	Vector4 color_{ 0.45f, 0.95f, 0.85f, 1.0f };

	bool active_ = false;
	float timer_ = 0.0f;          // 経過

	// ----- 可変ウィング -----
	// 板：0=Back（奥＝体の側。動かない）, 1=Middle, 2=Front（手前＝カメラの側。いちばん大きく回る）
	static constexpr int kPlateCount = 3;
	static constexpr int kGroupCount = 2;   // 0=自機の左, 1=右
	struct Group {
		std::array<std::unique_ptr<Object3DInstance>, kPlateCount> plates;
		bool placed = false;
	};
	std::array<Group, kGroupCount> groups_;
	Transform groupMounts_[kGroupCount];       // 束を付ける所（バックパックのローカル。+Z が後ろ、自機の左が +X。左右は別々）
	Transform closedPlates_[kPlateCount];      // 閉じたときの板（束のローカル。原点＝板の上の端＝支点。右の束は鏡写し）
	Transform openPlates_[kPlateCount];        // 開いたときの板
	float openTime_ = 0.3f;
	float closeTime_ = 0.4f;
	float openness_ = 0.0f;       // 0＝閉じている、1＝開いている
	bool preview_ = false;        // ImGui：止めたまま開き具合を見る
	float previewRatio_ = 1.0f;
};
