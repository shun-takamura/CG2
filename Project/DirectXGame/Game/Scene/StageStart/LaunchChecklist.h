#pragma once
#include <array>

class InputManager;
class JsonValue;

/// <summary>
/// スタート演出の機体確認（16_StageStart.md §3.1、F4）の判定。操作説明を兼ねるので、実際の操作で項目が CLEAR になる。
///   照準（AIM）     … マウス / 右スティックを動かした量の合計が aimDistancePx に届く
///   スラスター      … W / A / S / D（左スティックの 4 方向）を 1 回ずつ。どこから噴かすかとエフェクトは F5
///   武装（WEAPON）  … 射撃（Action::Fire）を weaponShots 回
/// 給電ワイヤー（POWER CABLE）は操作しない行で、表示だけ（LaunchControlBoard）。
/// 移動と照準はゲーム本体（StagePlayScene・Reticle）と同じく入力アクションを通さずに読む（Action はボタン入力だけのため）。
/// デモモードでは決まった間隔で 1 つずつ自動で CLEAR にする。調整値は StagePlay.json の "stageStart" → "checklist"。
/// </summary>
class LaunchChecklist {
public:
	enum class Item { Aim, Thruster, Weapon, Count };
	static constexpr int kItemCount = static_cast<int>(Item::Count);
	// スラスターの 4 方向（表示の順：W A S D）
	enum ThrusterDir { kThrusterUp, kThrusterLeft, kThrusterDown, kThrusterRight, kThrusterDirCount };

	LaunchChecklist() { Reset(); }
	void Reset();
	/// <summary>入力を読んで項目を進める。autoClear ならデモモードの自動の進め方</summary>
	void Update(InputManager* input, float deltaTime, bool autoClear);

	bool IsCleared(Item item) const { return clearedTime_[static_cast<int>(item)] >= 0.0f; }
	/// <summary>CLEAR になってからの秒（なっていなければ負）。表示が切り替わる瞬間のチラつきに使う</summary>
	float TimeSinceCleared(Item item) const;
	int ClearedCount() const;
	bool IsAllClear() const { return ClearedCount() == kItemCount; }

	/// <summary>照準を動かした量 0..1</summary>
	float GetAimProgress() const;
	bool IsThrusterDone(int dir) const { return thrusterDone_[dir]; }
	/// <summary>今押しているか（ランプを明るくする）</summary>
	bool IsThrusterHeld(int dir) const { return thrusterHeld_[dir]; }
	int GetWeaponCount() const { return weaponCount_; }
	int GetWeaponShots() const { return weaponShots_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	void Clear(Item item);
	void UpdateAuto(float deltaTime);

	float time_ = 0.0f;
	std::array<float, kItemCount> clearedTime_{};   // CLEAR になった時刻（負＝まだ）
	float aimDistance_ = 0.0f;
	std::array<bool, kThrusterDirCount> thrusterDone_{};
	std::array<bool, kThrusterDirCount> thrusterHeld_{};
	int weaponCount_ = 0;

	// 調整値
	float aimDistancePx_ = 400.0f;      // 照準：マウスを動かした量の合計 [px]（2026-10-19 ユーザー指示で 600 → 400）
	float aimStickSpeed_ = 900.0f;      // 照準：右スティックを倒し切ったときの量 [px/s]
	float stickThreshold_ = 0.5f;       // スラスター：左スティックをこれ以上倒したらその方向
	int weaponShots_ = 1;               // 武装：射撃の回数（2026-10-19 ユーザー指示で 3 → 1）
	float autoStartDelay_ = 1.2f;       // デモモード：機体確認が始まってから最初の項目まで [s]
	float autoInterval_ = 0.9f;         // デモモード：項目の間隔 [s]（スラスターの 4 方向・射撃もこの間で順に）
};
