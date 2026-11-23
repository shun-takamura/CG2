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

	/// <summary>全部の項目を未達に戻した状態で作る</summary>
	LaunchChecklist() { Reset(); }
	/// <summary>全部の項目を未達に戻す</summary>
	void Reset();
	/// <summary>入力を読んで項目を進める</summary>
	/// <param name="input">入力</param>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="autoClear">true ならデモモードの自動の進め方</param>
	void Update(InputManager* input, float deltaTime, bool autoClear);

	/// <summary>項目が CLEAR になったかを返す</summary>
	/// <param name="item">項目</param>
	/// <returns>なっていれば true</returns>
	bool IsCleared(Item item) const { return clearedTime_[static_cast<int>(item)] >= 0.0f; }
	/// <summary>CLEAR になってからの秒を返す（表示が切り替わる瞬間のチラつきに使う）</summary>
	/// <param name="item">項目</param>
	/// <returns>経過 [秒]（なっていなければ負）</returns>
	float TimeSinceCleared(Item item) const;
	/// <summary>CLEAR になった項目の数を返す</summary>
	/// <returns>数</returns>
	int ClearedCount() const;
	/// <summary>全部の項目が CLEAR になったかを返す</summary>
	/// <returns>全部なら true</returns>
	bool IsAllClear() const { return ClearedCount() == kItemCount; }

	/// <summary>照準を動かした量を返す</summary>
	/// <returns>0〜1</returns>
	float GetAimProgress() const;
	/// <summary>スラスターの方向を 1 回噴かしたかを返す</summary>
	/// <param name="dir">方向（表示の順：W A S D）</param>
	/// <returns>済んでいれば true</returns>
	bool IsThrusterDone(int dir) const { return thrusterDone_[dir]; }
	/// <summary>今押しているかを返す（ランプを明るくする）</summary>
	/// <param name="dir">方向（表示の順：W A S D）</param>
	/// <returns>押していれば true</returns>
	bool IsThrusterHeld(int dir) const { return thrusterHeld_[dir]; }
	/// <summary>撃った回数を返す</summary>
	/// <returns>回数</returns>
	int GetWeaponCount() const { return weaponCount_; }
	/// <summary>CLEAR に要る射撃の回数を返す</summary>
	/// <returns>回数</returns>
	int GetWeaponShots() const { return weaponShots_; }

	/// <summary>調整値（StagePlay.json の "stageStart" → "checklist"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>項目を CLEAR にする（済んでいれば何もしない）</summary>
	/// <param name="item">項目</param>
	void Clear(Item item);
	/// <summary>デモモード：決まった間隔で項目を 1 つずつ CLEAR にし、その間の進み具合も見せる</summary>
	/// <param name="deltaTime">経過 [秒]（使わない。時刻は Update で進む）</param>
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
