#pragma once
#include <cmath>
#include "MathUtility.h"
#include "Components/Gameplay.h"
#include "RandomGenerator.h"
#include "Enemy/Commands/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"
#include "Camera.h"

/// <summary>
/// 子ドローン（運び屋から生成された雑魚）用の徘徊コマンド。
/// - 運び屋 (carrier) の現在位置を中心に半径 radius 内をランダム徘徊
/// - 一定時間 (lifetime) 経過で IsFinished → 次のコマンド（通常 Retreat）へ遷移
/// - カメラから見て運び屋より奥には回り込まないようクランプ
/// - 兄弟ドローン同士は重ならないようコライダー半径で排斥
/// </summary>
class WanderInScreenCommand : public IEnemyCommand {
public:
	/// <summary>子ドローンの徘徊コマンドを作る</summary>
	/// <param name="carrier">徘徊の中心にする運び屋</param>
	/// <param name="radius">運び屋から離れてよい半径 [m]</param>
	/// <param name="lifetime">徘徊する時間 [秒]（過ぎたら終わる）</param>
	/// <param name="speed">移動の速さ [m/秒]</param>
	WanderInScreenCommand(IImGuiEditable* carrier, float radius, float lifetime, float speed)
		: carrier_(carrier), radius_(radius), lifetime_(lifetime), moveSpeed_(speed) {}

	/// <summary>スプライン追従を切り離し、最初の徘徊先を抽選する</summary>
	/// <param name="entity">動かす敵（このコマンドでは使わない）</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void OnEnter([[maybe_unused]] IImGuiEditable* entity, EnemyContext& ctx) override {
		elapsed_ = 0.0f;
		retargetCooldown_ = 0.0f;
		targetOffset_ = PickRandomOffset();
		ctx.requestDetach     = true;
		ctx.useFreeVelocity   = false; // 自力で位置を制御
		ctx.billboardToPlayer = true;
	}

	/// <summary>
	/// 運び屋のまわりの徘徊先へ進み（半径・運び屋より奥へ行かないようクランプ、兄弟とは押し合う）、
	/// 射撃間隔ごとに自機を撃つ。徘徊先はときどき抽選し直す
	/// </summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="entity">子ドローン</param>
	/// <param name="ctx">敵の入力と結果の書き込み先</param>
	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		elapsed_ += dt;
		retargetCooldown_ -= dt;
		if (retargetCooldown_ <= 0.0f) {
			targetOffset_ = PickRandomOffset();
			retargetCooldown_ = kRetargetMinSec + Frand() * kRetargetRandomSec;
		}
		ctx.useFreeVelocity = false;
		ctx.billboardToPlayer = true;

		// 運び屋が破棄されている場合は carrier_ を切る（解放済みポインタの dangling 参照を防ぐ）。
		// 以後は徘徊先を失うだけで、lifetime 経過で自然に次のコマンドへ遷移する。
		if (carrier_ && ctx.scene && !ctx.scene->IsDynamicEntityAlive(carrier_)) {
			carrier_ = nullptr;
		}

		if (!entity || !carrier_) return;
		Vector3* pos    = entity->GetEditableTranslate();
		Vector3* cpos   = carrier_->GetEditableTranslate();
		if (!pos || !cpos) return;

		// 目標位置 = 運び屋の現在位置 + 抽選オフセット
		const Vector3 target{
			cpos->x + targetOffset_.x,
			cpos->y + targetOffset_.y,
			cpos->z + targetOffset_.z,
		};
		// 目標へ滑らかに移動
		const float dx = target.x - pos->x;
		const float dy = target.y - pos->y;
		const float dz = target.z - pos->z;
		const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
		if (dist > kNearZeroLength) {
			float step = moveSpeed_ * dt;
			if (step > dist) step = dist;
			pos->x += dx / dist * step;
			pos->y += dy / dist * step;
			pos->z += dz / dist * step;
		}

		// 運び屋からの距離が radius を超えたらクランプ
		{
			const float ex = pos->x - cpos->x;
			const float ey = pos->y - cpos->y;
			const float ez = pos->z - cpos->z;
			const float r  = std::sqrt(ex * ex + ey * ey + ez * ez);
			if (r > radius_) {
				const float k = radius_ / r;
				pos->x = cpos->x + ex * k;
				pos->y = cpos->y + ey * k;
				pos->z = cpos->z + ez * k;
			}
		}

		// カメラから見て運び屋より奥へは行かせない（forward 方向にクランプ）
		if (ctx.scene && ctx.scene->GetCamera()) {
			const Vector3 camPos = ctx.scene->GetCamera()->GetTranslate();
			const float fx = cpos->x - camPos.x;
			const float fy = cpos->y - camPos.y;
			const float fz = cpos->z - camPos.z;
			const float flen = std::sqrt(fx * fx + fy * fy + fz * fz);
			if (flen > kMinCameraDistance) {
				const float nx = fx / flen;
				const float ny = fy / flen;
				const float nz = fz / flen;
				const float dotCarrier = flen;
				const float dotDrone =
					(pos->x - camPos.x) * nx +
					(pos->y - camPos.y) * ny +
					(pos->z - camPos.z) * nz;
				// ドローン半径分の余裕も引いて手前側に押し戻す
				const float safety = Gameplay::Of(entity).GetCollider().radius;
				const float maxDot = dotCarrier - safety;
				if (dotDrone > maxDot) {
					const float excess = dotDrone - maxDot;
					pos->x -= nx * excess;
					pos->y -= ny * excess;
					pos->z -= nz * excess;
				}
			}
		}

		// 兄弟ドローンとの相互排斥（コライダー半径ベース）
		if (ctx.scene) {
			ctx.scene->ApplyEnemyRepulsion(entity);
		}

		// 射撃（徘徊と並行）: shoot_interval_sec [秒] で発射
		// 画面外から撃たれると理不尽なので、画面内にいる時だけ発射する
		if (ctx.player && ctx.shootIntervalSec > 0.0f && ctx.CanAttackFrom(*pos)) {
			const float secSinceSpawn = ctx.stageTimeSec - ctx.triggerSec;
			if (secSinceSpawn >= 0.0f) {
				const int shotIdx = static_cast<int>(secSinceSpawn / ctx.shootIntervalSec);
				if (shotIdx > lastShotIdx_) {
					lastShotIdx_ = shotIdx;
					Vector3* ppos = ctx.player->GetEditableTranslate();
					if (ppos) {
						Vector3 d{ ppos->x - pos->x, ppos->y - pos->y, ppos->z - pos->z };
						const float dlen = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
						if (dlen > kNearZeroLength) {
							d = { d.x / dlen, d.y / dlen, d.z / dlen };
							ctx.scene->SpawnEnemyBullet(*pos, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
						}
					}
				}
			}
		}
	}

	/// <summary>徘徊が終わったか</summary>
	/// <returns>lifetime が過ぎたら true</returns>
	bool IsFinished() const override { return elapsed_ >= lifetime_; }

private:
	// 徘徊先を引き直すまでの最短の時間 [秒]
	static constexpr float kRetargetMinSec = 1.5f;
	// 徘徊先を引き直すまでの時間のばらつきの幅 [秒]
	static constexpr float kRetargetRandomSec = 1.5f;
	// これより短い距離・向きは 0 とみなす [m]
	static constexpr float kNearZeroLength = 0.01f;
	// これより短いカメラからの距離では奥行きの制限をしない [m]
	static constexpr float kMinCameraDistance = 1e-3f;
	IImGuiEditable* carrier_ = nullptr;
	float radius_     = 8.0f;
	float lifetime_   = 10.0f;
	float moveSpeed_  = 5.0f;
	float elapsed_    = 0.0f;
	float retargetCooldown_ = 0.0f;
	int   lastShotIdx_ = -1;
	Vector3 targetOffset_{ 0.0f, 0.0f, 0.0f };

	/// <summary>中央乱数から [0,1) を引く（リプレイ決定性のため std::rand は使わない）</summary>
	/// <returns>0 以上 1 未満の乱数</returns>
	float Frand() { return RandomGenerator::GetInstance()->NextFloat01(); }

	/// <summary>運び屋からの徘徊先のオフセットを抽選する（XZ は半径 radius の円盤、Y は ±radius/2）</summary>
	/// <returns>運び屋からのオフセット [m]</returns>
	Vector3 PickRandomOffset() {
		// XZ 円盤 + Y 縦は半分の幅でランダム
		const float angle = Frand() * kTwoPi;
		const float r = std::sqrt(Frand()) * radius_;
		return {
			std::cos(angle) * r,
			(Frand() - 0.5f) * radius_, // Y は ±radius/2
			std::sin(angle) * r,
		};
	}
};
