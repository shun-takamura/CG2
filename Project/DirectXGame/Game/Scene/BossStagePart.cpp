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
	bossSpawned_    = true;

	// カメラ初期姿勢：プレイヤーからボス方向を向いて開始（フリーモード）。
	camMode_   = BossCamMode::Free;
	snapTimer_ = -1.0f;
	float y, p;
	if (ComputeBossYawPitch(y, p)) { camYaw_ = y; camPitch_ = p; }
	else { camYaw_ = 0.0f; camPitch_ = 0.15f; }
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

void BossStagePart::ComputeGroundBasis(Vector3& outForward, Vector3& outRight) const {
	// カメラ前方を XZ 平面へ射影して移動基底を作る（forward=奥, right=右）。
	Vector3 fwd{ 0.0f, 0.0f, 1.0f };
	if (camera_) fwd = camera_->GetForward();
	fwd.y = 0.0f;
	float flen = std::sqrt(fwd.x * fwd.x + fwd.z * fwd.z);
	if (flen < 1e-4f) { fwd = { 0.0f, 0.0f, 1.0f }; flen = 1.0f; }
	fwd.x /= flen; fwd.z /= flen;
	outForward = fwd;
	// right = cross(up, forward), up=(0,1,0)
	outRight = { fwd.z, 0.0f, -fwd.x };
}

void BossStagePart::ApplyDashImpulse(const Vector2& moveDelta) {
	// 移動入力方向へ地上速度の初速を上乗せ（入力なしはその場）。以降は UpdatePlayerGroundMovement の
	// 指数減衰で自然に walk 速度へ落ちる＝STG のダッシュ→減速と同じ手触りになる。
	const float mlen = std::sqrt(moveDelta.x * moveDelta.x + moveDelta.y * moveDelta.y);
	if (mlen < 1e-3f) return;
	Vector3 fwd, right;
	ComputeGroundBasis(fwd, right);
	const float nx = moveDelta.x / mlen;
	const float ny = moveDelta.y / mlen;
	groundVelocity_.x += (right.x * nx + fwd.x * ny) * dodgeDashSpeed_;
	groundVelocity_.y += (right.z * nx + fwd.z * ny) * dodgeDashSpeed_;
}

void BossStagePart::UpdatePlayerGroundMovement(IImGuiEditable* player, float dt, const Vector2& moveDelta) {
	if (!player || !camera_) return;
	Vector3* tp = player->GetEditableTranslate();
	if (!tp) return;

	Vector3 fwd, right;
	ComputeGroundBasis(fwd, right);

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

bool BossStagePart::ComputeBossYawPitch(float& outYaw, float& outPitch) const {
	if (!boss_ || !host_) return false;
	Vector3 pPos{ 0.0f, 0.0f, 0.0f };
	if (IImGuiEditable* pl = host_->GetPlayer()) {
		if (const Vector3* pp = pl->GetEditableTranslate()) pPos = *pp;
	}
	Vector3 bPos{ 0.0f, 0.0f, 0.0f };
	if (const Vector3* bp = boss_->GetEditableTranslate()) bPos = *bp; else return false;
	// 注視基準（プレイヤー体の中心）からボス中心への方向。
	const Vector3 focus{ pPos.x, pPos.y + camFocusHeight_, pPos.z };
	Vector3 d{ bPos.x - focus.x, bPos.y - focus.y, bPos.z - focus.z };
	const float len = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
	if (len < 1e-4f) return false;
	d.x /= len; d.y /= len; d.z /= len;
	outYaw   = std::atan2(d.x, d.z);
	outPitch = -std::asin(std::clamp(d.y, -1.0f, 1.0f));
	return true;
}

void BossStagePart::RequestTargetSnap() {
	float y, p;
	if (!ComputeBossYawPitch(y, p)) return; // ボスが無ければ無視
	snapFromYaw_   = camYaw_;
	snapFromPitch_ = camPitch_;
	// yaw は最短経路になるよう from を基準に補正
	float dy = y - snapFromYaw_;
	while (dy >  3.14159265f) dy -= 6.28318531f;
	while (dy < -3.14159265f) dy += 6.28318531f;
	snapToYaw_   = snapFromYaw_ + dy;
	snapToPitch_ = p;
	snapTimer_   = 0.0f;
}

void BossStagePart::ToggleCamMode() {
	camMode_ = (camMode_ == BossCamMode::Free) ? BossCamMode::LockOn : BossCamMode::Free;
}

void BossStagePart::UpdateCamera(float dt, float stickX, float stickY, float mouseDx, float mouseDy) {
	if (!camera_ || !host_) return;

	// 生入力→回転量（感度・反転を内部で適用）。スティックは dt スケール、マウスは相対カウント。
	// pitch は俯角（+で下向き）。マウス上移動(GetDeltaY<0)で camPitch を下げる＝上を向く（+mouseDy）。
	const float lookYawDelta   = stickX * lookSensStick_ * dt + mouseDx * lookSensMouse_;
	const float lookPitchDelta = -stickY * lookSensStick_ * dt + mouseDy * lookSensMouse_;

	// 1) 手動ナッジ（フリー/ロックオン共通。ロックオン中もカメラ操作を効かせる）。
	camYaw_  += lookYawDelta;
	camPitch_ = std::clamp(camPitch_ + lookPitchDelta, pitchMin_, pitchMax_);

	// 2) ロックオン：ボス方向へ弱いバネで緩く追従（低ゲイン＝ラグ/低精度）。手動ナッジが上に乗る。
	if (camMode_ == BossCamMode::LockOn) {
		float y, p;
		if (ComputeBossYawPitch(y, p)) {
			float dy = y - camYaw_;
			while (dy >  3.14159265f) dy -= 6.28318531f;
			while (dy < -3.14159265f) dy += 6.28318531f;
			const float k = std::clamp(lockSpringGain_ * dt, 0.0f, 1.0f);
			camYaw_   += dy * k;
			camPitch_  = std::clamp(camPitch_ + (p - camPitch_) * k, pitchMin_, pitchMax_);
		}
	}

	// 3) 一瞬スナップ（ターゲットカメラ）。進行中は yaw/pitch を上書き。
	if (snapTimer_ >= 0.0f) {
		snapTimer_ += dt;
		float u = (snapDuration_ > 1e-4f) ? (snapTimer_ / snapDuration_) : 1.0f;
		if (u >= 1.0f) { u = 1.0f; }
		const float s = u * u * (3.0f - 2.0f * u); // smoothstep
		camYaw_   = snapFromYaw_   + (snapToYaw_   - snapFromYaw_)   * s;
		camPitch_ = std::clamp(snapFromPitch_ + (snapToPitch_ - snapFromPitch_) * s, pitchMin_, pitchMax_);
		if (u >= 1.0f) snapTimer_ = -1.0f; // 完了：以降はフリー/ロックの現在値として継続
	}

	// 4) カメラ姿勢：yaw/pitch から forward を作り、プレイヤー focus の背後 camDistance_ に eye を置く。
	const float cp = std::cos(camPitch_);
	const Vector3 forward{ std::sin(camYaw_) * cp, -std::sin(camPitch_), std::cos(camYaw_) * cp };

	Vector3 pPos{ 0.0f, 0.0f, 0.0f };
	if (IImGuiEditable* pl = host_->GetPlayer()) {
		if (const Vector3* pp = pl->GetEditableTranslate()) pPos = *pp;
	}
	const Vector3 focus{ pPos.x, pPos.y + camFocusHeight_, pPos.z };
	const Vector3 eye{
		focus.x - forward.x * camDistance_,
		focus.y - forward.y * camDistance_,
		focus.z - forward.z * camDistance_ };

	camera_->SetTranslate(eye);
	camera_->SetRotate({ camPitch_, camYaw_, 0.0f });
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
		ImGui::DragFloat("Dodge Dash Speed", &dodgeDashSpeed_, 0.5f, 0.0f, 120.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

		ImGui::SeparatorText("Camera");
		ImGui::Text("Mode: %s", (camMode_ == BossCamMode::LockOn) ? "LockOn" : "Free");
		ImGui::TextDisabled("L1/中ボタン 短押し=ターゲット / 長押し=Free⇔LockOn");
		ImGui::DragFloat("Look Sens (Stick)", &lookSensStick_, 0.05f, 0.0f, 10.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Look Sens (Mouse)", &lookSensMouse_, 0.0002f, 0.0f, 0.05f, "%.4f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat2("Pitch Clamp (min/max)", &pitchMin_, 0.01f, -1.5f, 1.5f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Cam Distance", &camDistance_, 0.2f, 1.0f, 80.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Focus Height", &camFocusHeight_, 0.1f, 0.0f, 20.0f, "%.1f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Lock Spring Gain", &lockSpringGain_, 0.05f, 0.1f, 20.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
		ImGui::DragFloat("Snap Duration (s)", &snapDuration_, 0.01f, 0.0f, 2.0f, "%.2f");
		if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	}
#else
	(void)changed;
#endif
}
