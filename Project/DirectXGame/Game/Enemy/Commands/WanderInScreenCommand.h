#pragma once
#include <cmath>
#include "RandomGenerator.h"
#include "Enemy/IEnemyCommand.h"
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
	WanderInScreenCommand(IImGuiEditable* carrier, float radius, float lifetime, float speed)
		: carrier_(carrier), radius_(radius), lifetime_(lifetime), moveSpeed_(speed) {}

	void OnEnter(IImGuiEditable*, EnemyContext& ctx) override {
		elapsed_ = 0.0f;
		retargetCooldown_ = 0.0f;
		targetOffset_ = PickRandomOffset();
		ctx.requestDetach     = true;
		ctx.useFreeVelocity   = false; // 自力で位置を制御
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		elapsed_ += dt;
		retargetCooldown_ -= dt;
		if (retargetCooldown_ <= 0.0f) {
			targetOffset_ = PickRandomOffset();
			retargetCooldown_ = 1.5f + Frand() * 1.5f;
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
		if (dist > 0.01f) {
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
			if (flen > 1e-3f) {
				const float nx = fx / flen;
				const float ny = fy / flen;
				const float nz = fz / flen;
				const float dotCarrier = flen;
				const float dotDrone =
					(pos->x - camPos.x) * nx +
					(pos->y - camPos.y) * ny +
					(pos->z - camPos.z) * nz;
				// ドローン半径分の余裕も引いて手前側に押し戻す
				const float safety = entity->GetCollider().radius;
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

		// 射撃（徘徊と並行）: shoot_interval_t [カメラt 単位] で発射
		if (ctx.player && ctx.shootIntervalT > 0.0f) {
			const float tSinceSpawn = ctx.railT - ctx.triggerT;
			if (tSinceSpawn >= 0.0f) {
				const int shotIdx = static_cast<int>(tSinceSpawn / ctx.shootIntervalT);
				if (shotIdx > lastShotIdx_) {
					lastShotIdx_ = shotIdx;
					Vector3* ppos = ctx.player->GetEditableTranslate();
					if (ppos) {
						Vector3 d{ ppos->x - pos->x, ppos->y - pos->y, ppos->z - pos->z };
						const float dlen = std::sqrt(d.x*d.x + d.y*d.y + d.z*d.z);
						if (dlen > 0.01f) {
							d = { d.x / dlen, d.y / dlen, d.z / dlen };
							ctx.scene->SpawnEnemyBullet(*pos, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
						}
					}
				}
			}
		}
	}

	bool IsFinished() const override { return elapsed_ >= lifetime_; }

private:
	IImGuiEditable* carrier_ = nullptr;
	float radius_     = 8.0f;
	float lifetime_   = 10.0f;
	float moveSpeed_  = 5.0f;
	float elapsed_    = 0.0f;
	float retargetCooldown_ = 0.0f;
	int   lastShotIdx_ = -1;
	Vector3 targetOffset_{ 0.0f, 0.0f, 0.0f };

	// 中央乱数から [0,1) を引く（リプレイ決定性のため std::rand は使わない）。
	float Frand() { return RandomGenerator::Instance().NextFloat01(); }

	Vector3 PickRandomOffset() {
		// XZ 円盤 + Y 縦は半分の幅でランダム
		const float angle = Frand() * 6.2831853f;
		const float r = std::sqrt(Frand()) * radius_;
		return {
			std::cos(angle) * r,
			(Frand() - 0.5f) * radius_, // Y は ±radius/2
			std::sin(angle) * r,
		};
	}
};
