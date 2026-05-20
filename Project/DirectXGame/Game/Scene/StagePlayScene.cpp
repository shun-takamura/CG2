#include "StagePlayScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SpriteManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "Skybox.h"
#include "DirectXCore.h"
#include "PostEffect.h"
#include "MaskedGrayscaleEffect.h"
#include "Primitive/LineRenderer.h"
#include "Spline/SplineCurveActor.h"
#include "Spline/RailCameraController.h"
#include <cmath>
#include "Components/EntityTag.h"
#include "AnimatedObject3DInstance.h"
#include "AnimatedModelInstance.h"
#include "Object3DInstance.h"
#include "Primitive/PrimitiveInstance.h"
#include "SpriteInstance.h"
#include "LightManager.h"
#include "LogBuffer.h"
#include "Components/SphereCollider.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "UI/Reticle.h"
#include "Enemy/EnemyController.h"
#include "Enemy/EnemyCommandFactory.h"
#include "Enemy/EnemyContext.h"

#ifdef _DEBUG
#include "ViewportWindow.h"
#include "MouseInput.h"
#else
#include "MouseInput.h"
#endif
#include "MathUtility.h"
#include "WindowsApplication.h"
#include <algorithm>
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#include "ImGuiManager.h"
#include "IImGuiEditable.h"
#endif

namespace {
	constexpr const char* kStagePlayTuningPath = "Resources/Json/Tuning/StagePlay.json";
}

#ifdef _DEBUG
#include "KeyboardInput.h"
#include "ImGuiManager.h"
#include "Effect/EffectEditorWindow.h"
#endif

StagePlayScene::StagePlayScene() = default;
StagePlayScene::~StagePlayScene() = default;

void StagePlayScene::LoadTuningFromJson() {
	if (!std::filesystem::exists(kStagePlayTuningPath)) return;
	auto result = JsonParser::ParseFile(kStagePlayTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;

	const JsonValue& off = root["player"]["localOffset"];
	if (off.IsArray() && off.Size() >= 3) {
		playerLocalOffset_ = {
			static_cast<float>(off[0].AsDouble(playerLocalOffset_.x)),
			static_cast<float>(off[1].AsDouble(playerLocalOffset_.y)),
			static_cast<float>(off[2].AsDouble(playerLocalOffset_.z)),
		};
	}
	railCameraSpeed_ = static_cast<float>(
		root["camera"]["speed"].AsDouble(railCameraSpeed_));
	playerSmoothTime_ = static_cast<float>(
		root["player"]["smoothTime"].AsDouble(playerSmoothTime_));

	const JsonValue& spd = root["player"]["moveSpeed"];
	if (spd.IsArray() && spd.Size() >= 2) {
		playerMoveSpeed_.x = static_cast<float>(spd[0].AsDouble(playerMoveSpeed_.x));
		playerMoveSpeed_.y = static_cast<float>(spd[1].AsDouble(playerMoveSpeed_.y));
	}
	const JsonValue& mar = root["player"]["clipMargin"];
	if (mar.IsArray() && mar.Size() >= 2) {
		playerClipMargin_.x = static_cast<float>(mar[0].AsDouble(playerClipMargin_.x));
		playerClipMargin_.y = static_cast<float>(mar[1].AsDouble(playerClipMargin_.y));
	}

	// ----- shooting -----
	const JsonValue& sh = root["shooting"];
	if (sh.IsObject()) {
		bulletSpeed_           = static_cast<float>(sh["bulletSpeed"].AsDouble(bulletSpeed_));
		bulletLifetime_        = static_cast<float>(sh["bulletLifetime"].AsDouble(bulletLifetime_));
		fireRate_              = static_cast<float>(sh["fireRate"].AsDouble(fireRate_));
		bulletColliderGrowth_  = static_cast<float>(sh["colliderGrowthPerMeter"].AsDouble(bulletColliderGrowth_));
		bulletHomingStrength_  = static_cast<float>(sh["homingStrength"].AsDouble(bulletHomingStrength_));
		bulletHomingLockOnBoost_ = static_cast<float>(sh["homingLockOnBoost"].AsDouble(bulletHomingLockOnBoost_));
	}

	// ----- aim -----
	const JsonValue& aim = root["aim"];
	if (aim.IsObject()) {
		aimPlaneDistance_     = static_cast<float>(aim["planeDistance"].AsDouble(aimPlaneDistance_));
		aimSmoothTime_        = static_cast<float>(aim["smoothTime"].AsDouble(aimSmoothTime_));
		aimAssistPixelScale_  = static_cast<float>(aim["assistPixelScale"].AsDouble(aimAssistPixelScale_));
	}
}

void StagePlayScene::SaveTuningToJson() const {
	JsonValue root = JsonValue::MakeObject();

	JsonValue offArr = JsonValue::MakeArray();
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.x)));
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.y)));
	offArr.Push(JsonValue(static_cast<double>(playerLocalOffset_.z)));
	JsonValue spdArr = JsonValue::MakeArray();
	spdArr.Push(JsonValue(static_cast<double>(playerMoveSpeed_.x)));
	spdArr.Push(JsonValue(static_cast<double>(playerMoveSpeed_.y)));
	JsonValue marArr = JsonValue::MakeArray();
	marArr.Push(JsonValue(static_cast<double>(playerClipMargin_.x)));
	marArr.Push(JsonValue(static_cast<double>(playerClipMargin_.y)));
	JsonValue playerObj = JsonValue::MakeObject();
	playerObj["localOffset"] = std::move(offArr);
	playerObj["moveSpeed"]   = std::move(spdArr);
	playerObj["clipMargin"]  = std::move(marArr);
	playerObj["smoothTime"]  = static_cast<double>(playerSmoothTime_);
	root["player"] = std::move(playerObj);

	JsonValue camObj = JsonValue::MakeObject();
	camObj["speed"] = static_cast<double>(railCameraSpeed_);
	root["camera"] = std::move(camObj);

	JsonValue shObj = JsonValue::MakeObject();
	shObj["bulletSpeed"]            = static_cast<double>(bulletSpeed_);
	shObj["bulletLifetime"]         = static_cast<double>(bulletLifetime_);
	shObj["fireRate"]               = static_cast<double>(fireRate_);
	shObj["colliderGrowthPerMeter"] = static_cast<double>(bulletColliderGrowth_);
	shObj["homingStrength"]         = static_cast<double>(bulletHomingStrength_);
	shObj["homingLockOnBoost"]      = static_cast<double>(bulletHomingLockOnBoost_);
	root["shooting"] = std::move(shObj);

	JsonValue aimObj = JsonValue::MakeObject();
	aimObj["planeDistance"]    = static_cast<double>(aimPlaneDistance_);
	aimObj["smoothTime"]       = static_cast<double>(aimSmoothTime_);
	aimObj["assistPixelScale"] = static_cast<double>(aimAssistPixelScale_);
	root["aim"] = std::move(aimObj);

	std::filesystem::path p(kStagePlayTuningPath);
	if (p.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(p.parent_path(), ec);
	}
	JsonWriter::WriteFile(kStagePlayTuningPath, root, { true, 2 });
}

void StagePlayScene::PlayJustDodgeEffect(IImGuiEditable* targetEnemy, float duration)
{
	justDodgeActive_ = true;
	justDodgeTimer_ = 0.0f;
	justDodgeDuration_ = duration;
	justDodgeTarget_ = targetEnemy;

	// プレイヤー + 対象敵をハイライト登録
	ClearHighlights();
	if (player_) AddHighlight(player_);
	if (targetEnemy) AddHighlight(targetEnemy);

	// PostEffect の MaskedGrayscale を有効化（intensity は Update で制御）
	if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
		pe->maskedGrayscale->SetEnabled(true);
		pe->maskedGrayscale->SetIntensity(0.0f);
		pe->maskedGrayscale->UpdateConstantBuffer();
	}
}

void StagePlayScene::UpdateJustDodgeEffect(float dt)
{
	if (!justDodgeActive_) return;

	justDodgeTimer_ += dt;

	// 0→fadeIn→duration-fadeOut→duration で 0/1/1/0 にスムーズ補間
	float intensity = 1.0f;
	const float t = justDodgeTimer_;
	const float D = justDodgeDuration_;
	const float fi = justDodgeFadeIn_;
	const float fo = justDodgeFadeOut_;

	if (t < fi && fi > 0.0f) {
		intensity = t / fi;
	} else if (t > D - fo && fo > 0.0f) {
		intensity = (std::max)(0.0f, (D - t) / fo);
	}

	if (t >= D) {
		// 終了
		justDodgeActive_ = false;
		justDodgeTimer_ = 0.0f;
		justDodgeTarget_ = nullptr;
		ClearHighlights();
		if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
			pe->maskedGrayscale->SetEnabled(false);
			pe->maskedGrayscale->SetIntensity(1.0f);
			pe->maskedGrayscale->UpdateConstantBuffer();
		}
		return;
	}

	// intensity 反映
	if (auto* pe = Game::GetPostEffect(); pe && pe->maskedGrayscale) {
		pe->maskedGrayscale->SetIntensity(intensity);
		pe->maskedGrayscale->UpdateConstantBuffer();
	}
}

void StagePlayScene::OnImGuiTuning() {
#ifdef _DEBUG
	bool changed = false;
	ImGui::TextDisabled("Auto-saved to: %s", kStagePlayTuningPath);
	ImGui::Separator();

	ImGui::TextUnformatted("Player");
	ImGui::DragFloat3("Local Offset", &playerLocalOffset_.x, 0.05f);
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat2("Move Speed (X/Y)", &playerMoveSpeed_.x, 0.05f, 0.0f, 50.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat2("Clip Margin (X/Y)", &playerClipMargin_.x, 0.01f, -1.0f, 2.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Smooth Time (s)", &playerSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::Text("InputOffset: (%.2f, %.2f)  Vel: (%.2f, %.2f)",
		playerInputOffset_.x, playerInputOffset_.y,
		playerVelocity_.x, playerVelocity_.y);

	ImGui::Separator();
	ImGui::TextUnformatted("Rail Camera");
	ImGui::DragFloat("Speed (t/sec)", &railCameraSpeed_, 0.005f, 0.0f, 5.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) {
		changed = true;
		if (railCamera_) railCamera_->SetSpeed(railCameraSpeed_);
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Shooting");
	ImGui::DragFloat("Bullet Speed", &bulletSpeed_, 1.0f, 1.0f, 500.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Bullet Lifetime (s)", &bulletLifetime_, 0.05f, 0.1f, 10.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Fire Rate (s/shot)", &fireRate_, 0.005f, 0.02f, 1.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Collider Growth /m", &bulletColliderGrowth_, 0.005f, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("弾が 1m 進むごとに collider 半径を + この値。\n0 で固定半径、0.02 だと 50m 進むと半径 +1.0。");
	}

	ImGui::DragFloat("Homing Strength /s",     &bulletHomingStrength_,    0.05f, 0.0f, 20.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Homing Strength (Lock)", &bulletHomingLockOnBoost_, 0.05f, 0.0f, 20.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("弾の進行方向を target に向ける指数収束 [/sec]。\n0 でホーミング無効、4.0 で約 0.25 秒で大半が向く。\nLock 側はロックオン中の弾の倍率（必殺技でも同じ仕組みを使う）。");
	}

	ImGui::Separator();
	ImGui::TextUnformatted("Aim / Lock-on");
	ImGui::DragFloat("Aim Plane Distance (m)", &aimPlaneDistance_, 1.0f, 5.0f, 500.0f, "%.1f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::SameLine();
	ImGui::TextDisabled("(?)");
	if (ImGui::IsItemHovered()) {
		ImGui::SetTooltip("カメラからの『狙いの面』距離。\n"
		                  "弾速・寿命とは独立。\n"
		                  "非ロックオン時、レティクルが指すこの面上の点へ弾が向かう。");
	}
	ImGui::DragFloat("Smooth Time (s)", &aimSmoothTime_, 0.005f, 0.0f, 1.0f, "%.3f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;
	ImGui::DragFloat("Assist Pixel Scale", &aimAssistPixelScale_, 0.05f, 0.5f, 5.0f, "%.2f");
	if (ImGui::IsItemDeactivatedAfterEdit()) changed = true;

	if (changed) SaveTuningToJson();

	ImGui::Separator();
	ImGui::TextUnformatted("Just Dodge Effect (debug)");
	ImGui::DragFloat("Duration (s)", &justDodgeDuration_, 0.05f, 0.1f, 10.0f, "%.2f");
	ImGui::DragFloat("Fade In (s)", &justDodgeFadeIn_, 0.01f, 0.0f, 2.0f, "%.2f");
	ImGui::DragFloat("Fade Out (s)", &justDodgeFadeOut_, 0.01f, 0.0f, 2.0f, "%.2f");

	IImGuiEditable* sel = ImGuiManager::Instance().GetSelected();
	ImGui::Text("Target (Inspector 選択): %s",
		sel ? sel->GetName().c_str() : "(none)");
	if (ImGui::Button("Play Just Dodge")) {
		PlayJustDodgeEffect(sel, justDodgeDuration_);
	}
	if (justDodgeActive_) {
		ImGui::Text("Active: %.2f / %.2f s", justDodgeTimer_, justDodgeDuration_);
	}
#endif
}

void StagePlayScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	// 調整値の読み込み（プレイヤー生成 / RailCamera 設定より前に行う）
	LoadTuningFromJson();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -20.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	// Skybox 生成（DemoScene と同じ Cubemap を使用）
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, "Resources/Cubemaps/rogland_clear_night_4k.dds");
	object3DManager_->SetEnvironmentTexture("Resources/Cubemaps/rogland_clear_night_4k.dds");

	// レールカメラ用スプライン（位置）
	cameraPath_ = std::make_unique<SplineCurveActor>();
	cameraPath_->SetName("CameraPath");
	cameraPath_->SetTag(EntityTag::CameraPathSpline);
	cameraPath_->MutablePoints() = {
		{   0.0f, 5.0f,   0.0f },
		{  10.0f, 5.0f,  20.0f },
		{  20.0f, 8.0f,  40.0f },
		{  30.0f, 5.0f,  60.0f },
		{  40.0f, 5.0f,  80.0f },
	};

	// レールカメラ用スプライン（注視点）— cameraPath を +Z に平行移動して前進視点にする
	lookAtPath_ = std::make_unique<SplineCurveActor>();
	lookAtPath_->SetName("LookAtPath");
	lookAtPath_->SetTag(EntityTag::CameraLookAtSpline);
	{
		auto& src = cameraPath_->GetPoints();
		std::vector<Vector3> dst;
		dst.reserve(src.size());
		constexpr float kForwardOffset = 10.0f;
		for (const auto& p : src) {
			dst.push_back({ p.x, p.y, p.z + kForwardOffset });
		}
		lookAtPath_->MutablePoints() = std::move(dst);
	}

	// レールカメラコントローラ
	railCamera_ = std::make_unique<RailCameraController>();
	railCamera_->Initialize(camera_.get());
	railCamera_->SetCameraPath(cameraPath_.get());
	railCamera_->SetLookAtPath(lookAtPath_.get());
	railCamera_->SetSpeed(railCameraSpeed_);
	railCamera_->SetLoop(true);

	phase_ = Phase::Rail;
	paused_ = false;

	// レティクル
	reticle_ = std::make_unique<Reticle>();
	reticle_->Initialize(spriteManager_);

	// 前回保存したシーン配置があれば自動ロード（先にやって、Player が含まれていれば再生成しない）
	{
		const std::string kAutoLoadPath = "Resources/Json/Scenes/StagePlay.json";
		if (std::filesystem::exists(kAutoLoadPath)) {
			LoadSceneFromJson(kAutoLoadPath);
		}
	}

	// auto-load で Player が復元されていなければデフォルトのプレイヤープレハブを生成
	if (!player_) {
		InstantiatePrefab("player", { 0.0f, 0.0f, 0.0f });
		if (!dynamicAnimated_.empty()) {
			player_ = dynamicAnimated_.back().get();
			player_->SetName("Player");
		}
	}

	// ウェーブ定義ロード（存在しなければ空のまま=何も湧かない）
	{
		const std::string wavePath = "Resources/Json/Waves/stage1.json";
		if (std::filesystem::exists(wavePath)) {
			if (WaveDefIO::LoadFromFile(wavePath, currentWave_)) {
				spawnFired_.assign(currentWave_.entries.size(), false);
				retreatFired_.assign(currentWave_.entries.size(), false);
				killAtT_.assign(currentWave_.entries.size(), -1.0f);
			}
		}
	}
}

void StagePlayScene::Finalize() {}

void StagePlayScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	// Pause アクションでポーズトグル（メニュー実装は後で）
	if (actions->IsTriggered(static_cast<int>(Action::Pause))) {
		paused_ = !paused_;
	}
	if (paused_) {
		return;
	}

#ifdef _DEBUG
	// DEBUG専用ショートカット（StagePlayScene完成時に削除）
	auto* kb = input_->GetKeyboard();
	if (kb->TriggerKey(DIK_F2)) {
		SceneManager::GetInstance()->ChangeScene("RESULT", TransitionType::Fade);
		return;
	}
	if (kb->TriggerKey(DIK_F3)) {
		SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Fade);
		return;
	}
	if (kb->TriggerKey(DIK_F4)) {
		switch (phase_) {
		case Phase::Rail:    phase_ = Phase::Landing; break;
		case Phase::Landing: phase_ = Phase::Boss;    break;
		case Phase::Boss:    phase_ = Phase::Rail;    break;
		}
	}

	// スプライン可視化（Hierarchy の Debug 表示 ON 時のみ描かれる）
	if (cameraPath_) cameraPath_->DrawDebug();
	if (lookAtPath_) lookAtPath_->DrawDebug();
#endif

	// レール走行
	if (phase_ == Phase::Rail && railCamera_) {
		railCamera_->Update(GetScaledDeltaTime());
	}

	camera_->Update();
	UpdateDebugCameraIfActive();

	if (skybox_) {
		skybox_->Update();
	}

	// プレイヤー：入力でカメラ空間オフセットを動かしつつ、コライダーが画面端を越えないようクリップ
	if (player_) {
		const float dt = GetScaledDeltaTime();

		// ----- 入力で moveDelta (-1..1) を作る -----
		Vector2 moveDelta{ 0.0f, 0.0f };
		auto* kb = input_->GetKeyboard();
		if (kb->PuhsKey(DIK_A)) moveDelta.x -= 1.0f;
		if (kb->PuhsKey(DIK_D)) moveDelta.x += 1.0f;
		if (kb->PuhsKey(DIK_W)) moveDelta.y += 1.0f;
		if (kb->PuhsKey(DIK_S)) moveDelta.y -= 1.0f;
		if (auto* pad = input_->GetController(); pad && pad->IsConnected()) {
			const auto& ls = pad->GetLeftStick();
			moveDelta.x += ls.x * ls.magnitude;
			moveDelta.y += ls.y * ls.magnitude;
		}
		moveDelta.x = std::clamp(moveDelta.x, -1.0f, 1.0f);
		moveDelta.y = std::clamp(moveDelta.y, -1.0f, 1.0f);

		// 目標速度（入力 × 最大速度）に対して指数減衰で接近させる＝慣性
		Vector2 targetVel = {
			moveDelta.x * playerMoveSpeed_.x,
			moveDelta.y * playerMoveSpeed_.y,
		};
		float alpha = (playerSmoothTime_ > 1e-4f)
			? (1.0f - std::exp(-dt / playerSmoothTime_))
			: 1.0f;
		playerVelocity_.x += (targetVel.x - playerVelocity_.x) * alpha;
		playerVelocity_.y += (targetVel.y - playerVelocity_.y) * alpha;

		// 候補オフセット（クリップで反映可否を判定する）
		Vector2 candidate = {
			playerInputOffset_.x + playerVelocity_.x * dt,
			playerInputOffset_.y + playerVelocity_.y * dt,
		};

		const Matrix4x4& camWorld = camera_->GetWorldMatrix();
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();

		// 候補位置でコライダーが画面端 + margin を超えないかチェック。超える軸は据え置き。
		auto isInsideClip = [&](float ox, float oy) {
			Vector3 worldPos = TransformCoordinate(
				{ playerLocalOffset_.x + ox, playerLocalOffset_.y + oy, playerLocalOffset_.z },
				camWorld);

			// プレイヤーの回転＝カメラ回転前提でコライダーの軸を構築
			Matrix4x4 rot = MakeRotateMatrix(camera_->GetRotate());
			Vector3 axes[3] = {
				{ rot.m[0][0], rot.m[0][1], rot.m[0][2] },
				{ rot.m[1][0], rot.m[1][1], rot.m[1][2] },
				{ rot.m[2][0], rot.m[2][1], rot.m[2][2] },
			};

			const Collider& col = player_->GetCollider();
			Vector3 center = {
				worldPos.x + col.offset.x,
				worldPos.y + col.offset.y,
				worldPos.z + col.offset.z,
			};

			// 形状に応じた「外殻ローカル AABB の半幅」
			Vector3 he{ 0.0f, 0.0f, 0.0f };
			switch (col.shape) {
			case ColliderShape::Sphere:
				he = { col.radius, col.radius, col.radius };
				break;
			case ColliderShape::OBB:
				he = col.halfExtents;
				break;
			case ColliderShape::Capsule:
				he = { col.capsuleRadius, 0.5f * col.capsuleHeight + col.capsuleRadius, col.capsuleRadius };
				break;
			}

			// 8 角を World → Clip 投影して X/Y が許容範囲を超えるか
			const float xLim = 1.0f + playerClipMargin_.x;
			const float yLim = 1.0f + playerClipMargin_.y;
			for (int i = 0; i < 8; ++i) {
				float sx = (i & 1) ? +1.0f : -1.0f;
				float sy = (i & 2) ? +1.0f : -1.0f;
				float sz = (i & 4) ? +1.0f : -1.0f;
				Vector3 local = {
					center.x + axes[0].x * he.x * sx + axes[1].x * he.y * sy + axes[2].x * he.z * sz,
					center.y + axes[0].y * he.x * sx + axes[1].y * he.y * sy + axes[2].y * he.z * sz,
					center.z + axes[0].z * he.x * sx + axes[1].z * he.y * sy + axes[2].z * he.z * sz,
				};
				// w 除算で NDC へ
				float wx = local.x * vp.m[0][0] + local.y * vp.m[1][0] + local.z * vp.m[2][0] + vp.m[3][0];
				float wy = local.x * vp.m[0][1] + local.y * vp.m[1][1] + local.z * vp.m[2][1] + vp.m[3][1];
				float ww = local.x * vp.m[0][3] + local.y * vp.m[1][3] + local.z * vp.m[2][3] + vp.m[3][3];
				if (ww <= 1e-4f) return false;
				float ndcX = wx / ww;
				float ndcY = wy / ww;
				if (ndcX < -xLim || ndcX > xLim) return false;
				if (ndcY < -yLim || ndcY > yLim) return false;
			}
			return true;
		};

		// 軸ごとに採否判定（X 単独・Y 単独で押し戻し）。拒否された軸は速度もゼロに（壁にぶつかった扱い）。
		Vector2 next = playerInputOffset_;
		if (isInsideClip(candidate.x, playerInputOffset_.y)) {
			next.x = candidate.x;
		} else {
			playerVelocity_.x = 0.0f;
		}
		if (isInsideClip(next.x, candidate.y)) {
			next.y = candidate.y;
		} else {
			playerVelocity_.y = 0.0f;
		}
		playerInputOffset_ = next;

		Vector3 worldPos = TransformCoordinate(
			{ playerLocalOffset_.x + playerInputOffset_.x,
			  playerLocalOffset_.y + playerInputOffset_.y,
			  playerLocalOffset_.z },
			camWorld);
		player_->SetTranslate(worldPos);
		// 回転は照準ロジックで後段にてセット（camera 同期は廃止）
	}

	// BaseScene の動的エンティティ群（プレハブ生成物含む）の Update
	UpdateDynamicObjects();
	UpdateDynamicAnimated(GetScaledDeltaTime());
	UpdateDynamicPrimitives();
	UpdateDynamicSprites();
	DrawDynamicSplinesDebug();

	// 全シーン共通の EffectManager + GPUParticle を更新
	UpdateGlobalEffects(camera_.get(), GetScaledDeltaTime());

	// ジャスト回避演出のタイマ更新（World グループ dt に従う）
	UpdateJustDodgeEffect(GetScaledDeltaTime());

	// レティクル：Viewport 内でマウスが動いたときだけワープ（ImGui 操作と衝突させない）
	if (reticle_) {
		bool mouseHovered = false;
		bool mouseMoved   = false;
		Vector2 mouseClient{ 0.0f, 0.0f };
#ifdef _DEBUG
		auto* vp = ImGuiManager::Instance().GetViewportWindow();
		if (vp && vp->IsHovered()) {
			ImVec2 ip = vp->GetImageScreenPos();
			ImVec2 is = vp->GetImageScreenSize();
			ImVec2 ms = ImGui::GetMousePos();
			if (is.x > 0.0f && is.y > 0.0f) {
				mouseHovered = true;
				mouseClient.x = (ms.x - ip.x) * (static_cast<float>(WindowsApplication::kClientWidth) / is.x);
				mouseClient.y = (ms.y - ip.y) * (static_cast<float>(WindowsApplication::kClientHeight) / is.y);
				ImVec2 d = ImGui::GetIO().MouseDelta;
				mouseMoved = (d.x != 0.0f) || (d.y != 0.0f);
			}
		}
#else
		// Release ビルドはスワップチェーン直書き＝マウス座標がそのままクライアント
		auto* m = input_->GetMouse();
		if (m) {
			mouseClient.x = static_cast<float>(m->GetClientX());
			mouseClient.y = static_cast<float>(m->GetClientY());
			mouseHovered = true;
			mouseMoved = (m->GetDeltaX() != 0) || (m->GetDeltaY() != 0);
		}
#endif
		reticle_->Update(mouseHovered, mouseClient, mouseMoved,
			input_->GetController(), GetScaledDeltaTime());
	}

	// ----- 照準ターゲット計算（プレイヤー回転 + 発射方向の元） -----
	if (player_ && reticle_ && camera_) {
		const Vector2 rp = reticle_->GetPosition();
		const float ndcX = (rp.x / float(WindowsApplication::kClientWidth)) * 2.0f - 1.0f;
		const float ndcY = 1.0f - (rp.y / float(WindowsApplication::kClientHeight)) * 2.0f;
		const Matrix4x4 invVP = Inverse(camera_->GetViewProjectionMatrix());
		const Vector3 farWorld = TransformCoordinate(Vector3{ ndcX, ndcY, 1.0f }, invVP);
		const Vector3 camPos = camera_->GetTranslate();
		Vector3 rayDir{ farWorld.x - camPos.x, farWorld.y - camPos.y, farWorld.z - camPos.z };
		const float rayLen = std::sqrt(rayDir.x * rayDir.x + rayDir.y * rayDir.y + rayDir.z * rayDir.z);
		if (rayLen > 1e-6f) {
			rayDir.x /= rayLen; rayDir.y /= rayLen; rayDir.z /= rayLen;
		}

		// 敵レイヒット判定（Enemy タグ、Sphere collider のみ）
		bool hitEnemy = false;
		// bestT は「最も近い候補までの距離」。aimPlaneDistance_ は target 用なので使わない。
		float bestT = (std::numeric_limits<float>::max)();
		float hitEnemyRadius = 0.0f;
		// 軽ホーミング用：レティクル中心から最近の敵を画面距離で追跡
		IImGuiEditable* nearestLocal = nullptr;
		float nearestPx = (std::numeric_limits<float>::max)();
		// ロックオン対象（強ホーミング用）
		IImGuiEditable* lockedLocal = nullptr;
		Vector3 desiredTarget{
			camPos.x + rayDir.x * aimPlaneDistance_,
			camPos.y + rayDir.y * aimPlaneDistance_,
			camPos.z + rayDir.z * aimPlaneDistance_,
		};

		// 敵中心をワールド→スクリーン pixel に投影
		const Matrix4x4& vp = camera_->GetViewProjectionMatrix();
		auto projectToPixel = [&](const Vector3& w, Vector2& outPx, float& outClipW) -> bool {
			const float wx = w.x * vp.m[0][0] + w.y * vp.m[1][0] + w.z * vp.m[2][0] + vp.m[3][0];
			const float wy = w.x * vp.m[0][1] + w.y * vp.m[1][1] + w.z * vp.m[2][1] + vp.m[3][1];
			const float ww = w.x * vp.m[0][3] + w.y * vp.m[1][3] + w.z * vp.m[2][3] + vp.m[3][3];
			if (ww <= 1e-4f) return false; // カメラ後方
			const float ndcX = wx / ww;
			const float ndcY = wy / ww;
			outPx.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(WindowsApplication::kClientWidth);
			outPx.y = (1.0f - (ndcY * 0.5f + 0.5f)) * static_cast<float>(WindowsApplication::kClientHeight);
			outClipW = ww;
			return true;
		};

		const Matrix4x4& projMat = camera_->GetProjectionMatrix();
		const float invTanHalfFovY = projMat.m[1][1];
		const float clientH = static_cast<float>(WindowsApplication::kClientHeight);

		auto checkEnemy = [&](IImGuiEditable* e, const Vector3& pos) {
			if (!e || e->GetTag() != EntityTag::Enemy) return;
			const auto& col = e->GetCollider();
			if (!col.enabled || col.shape != ColliderShape::Sphere) return;
			const Vector3 center{ pos.x + col.offset.x, pos.y + col.offset.y, pos.z + col.offset.z };

			// 敵中心をスクリーン投影
			Vector2 enemyPx{};
			float clipW = 0.0f;
			if (!projectToPixel(center, enemyPx, clipW)) return;

			// カメラから敵中心までの距離
			const Vector3 toEnemy{ center.x - camPos.x, center.y - camPos.y, center.z - camPos.z };
			const float dist = std::sqrt(toEnemy.x * toEnemy.x + toEnemy.y * toEnemy.y + toEnemy.z * toEnemy.z);
			if (dist < 1e-4f) return;

			// 敵の見かけ半径(px)
			const float pixelRadius = col.radius * clientH * invTanHalfFovY * 0.5f / dist;

			// レティクルとの pixel 距離
			const float dx = enemyPx.x - rp.x;
			const float dy = enemyPx.y - rp.y;
			const float dPx = std::sqrt(dx * dx + dy * dy);

			// 許容範囲：見かけ半径 × アシスト倍率
			const float assistThreshold = pixelRadius * aimAssistPixelScale_;

			// 画面上最近の敵を更新（軽ホーミング先候補）
			if (dPx < nearestPx) {
				nearestPx = dPx;
				nearestLocal = e;
			}

			if (dPx <= assistThreshold && dist < bestT) {
				bestT = dist;
				// ロックオン時は敵中心へ吸い込み（パルテナ式）
				desiredTarget = center;
				hitEnemyRadius = col.radius;
				hitEnemy = true;
				lockedLocal = e;
			}
		};

		for (auto& p : dynamicPrimitives_) {
			if (p) checkEnemy(p.get(), *p->GetEditableTranslate());
		}
		for (auto& a : dynamicAnimated_) {
			if (a) checkEnemy(a.get(), a->GetTranslate());
		}
		for (auto& o : object3DInstances_) {
			if (o) checkEnemy(o.get(), *o->GetEditableTranslate());
		}

		// 弾発射用は Lerp 前の即時 target（ロックオン直後でも遅れずに敵へ向かう）
		firingTarget_ = desiredTarget;

		// プレイヤー回転用は Lerp でぬるっと追従
		if (!aimInitialized_) {
			aimTarget_ = desiredTarget;
			aimInitialized_ = true;
		} else {
			const float dtP = GetScaledDeltaTime(TimeGroup::Player);
			const float alpha = (aimSmoothTime_ > 1e-4f)
				? (1.0f - std::exp(-dtP / aimSmoothTime_))
				: 1.0f;
			aimTarget_.x += (desiredTarget.x - aimTarget_.x) * alpha;
			aimTarget_.y += (desiredTarget.y - aimTarget_.y) * alpha;
			aimTarget_.z += (desiredTarget.z - aimTarget_.z) * alpha;
		}

		// プレイヤー回転を player → aimTarget 方向に
		const Vector3 playerPos = player_->GetTranslate();
		const Vector3 toAim{ aimTarget_.x - playerPos.x, aimTarget_.y - playerPos.y, aimTarget_.z - playerPos.z };
		const float horiz = std::sqrt(toAim.x * toAim.x + toAim.z * toAim.z);
		if (horiz > 1e-4f || std::abs(toAim.y) > 1e-4f) {
			const float yaw = std::atan2(toAim.x, toAim.z);
			const float pitch = -std::atan2(toAim.y, horiz);
			player_->SetRotate({ pitch, yaw, 0.0f });
		}

		// ロックオン時：敵の見かけサイズ（スクリーンpixel半径）を計算してレティクルへ
		if (hitEnemy && reticle_) {
			// 敵中心までのカメラからの距離（ray の t をそのまま使える）
			const float distToEnemy = bestT;
			// projection の m[1][1] = 1 / tan(fovY/2) なので、
			// pixelRadius = radius * clientHeight * m[1][1] / (2 * dist)
			const Matrix4x4& proj = camera_->GetProjectionMatrix();
			const float invTanHalfFovY = proj.m[1][1];
			const float pixelRadius = (distToEnemy > 1e-4f)
				? (hitEnemyRadius * static_cast<float>(WindowsApplication::kClientHeight)
					* invTanHalfFovY * 0.5f / distToEnemy)
				: 0.0f;
			// レティクルは外周マージン付きで少し大きめに（敵を囲む見た目に）
			constexpr float kReticleMargin = 1.25f;
			reticle_->SetLockOnTargetSize(pixelRadius * 2.0f * kReticleMargin);
		}
		reticle_->SetLockOn(hitEnemy);

		// 弾発射時のホーミング選択用にメンバへ反映
		lockedEnemy_  = lockedLocal;
		nearestEnemy_ = nearestLocal;
	}

	// ゲーム時間が止まっている時はゲームロジック（射撃／弾の進行）をスキップ。
	// TimeScale=0（"Pause" ボタン等）でシーンに敵を配置するときに弾が湧き続けないようにする。
	const float worldDt = GetScaledDeltaTime(TimeGroup::World);
	const bool gameFrozen = worldDt <= 0.0001f;

	// ----- 射撃（プレイヤー位置から aim ターゲット方向へ） -----
	if (!gameFrozen) {
		const float dtP = GetScaledDeltaTime(TimeGroup::Player);
		fireTimer_ -= dtP;
		if (fireTimer_ < 0.0f) fireTimer_ = 0.0f;

		bool firePressed = actions->IsPressed(static_cast<int>(Action::Fire));
#ifdef _DEBUG
		// Debug ビルドでは、マウスが Viewport 上に無く、かつ別の ImGui ウィンドウ
		// （Save ボタン等）がマウスをキャプチャ中の時は Fire を抑制する。
		// Viewport ホバー中はそのまま通す（Viewport も ImGui ウィンドウだから WantCaptureMouse は常に true）
		{
			bool mouseOverViewport = false;
			if (auto* vp = ImGuiManager::Instance().GetViewportWindow()) {
				mouseOverViewport = vp->IsHovered();
			}
			if (!mouseOverViewport && ImGui::GetIO().WantCaptureMouse) {
				firePressed = false;
			}
		}
#endif
		if (firePressed && fireTimer_ <= 0.0f && player_) {
			const Vector3 origin = player_->GetTranslate();
			// 発射方向は Lerp 前の即時 target を使う（ロックオン直後の弾が遅れて飛ぶのを防ぐ）
			Vector3 dir{ firingTarget_.x - origin.x, firingTarget_.y - origin.y, firingTarget_.z - origin.z };
			const float len = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
			if (len > 1e-6f) {
				dir.x /= len; dir.y /= len; dir.z /= len;
				// ロックオン中なら強ホーミング、外なら画面上最近の敵に軽ホーミング
				IImGuiEditable* homeTarget = lockedEnemy_ ? lockedEnemy_ : nearestEnemy_;
				const float homeStrength = lockedEnemy_
					? bulletHomingLockOnBoost_
					: (homeTarget ? bulletHomingStrength_ : 0.0f);
				// 弾は aim plane に到達した時点で消滅させ、面より奥への乱射を防ぐ
				const int atk = (player_ && player_->HasAttackPower()) ? player_->GetAttackPower() : 0;
				SpawnPlayerBullet(origin, dir, bulletSpeed_, bulletLifetime_,
					bulletColliderGrowth_, homeTarget, homeStrength,
					aimPlaneDistance_, "TemporaryPlayerBullet", atk);
				fireTimer_ = fireRate_;
			}
		}
	}

	// 弾の進行と寿命処理（World 時間軸で動かす）
	if (!gameFrozen) {
		UpdateBullets(worldDt);

		// SweepDeadEntities の前に、HP がゼロになった敵のスポーンエントリに kill t を記録
		// （SweepDeadEntities が DestroyDynamicEntity 経由で movingEnemies_ の entity を null 化する前に行う）
		{
			const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
			for (auto& m : movingEnemies_) {
				if (!m.entity) continue;
				if (m.waveEntryIndex < 0) continue;
				if (static_cast<size_t>(m.waveEntryIndex) >= killAtT_.size()) continue;
				if (killAtT_[m.waveEntryIndex] >= 0.0f) continue; // 既記録
				if (m.entity->GetHP().IsDead()) {
					killAtT_[m.waveEntryIndex] = currentT;
				}
			}
		}

		// HP がゼロになった敵などを破棄キューへ
		SweepDeadEntities();
	}

	// スポーン：カメラ進行度 t でエントリをトリガー
	if (!gameFrozen) {
		const float currentT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
			const WaveEntry& we = currentWave_.entries[i];

			// スポーントリガー
			if (i < spawnFired_.size() && !spawnFired_[i] && currentT >= we.triggerT) {
				IImGuiEditable* spawned = nullptr;
				if (!we.splineId.empty()) {
					SplineCurveActor* sp = FindDynamicSplineByName(we.splineId);
					if (sp) {
						// Rusher は終端で止まる（removeAtEnd=false）
						const bool removeAtEnd = (we.enemyType != "Rusher");
						spawned = SpawnEnemyOnSpline(we.prefab, sp, we.speed,
							removeAtEnd, 0.0f, static_cast<int>(i));
					} else {
						LogBuffer::Instance().Add(
							std::string("Wave: spline not found: ") + we.splineId,
							LogBuffer::Level::Warning);
					}
				}

				// EnemyController を生成してコマンドを設定
				if (spawned) {
					auto ctrl = std::make_unique<EnemyController>();
					ctrl->entity_           = spawned;
					ctrl->waveEntryIndex_   = static_cast<int>(i);
					ctrl->billboardToPlayer_ = (we.enemyType != "Carrier");
					ctrl->triggerT_         = we.triggerT;
					ctrl->shootIntervalT_   = we.shootIntervalT;
					ctrl->spawnIntervalSec_ = we.spawnIntervalSec;
					ctrl->spawnLimit_       = we.spawnLimit;
					ctrl->childPrefab_      = we.prefab; // 運び屋が生成するザコは同じプレハブ
					ctrl->childSplineId_    = we.splineId;
					ctrl->Init(EnemyCommandFactory::Create(we));

					// MovingEnemy にコントローラを紐付け
					for (auto& m : movingEnemies_) {
						if (m.entity == spawned) {
							m.controller       = ctrl.get();
							m.billboardToPlayer = ctrl->billboardToPlayer_;
							break;
						}
					}
					enemyControllers_.push_back(std::move(ctrl));
				}
				spawnFired_[i] = true;
			}

			// 退避トリガー
			if (i < retreatFired_.size() && i < spawnFired_.size()
				&& spawnFired_[i] && !retreatFired_[i]
				&& we.retreatT >= 0.0f && currentT >= we.retreatT) {
				// 対応するコントローラに退避を指示
				for (auto& ctrl : enemyControllers_) {
					if (ctrl && ctrl->waveEntryIndex_ == static_cast<int>(i)) {
						ctrl->TriggerRetreat();
						break;
					}
				}
				retreatFired_[i] = true;
			}
		}
	}

	// 敵コントローラ更新（自由移動・ビルボード・退避完了処理）
	if (!gameFrozen) {
		const float cameraT = railCamera_ ? railCamera_->GetProgress() : 0.0f;
		UpdateEnemyControllers(worldDt, player_, cameraT);
	}

	// スプライン追従敵の進行
	if (!gameFrozen) {
		UpdateMovingEnemies(worldDt);
	}
}

void StagePlayScene::Draw() {
	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto* commandList = dxCore_->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	if (skybox_) {
		skybox_->Draw(dxCore_);
	}

	// 通常 DSV に戻す（以降のオブジェクト描画用）
	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	// 3Dオブジェクトの共通描画設定（Object3D / Animated 描画前に必要）
	object3DManager_->DrawSetting();

	// rootParameter[3]=DirectionalLight / [5]=PointLight / [6]=SpotLight を bind
	// （Object3DInstance::Draw はライト系を bind しないため、ここで一括設定しないと GBV #935 が出る）
	LightManager::GetInstance()->BindLights(commandList);

	// BaseScene の動的エンティティ描画
	DrawDynamicObjects();
	DrawDynamicAnimated();
	DrawDynamicPrimitives();

	// 全シーン共通の GPUParticle + Effect Editor 描画
	DrawGlobalEffects();

	DrawSceneCameraGizmo();
	DrawDynamicAnimatedSkeletonDebug();

	// スプライト
	spriteManager_->DrawSetting();
	DrawDynamicSprites();
	if (reticle_) reticle_->Draw();

	// DebugDraw（スプライン可視化など）の出力
	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();

#ifdef _DEBUG
	// Effect Editor プレビュー RT への描画。
	// これを呼ばないと EffectEditor が開かれた瞬間に RT のバリアが整わず
	// SRV 読み出しと衝突して D3D12 GPU 検証エラーになる。
	if (auto* edit = ImGuiManager::Instance().GetEffectEditorWindow()) {
		edit->Render();

		// Scene RT に戻して以降の描画が漏れないようにする
		auto rtv = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
		auto dsv = dxCore_->GetDsvHandle();
		commandList->OMSetRenderTargets(1, &rtv, false, &dsv);
		D3D12_VIEWPORT vp{};
		vp.Width = static_cast<float>(WindowsApplication::kClientWidth);
		vp.Height = static_cast<float>(WindowsApplication::kClientHeight);
		vp.MaxDepth = 1.0f;
		commandList->RSSetViewports(1, &vp);
		D3D12_RECT sc{ 0, 0,
			static_cast<LONG>(WindowsApplication::kClientWidth),
			static_cast<LONG>(WindowsApplication::kClientHeight) };
		commandList->RSSetScissorRects(1, &sc);
	}
#endif
}

void StagePlayScene::Seek(float seconds) {
	BaseScene::Seek(seconds);

	// RailCamera の進行度を経過秒から再構築する（speed と loop を尊重）
	if (railCamera_) {
		float t = seconds * railCameraSpeed_;
		// loop = true 前提で 0..1 へ正規化
		t -= std::floor(t);
		railCamera_->SetProgress(t);
		// Seek 結果を即カメラに反映（dt=0 で Update）
		railCamera_->Update(0.0f);
		camera_->Update();
	}

	// ----- ゲーム状態を Seek 先に合わせてリセット -----
	// 現在生きている敵・弾・スプライン追従敵をすべて掃除
	movingEnemies_.clear();
	bullets_.clear();
	for (auto& p : dynamicPrimitives_) {
		if (!p) continue;
		const EntityTag t = p->GetTag();
		if (t == EntityTag::Enemy || t == EntityTag::Boss
			|| t == EntityTag::PlayerAttack || t == EntityTag::EnemyAttack) {
			deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(p.release()));
		}
	}
	dynamicPrimitives_.erase(
		std::remove_if(dynamicPrimitives_.begin(), dynamicPrimitives_.end(),
			[](const std::unique_ptr<PrimitiveInstance>& p) { return !p; }),
		dynamicPrimitives_.end());

	for (auto& a : dynamicAnimated_) {
		if (!a) continue;
		const EntityTag t = a->GetTag();
		if (t == EntityTag::Enemy || t == EntityTag::Boss) {
			deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(a.release()));
		}
	}
	dynamicAnimated_.erase(
		std::remove_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
			[](const std::unique_ptr<AnimatedObject3DInstance>& a) { return !a; }),
		dynamicAnimated_.end());

	// スポーン/退避フラグ / kill t を Seek 先に合わせて再構築
	const float seekT = railCamera_ ? railCamera_->GetProgress() : 0.0f;

	if (spawnFired_.size() != currentWave_.entries.size())
		spawnFired_.assign(currentWave_.entries.size(), false);
	if (retreatFired_.size() != currentWave_.entries.size())
		retreatFired_.assign(currentWave_.entries.size(), false);
	if (killAtT_.size() != currentWave_.entries.size())
		killAtT_.assign(currentWave_.entries.size(), -1.0f);

	// seek 先より後の kill は未発生扱いに戻す
	for (size_t i = 0; i < killAtT_.size(); ++i) {
		if (killAtT_[i] > seekT) killAtT_[i] = -1.0f;
	}

	// フラグを seekT から再構築
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		spawnFired_[i]  = (seekT >= we.triggerT);
		retreatFired_[i] = (we.retreatT >= 0.0f && seekT >= we.retreatT);
	}

	// 生存中の敵をスプライン上の正しい位置で復元
	for (size_t i = 0; i < currentWave_.entries.size(); ++i) {
		const WaveEntry& we = currentWave_.entries[i];
		if (!spawnFired_[i]) continue;
		if (retreatFired_[i]) continue;
		if (killAtT_[i] >= 0.0f) continue;
		if (we.splineId.empty()) continue;

		// camera t → スポーンからの経過秒 → スプライン上の t に変換
		const float timeSinceSpawn = (seekT - we.triggerT) / railCameraSpeed_;
		const float tOnSpline = std::clamp(timeSinceSpawn * we.speed, 0.0f, 1.0f);
		if (tOnSpline >= 1.0f) continue;

		SplineCurveActor* sp = FindDynamicSplineByName(we.splineId);
		if (!sp) continue;
		SpawnEnemyOnSpline(we.prefab, sp, we.speed, true, tOnSpline, static_cast<int>(i));
	}
}

Camera* StagePlayScene::GetCamera() {
	return camera_.get();
}

// =====================================================================
// シーン保存 / 読込（DemoScene の実装を流用）
// =====================================================================
namespace {
	JsonValue Vec3ToJson(const Vector3& v) {
		JsonValue arr = JsonValue::MakeArray();
		arr.Push(JsonValue(static_cast<double>(v.x)));
		arr.Push(JsonValue(static_cast<double>(v.y)));
		arr.Push(JsonValue(static_cast<double>(v.z)));
		return arr;
	}
	Vector3 JsonToVec3(const JsonValue& v, const Vector3& fallback = {}) {
		if (!v.IsArray() || v.Size() < 3) return fallback;
		return {
			static_cast<float>(v[0].AsDouble(fallback.x)),
			static_cast<float>(v[1].AsDouble(fallback.y)),
			static_cast<float>(v[2].AsDouble(fallback.z))
		};
	}
	JsonValue TransformToJson(const Vector3& s, const Vector3& r, const Vector3& t) {
		JsonValue obj = JsonValue::MakeObject();
		obj["scale"] = Vec3ToJson(s);
		obj["rotate"] = Vec3ToJson(r);
		obj["translate"] = Vec3ToJson(t);
		return obj;
	}
}

bool StagePlayScene::SaveSceneToJson(const std::string& filePath) {
	JsonValue root = JsonValue::MakeObject();
	root["scene"] = "StagePlayScene";

	JsonValue arr = JsonValue::MakeArray();

	for (const auto& o : object3DInstances_) {
		if (!o) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Object3D";
		e["name"] = o->GetName();
		e["tag"] = std::string(GetTagName(o->GetTag()));
		e["dir"] = o->GetDirectoryPath();
		e["file"] = o->GetModelFileName();
		e["transform"] = TransformToJson(o->GetScale(), o->GetRotate(), o->GetTranslate());
		arr.Push(std::move(e));
	}
	for (const auto& a : dynamicAnimated_) {
		if (!a) continue;
		// プレイヤーは常にプレハブから復元するので保存しない（collider 等のプレハブ属性を確実に反映するため）
		if (a->GetTag() == EntityTag::Player) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "AnimatedObject3D";
		e["name"] = a->GetName();
		e["tag"] = std::string(GetTagName(a->GetTag()));
		e["dir"] = a->GetDirectoryPath();
		e["file"] = a->GetModelFileName();
		e["transform"] = TransformToJson(a->GetScale(), a->GetRotate(), a->GetTranslate());
		arr.Push(std::move(e));
	}
	for (const auto& p : dynamicPrimitives_) {
		if (!p) continue;
		// 弾は一時オブジェクトなのでシーン保存に含めない
		const EntityTag t = p->GetTag();
		if (t == EntityTag::PlayerAttack || t == EntityTag::EnemyAttack) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Primitive";
		e["name"] = p->GetName();
		e["tag"] = std::string(GetTagName(t));
		e["primitiveType"] = static_cast<int64_t>(p->GetPrimitiveType());
		const Transform& tr = p->GetMesh().GetTransform();
		e["transform"] = TransformToJson(tr.scale, tr.rotate, tr.translate);
		if (!p->GetTextureFilePath().empty()) {
			e["texture"] = p->GetTextureFilePath();
		}
		arr.Push(std::move(e));
	}
	for (const auto& s : dynamicSprites_) {
		if (!s) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Sprite";
		e["name"] = s->GetName();
		e["tag"] = std::string(GetTagName(s->GetTag()));
		e["texture"] = s->GetTextureFilePath();
		const Vector2& pos = s->GetPosition();
		JsonValue p = JsonValue::MakeArray();
		p.Push(JsonValue(static_cast<double>(pos.x)));
		p.Push(JsonValue(static_cast<double>(pos.y)));
		e["pos"] = std::move(p);
		arr.Push(std::move(e));
	}
	for (const auto& sp : dynamicSplines_) {
		if (!sp) continue;
		JsonValue e = JsonValue::MakeObject();
		e["type"] = "Spline";
		e["name"] = sp->GetName();
		e["tag"] = std::string(GetTagName(sp->GetTag()));
		JsonValue ptsArr = JsonValue::MakeArray();
		for (const auto& pt : sp->GetPoints()) {
			ptsArr.Push(Vec3ToJson(pt));
		}
		e["points"] = std::move(ptsArr);
		arr.Push(std::move(e));
	}

	root["objects"] = std::move(arr);

	std::filesystem::path path(filePath);
	if (path.has_parent_path()) {
		std::error_code ec;
		std::filesystem::create_directories(path.parent_path(), ec);
	}

	bool ok = JsonWriter::WriteFile(filePath, root, { true, 2 });
	LogBuffer::Instance().Add(
		ok ? ("Scene saved: " + filePath)
		   : ("Scene save FAILED: " + filePath),
		ok ? LogBuffer::Level::Info : LogBuffer::Level::Error);
	return ok;
}

bool StagePlayScene::LoadSceneFromJson(const std::string& filePath) {
	auto result = JsonParser::ParseFile(filePath);
	if (!result.success) {
		LogBuffer::Instance().Add(
			"Scene load FAILED: " + filePath + " (" + result.errorMessage + ")",
			LogBuffer::Level::Error);
		return false;
	}

	// 既存の動的オブジェクトを全削除
	for (auto& o : object3DInstances_) {
		if (o) deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(o.release()));
	}
	object3DInstances_.clear();
	for (auto& a : dynamicAnimated_) {
		if (a) deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(a.release()));
	}
	dynamicAnimated_.clear();
	for (auto& m : dynamicAnimatedModels_) {
		if (m) deferredDeletes_.emplace_back(std::shared_ptr<AnimatedModelInstance>(m.release()));
	}
	dynamicAnimatedModels_.clear();
	for (auto& s : dynamicSprites_) {
		if (s) deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(s.release()));
	}
	dynamicSprites_.clear();
	for (auto& p : dynamicPrimitives_) {
		if (p) deferredDeletes_.emplace_back(std::shared_ptr<PrimitiveInstance>(p.release()));
	}
	dynamicPrimitives_.clear();
	for (auto& sp : dynamicSplines_) {
		if (sp) deferredDeletes_.emplace_back(std::shared_ptr<SplineCurveActor>(sp.release()));
	}
	dynamicSplines_.clear();

	// LoadScene で全エンティティが消えたので参照ポインタもリセット
	player_ = nullptr;
	movingEnemies_.clear();
	bullets_.clear();

	const JsonValue& objs = result.value["objects"];
	if (!objs.IsArray()) return true;

	for (size_t i = 0; i < objs.Size(); ++i) {
		const JsonValue& e = objs[i];
		std::string type = e["type"].AsString();
		std::string name = e["name"].AsString();
		EntityTag tag = TagFromName(e["tag"].AsString());
		Vector3 sc = JsonToVec3(e["transform"]["scale"], { 1,1,1 });
		Vector3 ro = JsonToVec3(e["transform"]["rotate"], { 0,0,0 });
		Vector3 tr = JsonToVec3(e["transform"]["translate"], { 0,0,0 });

		if (type == "Object3D") {
			AddDynamicObject(e["dir"].AsString(), e["file"].AsString(), tr);
			if (!object3DInstances_.empty()) {
				auto& back = object3DInstances_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
			}
		} else if (type == "AnimatedObject3D") {
			AddDynamicAnimated(e["dir"].AsString(), e["file"].AsString(), tr);
			if (!dynamicAnimated_.empty()) {
				auto& back = dynamicAnimated_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
				if (tag == EntityTag::Player) player_ = back.get();
			}
		} else if (type == "Primitive") {
			// 弾は一時オブジェクトなのでロードでも無視（過去の汚れた save 対策）
			if (tag == EntityTag::PlayerAttack || tag == EntityTag::EnemyAttack) {
				continue;
			}
			int primType = static_cast<int>(e["primitiveType"].AsInt());
			AddDynamicPrimitive(primType, tr);
			if (!dynamicPrimitives_.empty()) {
				auto& back = dynamicPrimitives_.back();
				back->SetName(name);
				back->SetTag(tag);
				back->SetScale(sc);
				back->SetRotate(ro);
				std::string tex = e["texture"].AsString();
				if (!tex.empty()) back->SetTexture(tex);
			}
		} else if (type == "Sprite") {
			float cx = static_cast<float>(e["pos"][0].AsDouble(0.0));
			float cy = static_cast<float>(e["pos"][1].AsDouble(0.0));
			AddDynamicSprite(e["texture"].AsString(), cx, cy);
			if (!dynamicSprites_.empty()) {
				auto& back = dynamicSprites_.back();
				back->SetName(name);
				back->SetTag(tag);
			}
		} else if (type == "Spline") {
			AddDynamicSpline(static_cast<int>(tag));
			if (!dynamicSplines_.empty()) {
				auto& back = dynamicSplines_.back();
				back->SetName(name);
				back->MutablePoints().clear();
				const JsonValue& pts = e["points"];
				if (pts.IsArray()) {
					for (size_t pi = 0; pi < pts.Size(); ++pi) {
						back->AddPoint(JsonToVec3(pts[pi]));
					}
				}
			}
		}
	}

	LogBuffer::Instance().Add("Scene loaded: " + filePath, LogBuffer::Level::Info);
	return true;
}
