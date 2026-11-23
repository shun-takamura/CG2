#pragma once

class EtherState;

/// <summary>
/// 純度（難易度）→ 敵の強さの倍率を 1 か所にまとめたもの（19_GameOverAndDifficulty.md §5、コンテスト⑤）。
/// 純度は GameSession の EtherState::GetDifficulty() をそのまま読む（賭ける画面の値／賭けずに始めたら既定 5.0／死んだ後は下がった値）。
/// 各項目は I・V・IX の 3 点の倍率を持ち、その間は直線でつなぐ。V で今までの強さ（1.0）。値は JSON＋ImGui。
/// 読む所：敵とボスの体力（GameScene のスポーン）・敵の攻撃のダメージ（StagePlayScene の被弾）・
/// 射撃の間隔（RailStagePart のコントローラ作成）・ボスの攻撃のクールタイム（BossStateMachine）。
/// </summary>
class DifficultyProfile {
public:
	/// <summary>調整値（JSON）を読み込む</summary>
	/// <param name="etherState">純度を読む先（GameSession が持つ物）</param>
	explicit DifficultyProfile(const EtherState* etherState);
	~DifficultyProfile() = default;


	/// <summary>I・V・IX のときの倍率</summary>
	struct Curve {
		float atMin = 1.0f;  // I（1.0）
		float atMid = 1.0f;  // V（5.0）
		float atMax = 1.0f;  // IX（9.0）
	};

	/// <summary>今の純度での敵の体力の倍率を取得する</summary>
	/// <returns>倍率</returns>
	float GetEnemyHpScale() const { return Evaluate(enemyHp_); }
	/// <summary>今の純度でのボスの体力の倍率を取得する</summary>
	/// <returns>倍率</returns>
	float GetBossHpScale() const { return Evaluate(bossHp_); }
	/// <summary>今の純度での敵の攻撃のダメージの倍率を取得する</summary>
	/// <returns>倍率</returns>
	float GetDamageScale() const { return Evaluate(damage_); }
	/// <summary>今の純度での敵の射撃間隔の倍率を取得する（小さいほど速く撃つ）</summary>
	/// <returns>倍率</returns>
	float GetShootIntervalScale() const { return Evaluate(shootInterval_); }
	/// <summary>今の純度でのボスの攻撃のクールタイムの倍率を取得する（小さいほど速い）</summary>
	/// <returns>倍率</returns>
	float GetBossCooldownScale() const { return Evaluate(bossCooldown_); }
	/// <summary>報酬の倍率。中身は G7（コンテストの後）。今はいつも 1.0</summary>
	/// <returns>常に 1.0</returns>
	float GetRewardScale() const { return 1.0f; }

	/// <summary>体力・ダメージなど整数の値に倍率を掛ける（四捨五入、1 未満にはしない）</summary>
	/// <param name="base">元の値（0 以下ならそのまま返す）</param>
	/// <param name="scale">倍率</param>
	/// <returns>倍率を掛けて四捨五入した値（1 以上）</returns>
	static int ScaleInt(int base, float scale);

	/// <summary>Resources/Json/Tuning/DifficultyProfile.json から調整値を読む</summary>
	/// <returns>読めたら true。ファイルが無い・パースに失敗したら false</returns>
	bool LoadTuning();
	/// <summary>今の調整値を Resources/Json/Tuning/DifficultyProfile.json へ書く</summary>
	void SaveTuning() const;
	/// <summary>調整値と今の倍率を ImGui に出す</summary>
	void OnImGui();

private:
	DifficultyProfile(const DifficultyProfile&) = delete;
	DifficultyProfile& operator=(const DifficultyProfile&) = delete;

	// 純度を読む先（GameSession が持つ EtherState。DifficultyProfile より長く生きる）
	const EtherState* etherState_ = nullptr;

	/// <summary>今の純度で倍率を出す</summary>
	/// <param name="c">I・V・IX の倍率</param>
	/// <returns>倍率（EtherState の純度で EvaluateAt）</returns>
	float Evaluate(const Curve& c) const;
	/// <summary>指定した純度で倍率を出す（I〜V、V〜IX をそれぞれ直線でつなぐ）</summary>
	/// <param name="c">I・V・IX の倍率</param>
	/// <param name="purity">純度（I〜IX に収める）</param>
	/// <returns>倍率</returns>
	float EvaluateAt(const Curve& c, float purity) const;

	// 調整値（Resources/Json/Tuning/DifficultyProfile.json）
	float midPurity_ = 5.0f;                       // V の位置
	Curve enemyHp_{ 0.6f, 1.0f, 1.8f };
	Curve bossHp_{ 0.7f, 1.0f, 1.5f };
	Curve damage_{ 0.5f, 1.0f, 2.0f };
	Curve shootInterval_{ 1.6f, 1.0f, 0.55f };     // 小さいほど速く撃つ
	Curve bossCooldown_{ 1.6f, 1.0f, 0.5f };
};
