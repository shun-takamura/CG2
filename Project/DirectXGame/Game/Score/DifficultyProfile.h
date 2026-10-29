#pragma once

/// <summary>
/// 純度（難易度）→ 敵の強さの倍率を 1 か所にまとめたもの（19_GameOverAndDifficulty.md §5、コンテスト⑤）。
/// 純度は EtherState::GetDifficulty() をそのまま読む（賭ける画面の値／賭けずに始めたら既定 5.0／死んだ後は下がった値）。
/// 各項目は I・V・IX の 3 点の倍率を持ち、その間は直線でつなぐ。V で今までの強さ（1.0）。値は JSON＋ImGui。
/// 読む所：敵とボスの体力（GameScene のスポーン）・敵の攻撃のダメージ（StagePlayScene の被弾）・
/// 射撃の間隔（RailStagePart のコントローラ作成）・ボスの攻撃のクールタイム（BossStateMachine）。
/// </summary>
class DifficultyProfile {
public:
	static DifficultyProfile* GetInstance();

	/// <summary>I・V・IX のときの倍率</summary>
	struct Curve {
		float atMin = 1.0f;  // I（1.0）
		float atMid = 1.0f;  // V（5.0）
		float atMax = 1.0f;  // IX（9.0）
	};

	float GetEnemyHpScale() const { return Evaluate(enemyHp_); }
	float GetBossHpScale() const { return Evaluate(bossHp_); }
	float GetDamageScale() const { return Evaluate(damage_); }
	float GetShootIntervalScale() const { return Evaluate(shootInterval_); }
	float GetBossCooldownScale() const { return Evaluate(bossCooldown_); }
	/// <summary>報酬の倍率。中身は G7（コンテストの後）。今はいつも 1.0</summary>
	float GetRewardScale() const { return 1.0f; }

	/// <summary>体力・ダメージなど整数の値に倍率を掛ける（四捨五入、1 未満にはしない）</summary>
	static int ScaleInt(int base, float scale);

	bool LoadTuning();
	void SaveTuning() const;
	void OnImGui();

private:
	DifficultyProfile();
	~DifficultyProfile() = default;
	DifficultyProfile(const DifficultyProfile&) = delete;
	DifficultyProfile& operator=(const DifficultyProfile&) = delete;

	/// <summary>今の純度で倍率を出す</summary>
	float Evaluate(const Curve& c) const;
	float EvaluateAt(const Curve& c, float purity) const;

	// 調整値（Resources/Json/Tuning/DifficultyProfile.json）
	float midPurity_ = 5.0f;                       // V の位置
	Curve enemyHp_{ 0.6f, 1.0f, 1.8f };
	Curve bossHp_{ 0.7f, 1.0f, 1.5f };
	Curve damage_{ 0.5f, 1.0f, 2.0f };
	Curve shootInterval_{ 1.6f, 1.0f, 0.55f };     // 小さいほど速く撃つ
	Curve bossCooldown_{ 1.6f, 1.0f, 0.5f };
};
