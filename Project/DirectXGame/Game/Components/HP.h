#pragma once

/// <summary>
/// HPコンポーネント。プレイヤー・敵・ボス等の被ダメージ対象に持たせる。
/// enabled = false のときはダメージを受けない（HPシステム非対象として扱う）。
/// </summary>
struct HP {
	bool enabled = false;
	int  maxHP = 100;
	int  currentHP = 100;

	void TakeDamage(int damage) {
		if (!enabled) return;
		if (damage <= 0) return;
		currentHP -= damage;
		if (currentHP < 0) currentHP = 0;
	}

	void Heal(int amount) {
		if (!enabled) return;
		if (amount <= 0) return;
		currentHP += amount;
		if (currentHP > maxHP) currentHP = maxHP;
	}

	bool IsDead() const { return enabled && currentHP <= 0; }
};
