#include "BossStagePart.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "Camera.h"
#include "IImGuiEditable.h"
#include "Components/Gameplay.h"
#include "Enemy/EnemyController.h"
#include "Enemy/IEnemyCommand.h"
#include "Enemy/Commands/BossAttackCommand.h"

#ifdef _DEBUG
#include "imgui.h"
#endif

BossStagePart::BossStagePart()  = default;
BossStagePart::~BossStagePart() = default;

void BossStagePart::Initialize(IBossStageHost* host, Camera* camera) {
	host_   = host;
	camera_ = camera;
}

void BossStagePart::Enter() {
	if (bossSpawned_ || !host_) return;

	// デバッグ地面（視覚のみ）
	ground_ = host_->SpawnPrefabAt(groundPrefab_, arenaCenter_);

	// ボス本体：アリーナ中心の +Z 側に接地。
	const Vector3 bossPos{ arenaCenter_.x, groundY_ + bossHeight_, arenaCenter_.z + bossForward_ };
	boss_ = host_->SpawnPrefabAt(bossPrefab_, bossPos);
	if (boss_) {
		// ボスAI：BossAttackCommand を1つ積んだ EnemyController をプログラム的に生成（Wave 非経由）。
		auto ctrl = std::make_unique<EnemyController>();
		ctrl->entity_            = boss_;
		ctrl->billboardToPlayer_ = true;
		std::vector<std::unique_ptr<IEnemyCommand>> cmds;
		cmds.push_back(std::make_unique<BossAttackCommand>());
		ctrl->Init(std::move(cmds));
		host_->RegisterEnemyController(std::move(ctrl));
	}

	// プレイヤーをボス手前の地上へ配置（レール終端の camera-local 位置から地上へリセット）。
	if (IImGuiEditable* pl = host_->GetPlayer()) {
		if (Vector3* pp = pl->GetEditableTranslate()) {
			*pp = { arenaCenter_.x, groundY_, arenaCenter_.z - playerStartBack_ };
		}
	}

	groundVelocity_ = { 0.0f, 0.0f };
	camLookCur_     = arenaCenter_;
	bossSpawned_    = true;
}

void BossStagePart::Update(float worldDt) {
	if (!bossSpawned_ || !host_) return;

	// 撃破検出：HP が尽きたら参照を切る（この後の SweepDeadEntities が実体を破棄＋death エフェクト）。
	if (boss_ && Gameplay::Of(boss_).GetHP().IsDead()) {
		boss_ = nullptr;
	}

	// ボスAI（敵コントローラ）駆動。静止ボスなので stageTimeSec は未使用（0 を渡す）。
	host_->UpdateEnemyControllers(worldDt, host_->GetPlayer(), 0.0f);
	host_->SweepDeadEntities();
}

void BossStagePart::UpdatePlayerGroundMovement(IImGuiEditable* player, float dt, const Vector2& moveDelta) {
	if (!player || !camera_) return;
	Vector3* tp = player->GetEditableTranslate();
	if (!tp) return;

	// カメラ前方を XZ 平面へ射影して移動基底を作る（forward=奥, right=右）。
	Vector3 fwd = camera_->GetForward();
	fwd.y = 0.0f;
	float flen = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
	if (flen < 1e-4f) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.z /= flen;
	// right = cross(up, forward), up=(0,1,0)
	const Vector3 right{ fwd.z, 0.0f, -fwd.x };

	// 目標速度（ワールドXZ）＝入力を基底で合成 × 最大速度。指数減衰で慣性。
	const float tvx = (right.x * moveDelta.x + fwd.x * moveDelta.y) * playerMoveSpeed_;
	const float tvz = (right.z * moveDelta.x + fwd.z * moveDelta.y) * playerMoveSpeed_;
	const float alpha = (playerSmoothTime_ > 1e-4f) ? (1.0f - std::exp(-dt / playerSmoothTime_)) : 1.0f;
	groundVelocity_.x += (tvx - groundVelocity_.x) * alpha;
	groundVelocity_.y += (tvz - groundVelocity_.y) * alpha;

	Vector3 pos = *tp;
	pos.x += groundVelocity_.x * dt;
	pos.z += groundVelocity_.y * dt;
	pos.y  = groundY_;

	// アリーナ円内にクランプ（端で外向き慣性をゼロ化）。
	const float dx = pos.x - arenaCenter_.x;
	const float dz = pos.z - arenaCenter_.z;
	const float d2 = dx * dx + dz * dz;
	if (d2 > arenaRadius_ * arenaRadius_ && d2 > 1e-6f) {
		const float d = std::sqrt(d2);
		const float s = arenaRadius_ / d;
		pos.x = arenaCenter_.x + dx * s;
		pos.z = arenaCenter_.z + dz * s;
		groundVelocity_ = { 0.0f, 0.0f };
	}

	*tp = pos;
}

void BossStagePart::UpdateCamera() {
	if (!camera_ || !host_) return;

	Vector3 pPos{ 0.0f, 0.0f, 0.0f };
	if (IImGuiEditable* pl = host_->GetPlayer()) {
		if (const Vector3* pp = pl->GetEditableTranslate()) pPos = *pp;
	}
	Vector3 bPos = arenaCenter_;
	if (boss_) {
		if (const Vector3* bp = boss_->GetEditableTranslate()) bPos = *bp;
	}

	// 注視点＝プレイヤーとボスの中点（平滑追従）。
	const Vector3 lookTarget{
		(pPos.x + bPos.x) * 0.5f, (pPos.y + bPos.y) * 0.5f, (pPos.z + bPos.z) * 0.5f };
	camLookCur_.x += (lookTarget.x - camLookCur_.x) * camLookLerp_;
	camLookCur_.y += (lookTarget.y - camLookCur_.y) * camLookLerp_;
	camLookCur_.z += (lookTarget.z - camLookCur_.z) * camLookLerp_;

	// プレイヤー→ボス方向（XZ）。カメラはその逆方向へ引く＝プレイヤー背後からボスを見る。
	Vector3 dirPB{ bPos.x - pPos.x, 0.0f, bPos.z - pPos.z };
	float plen = std::sqrt(dirPB.x * dirPB.x + dirPB.z * dirPB.z);
	if (plen < 1e-4f) { dirPB = { 0.0f, 0.0f, 1.0f }; plen = 1.0f; }
	dirPB.x /= plen; dirPB.z /= plen;

	const Vector3 eye{
		pPos.x - dirPB.x * camDistance_,
		pPos.y + camHeight_,
		pPos.z - dirPB.z * camDistance_ };

	Vector3 dir{ camLookCur_.x - eye.x, camLookCur_.y - eye.y, camLookCur_.z - eye.z };
	const float dlen = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (dlen < 1e-4f) return;
	dir.x /= dlen; dir.y /= dlen; dir.z /= dlen;
	const float yaw   = std::atan2(dir.x, dir.z);
	const float pitch = -std::asin(std::clamp(dir.y, -1.0f, 1.0f));

	camera_->SetTranslate(eye);
	camera_->SetRotate({ pitch, yaw, 0.0f });
	camera_->Update();
}

void BossStagePart::Reset() {
	if (host_) host_->ClearBossRuntimeState();
	boss_        = nullptr;
	ground_      = nullptr;
	bossSpawned_ = false;
	groundVelocity_ = { 0.0f, 0.0f };
}

void BossStagePart::OnImGuiTuning(bool& changed) {
#ifdef _DEBUG
	if (ImGui::CollapsingHeader("Boss Battle")) {
		ImGui::Text("Boss: %s", boss_ ? "alive" : (bossSpawned_ ? "dead" : "not spawned"));
		ImGui::DragFloat("Arena Radius", &arenaRadius_, 0.5f, 1.0f, 200.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Player Move Speed", &playerMoveSpeed_, 0.2f, 0.0f, 60.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Player Smooth (s)", &playerSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::SeparatorText("Lock-on Camera");
		ImGui::DragFloat("Cam Distance", &camDistance_, 0.2f, 1.0f, 80.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam Height", &camHeight_, 0.2f, 0.0f, 60.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam Look Lerp", &camLookLerp_, 0.01f, 0.01f, 1.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}
#else
	(void)changed;
#endif
}
