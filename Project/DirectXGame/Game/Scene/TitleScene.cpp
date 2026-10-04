#include "TitleScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "ModelInstance.h"
#include "TextureManager.h"
#include "Skybox.h"
#include "LightManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "PostEffect.h"
#include "VignetteEffect.h"
#include "RadialBlurEffect.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "MathUtility.h"
#include "Water/WaterReflection.h"
#include "Water/WaterSurface.h"
#include "Cloud/CloudLayer.h"
#include <Windows.h>
#include <algorithm>
#include <cmath>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	// 雲なしの昼の青空（tools/BlenderPipeline/gen_title_sky.py で生成。太陽の円盤は描いていない）
	constexpr const char* kTitleSkyboxPath = "Resources/Cubemaps/title_clear_sky.dds";
	// 映り込み（PBR の IBL）専用。地平線より下を石っぽい暖色にしてあり、カメラより低い所の金が
	// 明るい水色を映して白っぽくなるのを防ぐ（gen_title_sky.py --ibl）。背景・水面は通常版を使う
	constexpr const char* kTitleIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";
	// 空の太陽の向きから求めた光の進む向き（cubemap 上の太陽は仰角 55°、方向 (-0.191, 0.815, -0.547)）
	const Vector3 kSunLightDirection = { 0.191f, -0.815f, 0.547f };
	// 日差しは少し暖色にして、空の青い環境光で白い大理石が青く染まるのを打ち消す
	const Vector4 kSunLightColor = { 1.0f, 0.96f, 0.9f, 1.0f };

	// 水底の床（仮素材。石畳の素材ができたら差し替える）
	constexpr const char* kWaterFloorTexturePath = "Resources/Textures/Terrain/_TestRock_BaseColor.dds";
	constexpr float kWaterHeight = 0.0f;
	constexpr float kWaterDepth = 0.1f;
	constexpr float kCameraFarClip = 1000.0f;
	constexpr float kWaterSize = 1500.0f; // 半径 750m < ファークリップ

	// 遠景の雲のノイズ（tools/Python/gen_cloud_noise.py で生成。R=形 / G=マスク）
	constexpr const char* kCloudNoisePath = "Resources/Textures/Cloud/cloud_noise.dds";

	// 扉（tools/BlenderPipeline/gen_title_door.py で生成）。枠の原点は台座の底面の中央で、正面が +Z。
	// 扉板の蝶番の軸（枠のローカル）。左右は正面から見た向き（エンジンの +X が正面から見て左）
	constexpr const char* kDoorModelDir = "Resources/Models/TitleDoor";
	const Vector3 kDoorHingeL = { 0.9f, 0.0f, -0.2f };
	const Vector3 kDoorHingeR = { -0.9f, 0.0f, -0.2f };

	enum TitleMenuItem : int {
		kTitleStart = 0,
		kTitleQuit,
	};

	// 終端で少しだけ行き過ぎて戻る ease-out-back（ロゴの「ポン」と出る感じ）
	float EaseOutBack(float t) {
		const float c1 = 1.70158f;
		const float c3 = c1 + 1.0f;
		const float u = t - 1.0f;
		return 1.0f + c3 * u * u * u + c1 * u * u;
	}
}

TitleScene::TitleScene() = default;
TitleScene::~TitleScene() = default;

void TitleScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	// 位置と向きは UpdateCameraAndLogo で周回中心から決める。
	// 既定の 100m だと水面が途中で切れて地平線の手前に空が覗くので、水面の端より遠くまで取る
	camera_->SetFarClip(kCameraFarClip);
	orbitAngle_ = 0.0f;
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, kTitleSkyboxPath);
	// PBR の IBL（t1）。無いと金属は真っ黒、大理石も環境光が無く暗くなる
	TextureManager::GetInstance()->LoadTexture(kTitleIblPath);
	object3DManager_->SetEnvironmentTexture(kTitleIblPath);

	logo_ = std::make_unique<Object3DInstance>();
	logo_->Initialize(object3DManager_, dxCore_, "Resources/Models/Title", "title.mesh", "TitleLogo");

	waterReflection_ = std::make_unique<WaterReflection>();
	waterReflection_->Initialize(dxCore_, srvManager_, object3DManager_, 1.0f);
	waterReflection_->SetWaterHeight(kWaterHeight);
	if (showLogo_) waterReflection_->AddTarget(logo_.get());

	doorFrame_ = std::make_unique<Object3DInstance>();
	doorFrame_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_frame.mesh", "TitleDoorFrame");
	doorLeafL_ = std::make_unique<Object3DInstance>();
	doorLeafL_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_leaf_L.mesh", "TitleDoorLeafL");
	doorLeafR_ = std::make_unique<Object3DInstance>();
	doorLeafR_->Initialize(object3DManager_, dxCore_, kDoorModelDir, "door_leaf_R.mesh", "TitleDoorLeafR");
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		waterReflection_->AddTarget(part);
	}

	water_ = std::make_unique<WaterSurface>();
	water_->Initialize(dxCore_, object3DManager_, kWaterFloorTexturePath);
	water_->SetWaterHeight(kWaterHeight);
	water_->GetParams().depth = kWaterDepth;
	water_->SetSize(kWaterSize);
	// 待機中は「ごく小さな波がたまに出る」程度に抑える（大きな波は演出側で EmitRing / StartRingBurst）
	water_->GetParams().ringAmplitude = 0.013f;
	water_->GetParams().ringInterval = 6.0f;
	water_->GetParams().ringJitter = 0.7f;

	cloudLayer_ = std::make_unique<CloudLayer>();
	cloudLayer_->Initialize(dxCore_, kCloudNoisePath);
	cloudLayer_->SetSunDirection({ -kSunLightDirection.x, -kSunLightDirection.y, -kSunLightDirection.z });
	// --no-clouds：雲なしで起動（tools/Python/run_cloud_bench.py で雲あり/なしの GPU 時間を比べる用）。
	// enabled=0 ならシェーダは先頭で抜けるので、差がそのまま雲のコストになる
	if (std::wstring(::GetCommandLineW()).find(L"--no-clouds") != std::wstring::npos) {
		cloudLayer_->GetParams().enabled = false;
	}
	skybox_->SetCloudLayer(cloudLayer_.get());
	// PBR の鏡面反射（金の装飾）にも同じ雲を映す。シーンをまたいで残るので Finalize で外す
	object3DManager_->SetCloudLayer(cloudLayer_.get());
	water_->SetCloudLayer(cloudLayer_.get());

	// 平行光源の既定 intensity は 0 なので、ロゴを照らす光をここで必ず設定する
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(kSunLightDirection);
		lm->SetDirectionalLightColor(kSunLightColor);
		lm->SetDirectionalLightIntensity(sunIntensity_);
	}

	if (auto* pe = Game::GetPostEffect()) {
		if (pe->vignette) {
			pe->vignette->SetEnabled(true);
			pe->vignette->SetIntensity(vignetteIntensity_);
		}
		if (pe->radialBlur) {
			pe->radialBlur->SetEnabled(true);
			pe->radialBlur->SetCenter(0.5f, 0.5f);
			pe->radialBlur->SetBlurWidth(introBlurWidth_);
		}
	}

	menu_.SetItems({ "スタート", "ゲーム終了" });
	SoundManager::GetInstance()->Play2DSoundLooped("bgm_title", 0.5f);
	menuOpen_ = false;
	elapsed_ = 0.0f;
	idleSeconds_ = 0.0f;
}

void TitleScene::Finalize() {
	Game::GetPostEffect()->ResetEffects();
	SoundManager::GetInstance()->Stop2DSound("bgm_title");
	// cloudLayer_ はこのシーンと一緒に消えるので、共有の Object3DManager から外しておく
	object3DManager_->SetCloudLayer(nullptr);
}

void TitleScene::Update() {
	const float dt = GetScaledDeltaTime();
	elapsed_ += dt;

	// 遷移中も背景とロゴは動かし続ける（止まった画面を見せない）
	UpdateCameraAndLogo(dt);
	UpdateDoor();
	gameViewProjection_ = camera_->GetViewProjectionMatrix();
	gameEyePosition_ = camera_->GetTranslate();

	// デバッグカメラ ON ならここでカメラ行列が差し替わる。
	// Skybox / ロゴはその時点の VP を CB に焼くので、差し替えの後で Update する
	UpdateDebugCameraIfActive();
	skybox_->Update(dt);
	// 空側のレイの原点は描画カメラ（デバッグカメラ中はそちら）
	if (cloudLayer_) cloudLayer_->Update(dt, camera_->GetTranslate());
	logo_->Update();
	doorFrame_->Update();
	doorLeafL_->Update();
	doorLeafR_->Update();
	if (water_) water_->Update(dt);

	UpdateIntroPostEffect();

#ifdef _DEBUG
	OnImGuiTuning();
#endif

	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	UpdateInput();
}

void TitleScene::UpdateInput() {
	auto* actionMap = input_->GetActionMap();
	if (!actionMap) return;

	// ロゴ登場中の入力は受け付けない（演出を飛ばして即メニューにならないように）
	if (elapsed_ < introDuration_) return;

	if (!menuOpen_) {
		if (actionMap->AnyInputTriggered()) {
			// 押したフレームはメニュー入力を読まない（同じキーで即決定されるのを防ぐ）
			menuOpen_ = true;
			SoundManager::GetInstance()->Play2DSound("se_ui_decide");
			menu_.SetSelectedIndex(kTitleStart);
			idleSeconds_ = 0.0f;
			return;
		}
		// 無操作カウンタ更新（後でデモ動画再生のトリガーに使う）
		idleSeconds_ += GetScaledDeltaTime();
		(void)kDemoTriggerSeconds; // 警告抑制。動画再生実装時に使用
		return;
	}

	const VerticalMenu::Result result = menu_.Update(actionMap);
	if (result == VerticalMenu::Result::Canceled) {
		menuOpen_ = false;
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kTitleStart:
			SceneManager::GetInstance()->ChangeScene("HUB", TransitionType::Stripe);
			return;
		case kTitleQuit:
			PostQuitMessage(0);
			return;
		}
	}
}

void TitleScene::UpdateCameraAndLogo(float dt) {
	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	if (waterReflection_) waterReflection_->SetWaterHeight(waterHeight);

	// カメラ：周回中心の周りを回り、中心の aimHeight_ を見る。
	// Yaw=θ のときの前方は (sinθ, 0, cosθ) なので、中心からその逆向きに orbitRadius_ 離れた所に置く
	orbitAngle_ += orbitSpeed_ * dt;
	const float pitch = std::atan2(cameraHeight_ - aimHeight_, orbitRadius_); // 正で見下ろす
	camera_->SetRotate({ pitch, orbitAngle_, 0.0f });
	camera_->SetTranslate({
		orbitCenter_.x - std::sin(orbitAngle_) * orbitRadius_,
		waterHeight + cameraHeight_,
		orbitCenter_.z - std::cos(orbitAngle_) * orbitRadius_ });
	camera_->Update();

	if (water_) water_->SetRippleCenter(orbitCenter_);

	// ロゴ：周回中心の真上に固定し、常にカメラの方を向ける
	const float bob = std::sin(elapsed_ * logoBobSpeed_) * logoBobAmplitude_;
	const float sway = std::sin(elapsed_ * logoSwaySpeed_) * logoSwayAmplitude_;
	const Vector3 rot{ logoBaseRotate_.x + pitch, logoBaseRotate_.y + orbitAngle_ + sway, logoBaseRotate_.z };
	const float t = (introDuration_ > 0.0f) ? std::clamp(elapsed_ / introDuration_, 0.0f, 1.0f) : 1.0f;
	const float s = logoScale_ * EaseOutBack(t);

	// メッシュの中心（logoPivot_）が狙った位置に来るよう、回転・拡大後の中心ずれを差し引く。
	// Object3D と同じ MakeAffineMatrix（Rx·Ry·Rz）で変換するので回転順のずれは出ない。
	Transform pivotXf;
	pivotXf.scale = { s, s, s };
	pivotXf.rotate = rot;
	pivotXf.translate = { 0.0f, 0.0f, 0.0f };
	const Vector3 pivotWorld = TransformCoordinate(logoPivot_, MakeAffineMatrix(pivotXf));

	logo_->SetTranslate({
		orbitCenter_.x - pivotWorld.x,
		waterHeight + logoHeight_ + bob - pivotWorld.y,
		orbitCenter_.z - pivotWorld.z });
	logo_->SetRotate(rot);
	logo_->SetScale({ s, s, s });
}

void TitleScene::UpdateDoor() {
	if (!doorFrame_) return;
	const float waterHeight = water_ ? water_->GetParams().waterHeight : kWaterHeight;
	const float depth = water_ ? water_->GetParams().depth : kWaterDepth;

	// 台座の底を水底の床に合わせる（台座の上面が水面から少し出る）
	const Vector3 base{ orbitCenter_.x, waterHeight - depth, orbitCenter_.z };
	Transform frameXf;
	frameXf.scale = { 1.0f, 1.0f, 1.0f };
	frameXf.rotate = { 0.0f, doorYaw_, 0.0f };
	frameXf.translate = base;
	const Matrix4x4 frameMatrix = MakeAffineMatrix(frameXf);

	doorFrame_->SetTranslate(base);
	doorFrame_->SetRotate(frameXf.rotate);

	// 扉板は蝶番の軸まわりに回す。奥（-Z）へ開くので左右で符号が逆
	const float open = doorOpenDegrees_ * (3.1415927f / 180.0f);
	doorLeafL_->SetTranslate(TransformCoordinate(kDoorHingeL, frameMatrix));
	doorLeafL_->SetRotate({ 0.0f, doorYaw_ - open, 0.0f });
	doorLeafR_->SetTranslate(TransformCoordinate(kDoorHingeR, frameMatrix));
	doorLeafR_->SetRotate({ 0.0f, doorYaw_ + open, 0.0f });

	// マテリアルは GPU 側の準備ができてから作られる（遅延ロード）ので、初期化時ではなく毎フレーム上書きする
	ApplyDoorMaterials();
}

void TitleScene::ApplyDoorMaterials() {
	// 部位ごとの .mat 名（door_*_Marble.mat / door_*_Gold.mat）で大理石と金を見分ける
	for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
		ModelInstance* model = part ? part->GetModelInstance() : nullptr;
		if (!model) continue;
		for (const RenderSubmesh& sm : model->GetSubmeshes()) {
			if (!sm.material) continue;
			if (sm.matFilePath.find("_Gold") != std::string::npos) {
				sm.material->environmentCoefficient = doorGold_.envCoefficient;
				sm.material->metallic = doorGold_.metallic;
				sm.material->roughness = doorGold_.roughness;
				sm.material->color = doorGold_.color;
				sm.material->cloudReflection = doorGold_.cloudReflection;
			} else {
				sm.material->environmentCoefficient = doorMarbleEnvCoefficient_;
			}
		}
	}
}

void TitleScene::SetLogoVisible(bool visible) {
	if (showLogo_ == visible) return;
	showLogo_ = visible;
	if (!waterReflection_ || !logo_) return;
	if (visible) {
		waterReflection_->AddTarget(logo_.get());
	} else {
		waterReflection_->RemoveTarget(logo_.get());
	}
}

void TitleScene::UpdateIntroPostEffect() {
	auto* pe = Game::GetPostEffect();
	if (!pe) return;
	if (pe->vignette) pe->vignette->SetIntensity(vignetteIntensity_);

	// 起動直後だけ強いラジアルブラーをかけ、ロゴ登場に合わせて収束させる
	if (pe->radialBlur && pe->radialBlur->IsEnabled()) {
		const float t = (introDuration_ > 0.0f) ? std::clamp(elapsed_ / introDuration_, 0.0f, 1.0f) : 1.0f;
		const float remain = 1.0f - t;
		pe->radialBlur->SetBlurWidth(introBlurWidth_ * remain * remain);
		if (t >= 1.0f) pe->radialBlur->SetEnabled(false);
	}
}

void TitleScene::Draw() {
	auto* commandList = dxCore_->GetCommandList();

	// 水面に映す物を反射 RT へ（RT を切り替えるので、シーン RT のバインドより前に行う）
	// 通常はゲームカメラ基準。デバッグカメラ中もゲーム画面での映り込みが水面に焼き付くので、
	// 回り込んで確認できる（Debug で切り替えればデバッグカメラ基準の正しい反射になる）
	if (waterReflection_) {
		bool fromDebugCamera = false;
#ifdef _DEBUG
		fromDebugCamera = GetUseDebugCamera() && reflectFromDebugCamera_;
#endif
		if (fromDebugCamera) {
			waterReflection_->Render(*camera_);
		} else {
			waterReflection_->Render(gameViewProjection_, gameEyePosition_);
		}
	}

	// Skybox を最初に描画（深度書き込みなしの ReadOnly DSV）
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	if (skybox_) skybox_->Draw(dxCore_);

	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	if (logo_ && showLogo_) logo_->Draw(dxCore_);
	if (showDoor_) {
		if (doorFrame_) doorFrame_->Draw(dxCore_);
		if (doorLeafL_) doorLeafL_->Draw(dxCore_);
		if (doorLeafR_) doorLeafR_->Draw(dxCore_);
	}

	// 水面は不透明物の後（ロゴとの前後は深度で決まる）
	if (water_) water_->Draw(*camera_, kTitleSkyboxPath, waterReflection_.get());

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
	const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());

	if (elapsed_ >= introDuration_) {
		if (menuOpen_) {
			menu_.Draw({ screenW * 0.5f, screenH * 0.72f });
		} else {
			// ゆっくり明滅させて「押せる」ことを伝える
			const float blink = 0.35f + 0.65f * (0.5f + 0.5f * std::sin((elapsed_ - introDuration_) * 3.0f));
			const char* label = "PRESS ANY BUTTON";
			const float scale = 1.2f;
			const float w = tr->MeasureWidth(label, scale);
			tr->DrawText(label, { (screenW - w) * 0.5f, screenH * 0.72f }, scale,
				{ 1.0f, 1.0f, 1.0f, blink }, 2.0f, { 0.0f, 0.0f, 0.0f, blink });
		}
	}
	tr->Flush();
}

void TitleScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Begin("Title Tuning")) {
		ImGui::DragFloat3("Orbit Center", &orbitCenter_.x, 0.1f);
		ImGui::DragFloat("Orbit Speed", &orbitSpeed_, 0.005f, -1.0f, 1.0f);
		ImGui::DragFloat("Orbit Angle", &orbitAngle_, 0.01f);
		ImGui::DragFloat("Orbit Radius", &orbitRadius_, 0.1f, 1.0f, 100.0f);
		ImGui::DragFloat("Camera Height", &cameraHeight_, 0.05f, 0.05f, 50.0f);
		ImGui::DragFloat("Aim Height", &aimHeight_, 0.05f, -10.0f, 20.0f);
		ImGui::Separator();
		ImGui::DragFloat("Logo Height", &logoHeight_, 0.05f, -10.0f, 10.0f);
		ImGui::DragFloat("Logo Scale", &logoScale_, 0.05f, 0.1f, 20.0f);
		ImGui::DragFloat3("Logo Base Rotate", &logoBaseRotate_.x, 0.01f);
		ImGui::DragFloat3("Logo Pivot (model)", &logoPivot_.x, 0.005f);
		ImGui::DragFloat("Bob Amplitude", &logoBobAmplitude_, 0.01f, 0.0f, 2.0f);
		ImGui::DragFloat("Bob Speed", &logoBobSpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::DragFloat("Sway Amplitude", &logoSwayAmplitude_, 0.01f, 0.0f, 1.0f);
		ImGui::DragFloat("Sway Speed", &logoSwaySpeed_, 0.05f, 0.0f, 10.0f);
		ImGui::Separator();
		ImGui::DragFloat("Intro Duration", &introDuration_, 0.05f, 0.0f, 5.0f);
		ImGui::DragFloat("Intro Blur Width", &introBlurWidth_, 0.005f, 0.0f, 0.5f);
		ImGui::DragFloat("Vignette", &vignetteIntensity_, 0.01f, 0.0f, 2.0f);
		if (ImGui::Button("Replay Intro")) {
			elapsed_ = 0.0f;
			menuOpen_ = false;
			if (auto* pe = Game::GetPostEffect(); pe && pe->radialBlur) pe->radialBlur->SetEnabled(true);
		}
		if (ImGui::CollapsingHeader("Door", ImGuiTreeNodeFlags_DefaultOpen)) {
			bool showLogo = showLogo_;
			if (ImGui::Checkbox("Logo Visible", &showLogo)) SetLogoVisible(showLogo);
			if (ImGui::Checkbox("Door Visible", &showDoor_) && waterReflection_) {
				for (Object3DInstance* part : { doorFrame_.get(), doorLeafL_.get(), doorLeafR_.get() }) {
					if (showDoor_) {
						waterReflection_->AddTarget(part);
					} else {
						waterReflection_->RemoveTarget(part);
					}
				}
			}
			ImGui::SliderAngle("Door Yaw", &doorYaw_, -180.0f, 180.0f);
			ImGui::SliderFloat("Door Open (deg)", &doorOpenDegrees_, 0.0f, 90.0f);
			if (ImGui::SliderFloat("Sun Intensity", &sunIntensity_, 0.0f, 8.0f)) {
				if (auto* lm = LightManager::GetInstance()) lm->SetDirectionalLightIntensity(sunIntensity_);
			}
			// マテリアルは UpdateDoor で毎フレーム反映される
			ImGui::SliderFloat("Marble Env (IBL)", &doorMarbleEnvCoefficient_, 0.0f, 1.5f);
			ImGui::SliderFloat("Gold Env (IBL)", &doorGold_.envCoefficient, 0.0f, 1.5f);
			ImGui::SliderFloat("Gold Metallic", &doorGold_.metallic, 0.0f, 1.0f);
			ImGui::SliderFloat("Gold Roughness", &doorGold_.roughness, 0.04f, 1.0f);
			ImGui::ColorEdit3("Gold Color", &doorGold_.color.x);
			ImGui::SliderFloat("Gold Cloud Reflect", &doorGold_.cloudReflection, 0.0f, 3.0f);
		}
		if (ImGui::CollapsingHeader("Water", ImGuiTreeNodeFlags_DefaultOpen)) {
			if (GetUseDebugCamera()) {
				int source = reflectFromDebugCamera_ ? 1 : 0;
				const char* sources[] = { "Game Camera", "Debug Camera" };
				if (ImGui::Combo("Reflection Source", &source, sources, IM_ARRAYSIZE(sources))) {
					reflectFromDebugCamera_ = (source == 1);
				}
			}
			if (water_) water_->OnImGui();
			if (waterReflection_) waterReflection_->OnImGui();
		}
		if (ImGui::CollapsingHeader("Cloud")) {
			if (cloudLayer_) cloudLayer_->OnImGui();
		}
	}
	ImGui::End();
#endif
}

Camera* TitleScene::GetCamera() {
	return camera_.get();
}

void TitleScene::DrawShadowCasters() {
	GameScene::DrawShadowCasters();
	if (logo_ && showLogo_) logo_->DrawShadowPass(dxCore_);
	if (showDoor_) {
		if (doorFrame_) doorFrame_->DrawShadowPass(dxCore_);
		if (doorLeafL_) doorLeafL_->DrawShadowPass(dxCore_);
		if (doorLeafR_) doorLeafR_->DrawShadowPass(dxCore_);
	}
}
