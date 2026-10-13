#include "HubScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Skybox.h"
#include "TextureManager.h"
#include "LightManager.h"
#include "MathUtility.h"
#include "Hub/HubMechModel.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "PostEffect.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "Vector4.h"
#include "AttractMode.h"
#include "UIPointer.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "FadeTransition.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <filesystem>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	enum HubItem : int {
		kHubSortie = 0,   // 出撃（必殺技の切り替え・タイトルに戻るは後で足す。17 番 §0）
	};

	constexpr const char* kTuningPath = "Resources/Json/Tuning/Hub.json";
	// メニューは画面の下の方（機体の胸を隠さない）
	constexpr float kMenuHeightRatio = 0.78f;

	// 神殿のモデル（17 番 H1）ができるまでの仮の背景と映り込み（タイトルの空を借りる）
	constexpr const char* kSkyboxPath = "Resources/Cubemaps/title_clear_sky.dds";
	constexpr const char* kIblPath = "Resources/Cubemaps/title_clear_sky_ibl.dds";
	constexpr float kCameraFarClip = 500.0f;

	// 機体の正面（+Z）を前上から照らす（光の進む向き）
	const Vector3 kSunLightDirection = { -0.191f, -0.815f, -0.547f };
	const Vector4 kSunLightColor = { 1.0f, 0.96f, 0.9f, 1.0f };

	// 仮の床：石畳の板（tools/Python/gen_flagstone_floor.py。30m 四方・中心が原点）
	constexpr const char* kFloorModelDir = "Resources/Models/FlagstoneFloor";
	constexpr const char* kFloorMesh = "flagstone_floor.mesh";
	constexpr float kFloorMeshSize = 30.0f;
	// 回廊は手のひらと同じ高さ。機体側の縁は z = 7m（指先との間を一歩で渡る）
	constexpr float kGalleryHeight = 10.6f;
	constexpr float kGalleryFrontZ = 7.0f;
	constexpr float kGalleryWidth = 16.0f;
	constexpr float kGalleryDepth = 14.0f;
}

HubScene::HubScene() = default;
HubScene::~HubScene() = default;

void HubScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetFarClip(kCameraFarClip);
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, kSkyboxPath);
	// PBR の IBL。無いと金属は真っ黒になる
	TextureManager::GetInstance()->LoadTexture(kIblPath);
	object3DManager_->SetEnvironmentTexture(kIblPath);

	mech_ = std::make_unique<HubMechModel>();
	mech_->Initialize(object3DManager_, dxCore_);

	groundFloor_ = std::make_unique<Object3DInstance>();
	groundFloor_->Initialize(object3DManager_, dxCore_, kFloorModelDir, kFloorMesh, "HubGroundFloor");
	groundFloor_->SetScale({ 2.0f, 1.0f, 2.0f });
	groundFloor_->Update();
	galleryFloor_ = std::make_unique<Object3DInstance>();
	galleryFloor_->Initialize(object3DManager_, dxCore_, kFloorModelDir, kFloorMesh, "HubGalleryFloor");
	galleryFloor_->SetScale({ kGalleryWidth / kFloorMeshSize, 1.0f, kGalleryDepth / kFloorMeshSize });
	galleryFloor_->SetTranslate({ 0.0f, kGalleryHeight, kGalleryFrontZ + kGalleryDepth * 0.5f });
	galleryFloor_->Update();

	LoadTuning();

	menu_.SetItems({ "出撃" });

	// 閉じ切りで真っ暗にする黒い板
	blackout_ = std::make_unique<SpriteInstance>();
	blackout_->Initialize(spriteManager_, "Resources/Textures/white1x1.dds", "HubBlackout");
	blackout_->SetPosition({ 0.0f, 0.0f });
	blackout_->SetSize({ static_cast<float>(dxCore_->GetSwapChainWidth()),
		static_cast<float>(dxCore_->GetSwapChainHeight()) });

	boarding_.Stop();
	ApplyBoardingPose();
	camera_->Update();
	skybox_->Update(0.0f);

	SoundManager::GetInstance()->Play2DSoundLooped("bgm_title", 0.5f);
	attractTimer_ = 0.0f;
}

void HubScene::Finalize() {
	SoundManager::GetInstance()->Stop2DSound("bgm_title");
}

void HubScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	const float dt = GetScaledDeltaTime();

	// 乗り込み中は入力を読まない（ChangeScene を呼ぶまでは遷移中ではないので、自分で止める）
	if (boarding_.IsPlaying()) {
		boarding_.Update(dt);
		if (boarding_.ConsumeSitImpact()) {
			camera_->Shake(boarding_.GetSitShakeIntensity(), boarding_.GetSitShakeSeconds());
		}
		if (boarding_.ConsumeCloseImpact()) {
			camera_->Shake(boarding_.GetCloseShakeIntensity(), boarding_.GetCloseShakeSeconds());
		}
		ApplyBoardingPose();
		camera_->UpdateShake(dt);
		camera_->Update();
		skybox_->Update(dt);
		if (boarding_.IsFinished()) {
			// 真っ暗のまま切り替える（組み合わせ表の覆う側は kCoverByScene）
			SceneManager::GetInstance()->ChangeScene("STAGESELECT");
		}
		return;
	}

	ApplyBoardingPose();
	camera_->UpdateShake(dt);
	camera_->Update();
	skybox_->Update(dt);

	auto* actions = input_->GetActionMap();
	if (!actions) return;

	// デモモード：一定時間 HUB を見せてからタイトルへ戻る（メニューに「タイトルに戻る」が無いのでキャンセル扱い）
	if (auto* attract = AttractMode::GetInstance(); attract->IsEnabled()) {
		attractTimer_ += dt;
		if (attractTimer_ >= attract->hubWait + attract->hubConfirmDelay) {
			SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
			ChangeToTitle();
			return;
		}
	} else {
		attractTimer_ = 0.0f;
	}

	menu_.SetPosition({ static_cast<float>(dxCore_->GetSwapChainWidth()) * 0.5f,
		static_cast<float>(dxCore_->GetSwapChainHeight()) * kMenuHeightRatio });
	const UIPointer pointer = UIPointer::FromInput(input_);
	const VerticalMenu::Result result = menu_.Update(actions, &pointer);
	if (result == VerticalMenu::Result::Canceled) {
		ChangeToTitle();
		return;
	}
	if (result == VerticalMenu::Result::Confirmed) {
		switch (menu_.GetSelectedIndex()) {
		case kHubSortie:
			// 乗り込み（18 番 ①の OUT）を演じてから STAGESELECT へ
			boarding_.Start();
			return;
		}
	}
}

void HubScene::Draw() {
	auto* commandList = dxCore_->GetCommandList();

	// 空を最初に描く（深度書き込みなしの ReadOnly DSV）
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);
	skyboxManager_->DrawSetting();
	if (skybox_) skybox_->Draw(dxCore_);

	auto normalDsv = dxCore_->GetDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(commandList);
	if (groundFloor_) groundFloor_->Draw(dxCore_);
	if (galleryFloor_) galleryFloor_->Draw(dxCore_);
	if (mech_) mech_->Draw(dxCore_);

	const HubBoardingSequence::Pose pose = boarding_.GetCurrentPose();
	if (blackout_ && pose.blackout > 0.0f) {
		blackout_->SetColor({ 0.0f, 0.0f, 0.0f, pose.blackout });
		blackout_->Update();
		spriteManager_->DrawSetting();
		blackout_->Draw();
	}

	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	// メニューは待機中だけ（決定したら消して乗り込みを見せる）
	if (!boarding_.IsPlaying() && !boarding_.IsPreviewing()) {
		const float screenW = static_cast<float>(dxCore_->GetSwapChainWidth());
		const float screenH = static_cast<float>(dxCore_->GetSwapChainHeight());
		menu_.SetPosition({ screenW * 0.5f, screenH * kMenuHeightRatio });
		menu_.Draw();
	}
	tr->Flush();
}

Camera* HubScene::GetCamera() {
	return camera_.get();
}

void HubScene::ApplyBoardingPose() {
	const HubBoardingSequence::Pose pose = boarding_.GetCurrentPose();
	const Vector3& origin = mech_->GetPosition();
	camera_->SetFovY(DegToRad(fovYDegrees_));
	camera_->SetTranslate({ origin.x + pose.eye.x, origin.y + pose.eye.y, origin.z + pose.eye.z });
	camera_->SetRotate({ pose.pitch, pose.yaw, 0.0f });

	mech_->SetHatchOpenRatio(pose.hatchOpenRatio);
	mech_->Update();

	// 平行光源の既定 intensity は 0 なので毎フレーム決める。ハッチが閉じるにつれて弱める
	if (auto* lm = LightManager::GetInstance(); lm && lm->GetDirectionalLightData()) {
		lm->SetDirectionalLightDirection(kSunLightDirection);
		lm->SetDirectionalLightColor(kSunLightColor);
		lm->SetDirectionalLightIntensity(sunIntensity_ * pose.lightScale);
	}
}

void HubScene::ChangeToTitle() {
	// タイトル ↔ HUB は白フェード（18 番 ⑧）。Fade の既定は黒なので、色と長さを直前に決める
	if (auto* fade = TransitionManager::GetInstance()->GetTransition<FadeTransition>(TransitionType::Fade)) {
		fade->SetNextFade({ 1.0f, 1.0f, 1.0f }, whiteFadeSeconds_, whiteHoldSeconds_);
	}
	SceneManager::GetInstance()->ChangeScene("TITLE");
}

void HubScene::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	const JsonValue& root = result.value;
	whiteFadeSeconds_ = static_cast<float>(root["whiteFadeSeconds"].AsDouble(whiteFadeSeconds_));
	whiteHoldSeconds_ = static_cast<float>(root["whiteHoldSeconds"].AsDouble(whiteHoldSeconds_));
	fovYDegrees_ = static_cast<float>(root["fovYDegrees"].AsDouble(fovYDegrees_));
	sunIntensity_ = static_cast<float>(root["sunIntensity"].AsDouble(sunIntensity_));
	if (mech_) mech_->LoadTuning(root["mech"]);
	boarding_.LoadTuning(root["boarding"]);
}

void HubScene::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	root["whiteFadeSeconds"] = JsonValue(static_cast<double>(whiteFadeSeconds_));
	root["whiteHoldSeconds"] = JsonValue(static_cast<double>(whiteHoldSeconds_));
	root["fovYDegrees"] = JsonValue(static_cast<double>(fovYDegrees_));
	root["sunIntensity"] = JsonValue(static_cast<double>(sunIntensity_));
	if (mech_) mech_->SaveTuning(root["mech"]);
	boarding_.SaveTuning(root["boarding"]);
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void HubScene::OnImGuiTuning() {
#ifdef _DEBUG
	if (ImGui::Button("Save Tuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload Tuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
	ImGui::SeparatorText("To Title (white fade)");
	ImGui::DragFloat("White Fade (s)", &whiteFadeSeconds_, 0.05f, 0.05f, 5.0f);
	ImGui::DragFloat("White Hold (s)", &whiteHoldSeconds_, 0.05f, 0.0f, 5.0f);

	ImGui::SeparatorText("View");
	ImGui::DragFloat("FovY (deg)", &fovYDegrees_, 0.5f, 20.0f, 110.0f);
	ImGui::DragFloat("Sun Intensity", &sunIntensity_, 0.01f, 0.0f, 5.0f);

	if (mech_ && ImGui::CollapsingHeader("Mech")) {
		mech_->OnImGui();
	}
	if (ImGui::CollapsingHeader("Boarding (18 OUT-1)", ImGuiTreeNodeFlags_DefaultOpen)) {
		// 最後まで再生すると STAGESELECT へ切り替わる（本番と同じ）
		if (ImGui::Button("Play")) boarding_.Start();
		ImGui::SameLine();
		if (ImGui::Button("Stop")) boarding_.Stop();
		boarding_.OnImGui();
	}
#endif // _DEBUG
}
