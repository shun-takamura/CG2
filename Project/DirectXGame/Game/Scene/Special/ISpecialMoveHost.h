#pragma once
#include "TimeGroup.h"
#include "Vector2.h"
#include "Vector3.h"
#include <vector>

class IImGuiEditable;

/// <summary>
/// 必殺技（ShatterlinePart / StormLockPart）がシーンへ頼むことだけを並べた窓口
/// （IRailStageHost / IFlightUnitHost と同じ流儀。公開 API は増やさない）。
/// GetPlayer / SetTimeScale / IsEnemyAlive / AbsorbEnemyBulletsInSphere は IFlightUnitHost と同じ形なので、
/// StagePlayScene の 1 つの override で両方を満たす。
/// </summary>
class ISpecialMoveHost {
public:
	virtual ~ISpecialMoveHost() = default;
	/// <summary>自機を取得する</summary>
	/// <returns>自機。いなければ nullptr</returns>
	virtual IImGuiEditable* GetPlayer() const = 0;
	/// <summary>時間グループの進む速さの倍率を設定する</summary>
	/// <param name="g">対象の時間グループ</param>
	/// <param name="scale">倍率（0 で停止、1 で通常）</param>
	virtual void SetTimeScale(TimeGroup g, float scale) = 0;
	/// <summary>必殺技の無敵を切り替える（Shatterline の後隙だけ false にして被弾できるようにする）</summary>
	/// <param name="invincible">true で無敵</param>
	virtual void SetSpecialInvincible(bool invincible) = 0;
	/// <summary>必殺技の無敵が有効かを返す</summary>
	/// <returns>無敵中なら true</returns>
	virtual bool IsSpecialInvincible() const = 0;
	/// <summary>自機の体の中心を返す</summary>
	/// <returns>体の中心のワールド座標</returns>
	virtual Vector3 GetSpecialPlayerCenter() const = 0;
	/// <summary>体の中心から dir へ撃つときの銃口（体を囲む楕円の表面）を返す</summary>
	/// <param name="playerCenter">自機の体の中心（ワールド）</param>
	/// <param name="dir">撃つ向き（ワールド）</param>
	/// <returns>銃口のワールド座標</returns>
	virtual Vector3 GetSpecialMuzzle(const Vector3& playerCenter, const Vector3& dir) const = 0;
	/// <summary>自機の体を囲む楕円の半軸を返す</summary>
	/// <param name="horizontal">画面の横方向の半軸の受け取り先</param>
	/// <param name="vertical">画面の縦方向の半軸の受け取り先</param>
	virtual void GetSpecialBodyEllipse(float& horizontal, float& vertical) const = 0;
	/// <summary>レティクルの画面上の位置を返す</summary>
	/// <returns>レティクルの位置（UI 座標）</returns>
	virtual Vector2 GetReticleScreenPos() const = 0;
	/// <summary>生きている敵弾の一覧を集める</summary>
	/// <param name="out">敵弾を追加する先</param>
	virtual void CollectEnemyBullets(std::vector<IImGuiEditable*>& out) = 0;
	/// <summary>生きている敵本体（Enemy・Boss）の一覧を集める</summary>
	/// <param name="out">敵本体を追加する先</param>
	virtual void CollectEnemies(std::vector<IImGuiEditable*>& out) = 0;
	/// <summary>敵がまだ生きているかを返す</summary>
	/// <param name="e">調べる敵</param>
	/// <returns>生きていれば true</returns>
	virtual bool IsEnemyAlive(IImGuiEditable* e) const = 0;
	/// <summary>敵弾を消す</summary>
	/// <param name="bullet">消す敵弾</param>
	virtual void DestroyEnemyBullet(IImGuiEditable* bullet) = 0;
	/// <summary>球の中の敵弾を消し、そのダメージの合計を返す</summary>
	/// <param name="center">球の中心（ワールド）</param>
	/// <param name="radius">球の半径 [m]</param>
	/// <returns>消した敵弾のダメージの合計（難易度の倍率の前）</returns>
	virtual int AbsorbEnemyBulletsInSphere(const Vector3& center, float radius) = 0;
	/// <summary>HP を持たない敵をスコア・撃破数付きで消す（HP を持つ敵は HP を 0 にすれば通常の撃破の流れに乗る）</summary>
	/// <param name="e">消す敵</param>
	virtual void DestroyEnemyWithScore(IImGuiEditable* e) = 0;
};
