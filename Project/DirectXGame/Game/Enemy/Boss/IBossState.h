#pragma once
#include <algorithm>
#include "MathUtility.h"
#include <cmath>
#include <memory>
#include <random>
#include "Vector3.h"
#include "Vector4.h"
#include "IImGuiEditable.h"

class GameScene;
class BossRig;
class IBossState;

/// <summary>
/// 状態をまたいで使う、行動を選ぶための共有データ（黒板）。BossStateMachine が持ち、状態クラスが読み書きする。
/// クールタイムは BossStateMachine が毎フレーム減らす。調整値は BossIdleState の選び方で使う。
/// </summary>
struct BossBrain {
	float attackCooldown = 0.0f;     // 攻撃終了後、次に攻撃を選べるまでの残り秒
	float jumpCooldown   = 0.0f;     // ジャンプ飛び越え後、次に選べるまでの残り秒
	bool  rageEntered    = false;    // 発狂の咆哮を出したか（1 回だけ）
	bool  standDown      = false;    // true の間は次の行動を選ばない（自機の死亡演出の間。19 番 G3）
	int   gravityOrbUses = 0;        // 必殺技 GravityOrb の HP のしきい値をいくつ使ったか
	std::mt19937 rng{ std::random_device{}() };

	// ----- 選び方の調整値 -----
	float attackCooldownBase = 1.5f; // 攻撃を選ぶたびにクールタイムをこの値へ
	float attackChance       = 0.5f; // クールタイム明けに攻撃を選ぶ確率（残りは移動系）
	float rageHpRatio        = 0.5f; // 発狂（HP 比率がこれ以下）：攻撃間隔を詰め、頻度・弾数を上げ、予兆を短くする
	float rageAttackCooldown = 0.9f;
	float rageAttackChance   = 0.7f;
	float rageTelegraphScale = 0.8f;
	float meleeRange         = 9.0f; // このXZ距離以内なら近接攻撃を選択肢に入れる
	float jumpCooldownBase   = 4.0f; // ジャンプの連発防止（飛び道具ほど頻発させない）
	float jumpChance         = 0.3f; // 攻撃しないとき、移動系の中でジャンプを選ぶ確率（残りは接近）
	// 必殺技 GravityOrb を使う HP の比率（上から順に、切るたびに 1 回。ランダムな攻撃とは別枠）
	float gravityOrbHpRatios[2] = { 0.7f, 0.35f };
};

/// <summary>
/// ボス行動（移動/攻撃/反撃）に渡すコンテキスト。雑魚敵の EnemyContext とは独立して定義し、
/// ボス側の変更が雑魚敵コードに波及しないようにする。
/// </summary>
struct BossStateContext {
	BossBrain*      brain  = nullptr; // 状態をまたぐ共有データ（BossStateMachine が持つ）
	IImGuiEditable* boss   = nullptr;
	IImGuiEditable* player = nullptr;
	GameScene*      scene  = nullptr;
	bool billboardToPlayer = true; // out: プレイヤー方向を向くか（ビルボード制御）

	// out: ボス本体に重ねる色（rgb）と重み（a、0 で元の色）。予兆の点滅に使う。毎フレーム 0 から始まる
	Vector4 tint{ 0.0f, 0.0f, 0.0f, 0.0f };

	// アリーナ（突進/跳躍の着地点をこの円内へ収める）。radius <= 0 なら制限なし
	Vector3 arenaCenter{ 0.0f, 0.0f, 0.0f };
	float   arenaRadius = 0.0f;

	// 手足（無ければ null）。攻撃は腕の構えを指示し、近接の判定は拳に付ける
	BossRig* rig = nullptr;
};

/// <summary>
/// 予兆の点滅色を ctx.tint へ書く。溜めの終わりに近づくほど濃く・速く点滅させ、「もうすぐ来る」を伝える
/// </summary>
/// <param name="ctx">書き込み先のコンテキスト（tint を上書きする）</param>
/// <param name="color">点滅させる色（rgb）</param>
/// <param name="timer">溜めの経過時間 [秒]</param>
/// <param name="duration">溜めの全体の長さ [秒]</param>
inline void SetBossTelegraphTint(BossStateContext& ctx, const Vector3& color, float timer, float duration) {
	// これより短い溜めは最初から溜め終わりとみなす [秒]
	constexpr float kMinDuration = 1e-4f;
	// 溜めの始めの点滅の速さ [回/秒]
	constexpr float kBlinkStartHz = 5.0f;
	// 溜めの終わりまでに点滅の速さが増える量 [回/秒]
	constexpr float kBlinkAddHz = 9.0f;
	// 溜めの始めの点滅の濃さ
	constexpr float kAlphaStart = 0.35f;
	// 溜めの終わりまでに点滅の濃さが増える量
	constexpr float kAlphaAdd = 0.65f;
	const float t = (duration > kMinDuration) ? (std::min)(timer / duration, 1.0f) : 1.0f;
	const float freqHz = kBlinkStartHz + kBlinkAddHz * t;
	const float blink = 0.5f + 0.5f * std::cos(kTwoPi * freqHz * timer);
	ctx.tint = { color.x, color.y, color.z, (kAlphaStart + kAlphaAdd * t) * blink };
}

/// <summary>着地点などをアリーナの円（margin だけ内側）に収める。Y は触らない。アリーナ半径が 0 以下なら何もしない</summary>
/// <param name="ctx">アリーナの中心と半径を持つコンテキスト</param>
/// <param name="p">収める位置（円の外なら円周上へ書き換える）</param>
/// <param name="margin">円の縁から内側へ空ける幅 [m]</param>
inline void ClampToBossArena(const BossStateContext& ctx, Vector3& p, float margin = 2.0f) {
	// これより中心に近いときは収めない（向きが決まらない）[m]
	constexpr float kNearZeroDistance = 1e-4f;
	if (ctx.arenaRadius <= 0.0f) return;
	const float r = (std::max)(ctx.arenaRadius - margin, 0.0f);
	const float dx = p.x - ctx.arenaCenter.x;
	const float dz = p.z - ctx.arenaCenter.z;
	const float d = std::sqrt(dx * dx + dz * dz);
	if (d <= r || d < kNearZeroDistance) return;
	p.x = ctx.arenaCenter.x + dx / d * r;
	p.z = ctx.arenaCenter.z + dz / d * r;
}

/// <summary>
/// ボスの状態（ステートパターンの状態クラス）。BossStateMachine が今の状態を 1 つだけ持ち、基底クラスのポインタ経由で呼ぶ。
/// 次の状態は状態クラス自身が決めて Update の戻り値で返す（BossStateMachine は切り替えるだけ）。
/// </summary>
class IBossState {
public:
	virtual ~IBossState() = default;

	/// <summary>
	/// この状態に入った直後に 1 回だけ呼ばれる（BossStateMachine::ChangeState が前の状態の OnExit の後に呼ぶ）。
	/// タイマーの初期化・腕の構えの指示などを行う。既定は何もしない
	/// </summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	virtual void OnEnter(BossStateContext& ctx) { (void)ctx; }

	/// <summary>毎フレーム 1 回呼ばれる、この状態の更新</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>
	/// 次の状態。nullptr 以外を返すと BossStateMachine がこの状態の OnExit → 返した状態の OnEnter の順で切り替える。
	/// nullptr ならこの状態のまま続ける
	/// </returns>
	virtual std::unique_ptr<IBossState> Update(float dt, BossStateContext& ctx) = 0;

	/// <summary>
	/// この状態から出る時に 1 回だけ呼ばれる（次の状態の OnEnter より前）。判定の消去・高さの戻しなどの後始末を行う。
	/// 既定は何もしない
	/// </summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	virtual void OnExit(BossStateContext& ctx) { (void)ctx; }

	/// <summary>
	/// 部位が壊れた時に BossStateMachine が Update より先に呼ぶ。移る先の状態を返す。
	/// 既定は攻撃のクールタイムを最低限まで延ばしてひるみ（BossStagger）を返す＝今の行動は打ち切る
	/// </summary>
	/// <param name="staggerSeconds">ひるむ時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>
	/// 次の状態。nullptr 以外ならその状態へ切り替える（この状態の OnExit が走る）。
	/// nullptr を返すとひるまずにこの状態のまま続ける
	/// </returns>
	virtual std::unique_ptr<IBossState> OnPartBroken(float staggerSeconds, BossStateContext& ctx);
};

/// <summary>
/// 1 回きりの行動（攻撃・移動・咆哮・ひるみ）の基底（テンプレートメソッド）。Update は final で、
/// 派生クラスは Tick（中身）・IsFinished（終わったか）・NextState（終わった後の行き先）だけを書く。
/// 次の状態の既定は待機（BossIdleState が次の行動を選ぶ）。行動ごとに変えたい時は NextState を上書きする。
/// </summary>
class BossActionState : public IBossState {
public:
	/// <summary>Tick で行動を 1 フレーム進め、IsFinished が true なら NextState の状態を返す（派生クラスでは上書きできない）</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>行動が終わったら NextState が返した次の状態。行動の途中なら nullptr（この状態のまま）</returns>
	std::unique_ptr<IBossState> Update(float dt, BossStateContext& ctx) final;

protected:
	/// <summary>行動の中身を 1 フレーム進める（移動・弾の発射・判定の更新など）。Update から毎フレーム呼ばれる</summary>
	/// <param name="dt">World の経過時間 [秒]</param>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	virtual void Tick(float dt, BossStateContext& ctx) = 0;

	/// <summary>行動が終わったか。Tick の直後に Update から呼ばれる</summary>
	/// <returns>終わったら true（次の状態へ移る）。続けるなら false</returns>
	virtual bool IsFinished() const = 0;

	/// <summary>行動が終わった後に移る状態を作る。IsFinished が true になったフレームに 1 回だけ呼ばれる。既定は待機（BossIdleState）</summary>
	/// <param name="ctx">ボス・自機・シーンと共有データ</param>
	/// <returns>次の状態（nullptr を返すとこの状態のまま残るので返さない）</returns>
	virtual std::unique_ptr<IBossState> NextState(BossStateContext& ctx);
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

/// <summary>ボスから自機の狙う高さへの照準（発射位置・水平方位・仰角・水平距離）を求める</summary>
/// <param name="ctx">ボスと自機を持つコンテキスト</param>
/// <param name="targetHeight">自機の足元から狙う点までの高さ [m]</param>
/// <returns>照準。ボスか自機が無い・位置が取れない時は valid = false のまま</returns>
inline BossAimInfo ComputeBossAim(const BossStateContext& ctx, float targetHeight = 1.0f) {
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

/// <summary>yaw と pitch から単位方向ベクトルを作る</summary>
/// <param name="yaw">水平方位 [ラジアン]（+Z 基準・右回り）</param>
/// <param name="pitch">仰角 [ラジアン]（上向き正）</param>
/// <returns>単位方向ベクトル</returns>
inline Vector3 BossDirFromYawPitch(float yaw, float pitch) {
	const float cp = std::cos(pitch);
	return { cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw) };
}

/// <summary>
/// ボスの攻撃判定などが使う基準点（アンカー）のワールド位置を求める。現在はボス root 位置＋「ボス→プレイヤー方向」を
/// forward とした基底でのローカルオフセット（右/上/前）。
/// アニメーション導入後は、この関数の中身だけをボーン行列基準（WeaponParams と同じ流儀）に
/// 差し替えれば、呼び出す攻撃クラス（BossAttackXxx）側は一切変更不要になる。
/// </summary>
/// <param name="ctx">ボスと自機を持つコンテキスト（自機が無ければ forward は +Z）</param>
/// <param name="localOffset">右/上/前のローカルオフセット [m]</param>
/// <returns>アンカーのワールド位置</returns>
inline Vector3 ResolveBossHitAnchor(const BossStateContext& ctx, const Vector3& localOffset) {
	// これより短い水平距離では向きを決めない（+Z のまま）[m]
	constexpr float kNearZeroLength = 1e-4f;
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
			if (len > kNearZeroLength) fwd = { d.x / len, 0.0f, d.z / len };
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
