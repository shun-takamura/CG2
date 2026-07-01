#pragma once
#include <cmath>
#include "Enemy/IEnemyCommand.h"
#include "Enemy/EnemyContext.h"
#include "IImGuiEditable.h"
#include "Scene/GameScene.h"
#include "Camera.h"

/// <summary>
/// 画面内停止型（ScreenHover）の敵コマンド。
/// - OnEnter でスプライン追従を切り離す
/// - Approach: カメラ相対の停止点（ctx.hoverOffset）へ hoverApproachSpeed で飛来
/// - Hold: 到達後はカメラ相対にロック（毎フレ world = camPos + camRot*offset）
///   ＝カメラが進んでもプレイヤーから見て静止して見える。並行して shoot_interval_t で射撃
/// - hoverHoldDuration 経過で IsFinished → 次コマンド（RetreatCommand）で逃走
/// </summary>
class HoverStationCommand : public IEnemyCommand {
public:
	void OnEnter(IImGuiEditable*, EnemyContext& ctx) override {
		arrived_         = false;
		holdElapsed_     = 0.0f;
		holdDuration_    = ctx.hoverHoldDuration;
		lastShotIdx_     = -1;
		ctx.requestDetach     = true;
		ctx.useFreeVelocity   = false; // 自力で位置を制御
		ctx.billboardToPlayer = true;
	}

	void Update(float dt, IImGuiEditable* entity, EnemyContext& ctx) override {
		holdDuration_ = ctx.hoverHoldDuration;
		ctx.useFreeVelocity   = false;
		ctx.billboardToPlayer = true;
		if (!entity || !ctx.scene) return;
		Vector3* pos = entity->GetEditableTranslate();
		if (!pos) return;

		// カメラ相対の停止点をワールド座標へ
		Vector3 target = *pos;
		Camera* cam = ctx.scene->GetCamera();
		if (cam) {
			target = CameraLocalToWorld(cam, ctx.hoverOffset);
		}

		if (!arrived_) {
			// 飛来：target へ approachSpeed で近づく
			const float dx = target.x - pos->x;
			const float dy = target.y - pos->y;
			const float dz = target.z - pos->z;
			const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
			if (dist <= 0.25f) {
				arrived_ = true;
				*pos = target;
			} else {
				float step = ctx.hoverApproachSpeed * dt;
				if (step > dist) step = dist;
				pos->x += dx / dist * step;
				pos->y += dy / dist * step;
				pos->z += dz / dist * step;
			}
		} else {
			// 停止：カメラ相対にロック（画面上は静止）
			*pos = target;
			holdElapsed_ += dt;

			// 射撃（ShootAtPlayerCommand と同様に shoot_interval_sec [秒] 周期）
			if (ctx.player && ctx.shootIntervalSec > 0.0f) {
				const float secSinceSpawn = ctx.stageTimeSec - ctx.triggerSec;
				if (secSinceSpawn >= 0.0f) {
					const int shotIdx = static_cast<int>(secSinceSpawn / ctx.shootIntervalSec);
					if (shotIdx > lastShotIdx_) {
						lastShotIdx_ = shotIdx;
						Vector3* ppos = ctx.player->GetEditableTranslate();
						if (ppos) {
							Vector3 d{ ppos->x - pos->x, ppos->y - pos->y, ppos->z - pos->z };
							const float dlen = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
							if (dlen > 0.01f) {
								d = { d.x / dlen, d.y / dlen, d.z / dlen };
								ctx.scene->SpawnEnemyBullet(*pos, d, -1.0f, -1.0f, "EnemyBullet", ctx.player);
							}
						}
					}
				}
			}
		}
	}

	bool IsFinished() const override { return arrived_ && holdElapsed_ >= holdDuration_; }

private:
	bool  arrived_      = false;
	float holdElapsed_  = 0.0f;
	float holdDuration_ = 6.0f;
	int   lastShotIdx_  = -1;

	// カメラローカルオフセット（x=右 / y=上 / z=前方深度）をワールド座標へ。
	// ワールド行列の各行が正規直交基底（右/上/前）。
	static Vector3 CameraLocalToWorld(Camera* cam, const Vector3& off) {
		const Matrix4x4& w = cam->GetWorldMatrix();
		const Vector3 camPos = cam->GetTranslate();
		return {
			camPos.x + w.m[0][0] * off.x + w.m[1][0] * off.y + w.m[2][0] * off.z,
			camPos.y + w.m[0][1] * off.x + w.m[1][1] * off.y + w.m[2][1] * off.z,
			camPos.z + w.m[0][2] * off.x + w.m[1][2] * off.y + w.m[2][2] * off.z,
		};
	}
};
