#pragma once
#include <algorithm>
#include <cmath>
#include "Vector3.h"
#include "IImGuiEditable.h"

class GameScene;

/// <summary>
/// ボス行動（移動/攻撃/反撃）に渡すコンテキスト。雑魚敵の EnemyContext とは独立して定義し、
/// ボス側の変更が雑魚敵コードに波及しないようにする。
/// </summary>
struct BossActionContext {
	IImGuiEditable* boss   = nullptr;
	IImGuiEditable* player = nullptr;
	GameScene*      scene  = nullptr;
	bool billboardToPlayer = true; // out: プレイヤー方向を向くか（ビルボード制御）
};

/// <summary>
/// ボス行動の共通インターフェース。BossActionManager が1つだけ保持して駆動する。
/// </summary>
struct IBossAction {
	virtual void OnEnter(BossActionContext& ctx) {}
	virtual void Update(float dt, BossActionContext& ctx) = 0;
	virtual void OnExit(BossActionContext& ctx) {}
	virtual bool IsFinished() const = 0;
	virtual ~IBossAction() = default;
};

/// <summary>
/// 弾幕系攻撃の共通照準。ボス→プレイヤーの水平方位(yaw)と、プレイヤーの胸の高さへ届く仰角(pitch)。
/// ボス中心は地面より高いので、水平に撃つと地上のプレイヤーの頭上を抜ける。pitch を全弾に掛けて揃える。
/// </summary>
struct BossAimInfo {
	Vector3 origin{ 0.0f, 0.0f, 0.0f };
	float yaw = 0.0f;
	float pitch = 0.0f;
	float distXZ = 0.0f;
	bool valid = false;
};

inline BossAimInfo ComputeBossAim(const BossActionContext& ctx, float targetHeight = 1.0f) {
	BossAimInfo info;
	if (!ctx.boss || !ctx.player) return info;
	const Vector3* bp = ctx.boss->GetEditableTranslate();
	const Vector3* pp = ctx.player->GetEditableTranslate();
	if (!bp || !pp) return info;
	const float dx = pp->x - bp->x;
	const float dz = pp->z - bp->z;
	info.origin = *bp;
	info.distXZ = std::sqrt(dx * dx + dz * dz);
	info.yaw = std::atan2(dx, dz);
	info.pitch = std::atan2((pp->y + targetHeight) - bp->y, (std::max)(info.distXZ, 1.0f));
	info.valid = true;
	return info;
}

// yaw（+Z 基準・右回り）と pitch（上向き正）から単位方向ベクトル
inline Vector3 BossDirFromYawPitch(float yaw, float pitch) {
	const float cp = std::cos(pitch);
	return { cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw) };
}

/// <summary>
/// ボスの攻撃判定などが使う基準点（アンカー）。現在はボス root 位置＋「ボス→プレイヤー方向」を
/// forward とした基底でのローカルオフセット（右/上/前）。
/// アニメーション導入後は、この関数の中身だけをボーン行列基準（WeaponParams と同じ流儀）に
/// 差し替えれば、呼び出す攻撃クラス（BossAttackXxx）側は一切変更不要になる。
/// </summary>
inline Vector3 ResolveBossHitAnchor(const BossActionContext& ctx, const Vector3& localOffset) {
	Vector3 origin{ 0.0f, 0.0f, 0.0f };
	if (ctx.boss) {
		if (const Vector3* bp = ctx.boss->GetEditableTranslate()) origin = *bp;
	}

	Vector3 fwd{ 0.0f, 0.0f, 1.0f };
	if (ctx.boss && ctx.player) {
		const Vector3* bp = ctx.boss->GetEditableTranslate();
		const Vector3* pp = ctx.player->GetEditableTranslate();
		if (bp && pp) {
			Vector3 d{ pp->x - bp->x, 0.0f, pp->z - bp->z };
			const float len = std::sqrt(d.x * d.x + d.z * d.z);
			if (len > 1e-4f) fwd = { d.x / len, 0.0f, d.z / len };
		}
	}
	const Vector3 up{ 0.0f, 1.0f, 0.0f };
	const Vector3 right{ up.y * fwd.z - up.z * fwd.y, up.z * fwd.x - up.x * fwd.z, up.x * fwd.y - up.y * fwd.x };

	return {
		origin.x + right.x * localOffset.x + up.x * localOffset.y + fwd.x * localOffset.z,
		origin.y + right.y * localOffset.x + up.y * localOffset.y + fwd.y * localOffset.z,
		origin.z + right.z * localOffset.x + up.z * localOffset.y + fwd.z * localOffset.z,
	};
}
