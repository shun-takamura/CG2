#pragma once

/// <summary>
/// HPコンポーネント。プレイヤー・敵・ボス等の被ダメージ対象に持たせる。
/// enabled = false のときはダメージを受けない（HPシステム非対象として扱う）。
/// </summary>
struct HP {
	bool enabled = false;
	int  maxHP = 100;
	int  currentHP = 100;

	/// <summary>ダメージを受けて HP を減らす（0 で止める）。無効・0 以下のダメージなら何もしない</summary>
	/// <param name="damage">受けるダメージ</param>
	void TakeDamage(int damage) {
		if (!enabled) return;
		if (damage <= 0) return;
		currentHP -= damage;
		if (currentHP < 0) currentHP = 0;
	}

	/// <summary>HP を回復する（最大 HP で止める）。無効・0 以下の量なら何もしない</summary>
	/// <param name="amount">回復する量</param>
	void Heal(int amount) {
		if (!enabled) return;
		if (amount <= 0) return;
		currentHP += amount;
		if (currentHP > maxHP) currentHP = maxHP;
	}

	/// <summary>倒れたか</summary>
	/// <returns>有効で HP が 0 以下なら true</returns>
	bool IsDead() const { return enabled && currentHP <= 0; }
};
