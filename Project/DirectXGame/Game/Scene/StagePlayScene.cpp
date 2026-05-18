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
#include "Components/SphereCollider.h"
#include "KeyboardInput.h"
#include "ControllerInput.h"
#include "UI/Reticle.h"

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

	// プレイヤープレハブを生成（カメラの子として毎フレ配置）
	InstantiatePrefab("player", { 0.0f, 0.0f, 0.0f });
	if (!dynamicAnimated_.empty()) {
		player_ = dynamicAnimated_.back().get();
		player_->SetName("Player");
	}

	// レティクル
	reticle_ = std::make_unique<Reticle>();
	reticle_->Initialize(spriteManager_);
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
		player_->SetRotate(camera_->GetRotate());
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
}

Camera* StagePlayScene::GetCamera() {
	return camera_.get();
}
