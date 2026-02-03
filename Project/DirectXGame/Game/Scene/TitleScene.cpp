#include "TitleScene.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Debug.h"
#include"StripeTransition.h"
#include"Game.h"

//============================
// 自作ヘッダーのインクルード
//============================
#include "DebugCamera.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Camera.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "SoundManager.h"
#include "ControllerInput.h"
#include "MouseInput.h"
#include "LightManager.h"
#include "GameData.h"

TitleScene::~TitleScene() {
}

void TitleScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	// 前のシーンからどのトランジションで来たか確認
	TransitionType lastType = SceneManager::GetInstance()->GetLastUsedTransitionType();
	std::string lastName = SceneManager::GetInstance()->GetLastUsedTransitionName();

	if (lastType != TransitionType::None) {
		Debug::Log("Arrived via transition: " + lastName);
	}

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 10.0f, -20.0f });
	camera_->SetRotate({ 0.3f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("qr", "Resources/qrcord.png");

	title_ = std::make_unique<Object3DInstance>();
	title_->Initialize(object3DManager_, dxCore_, "Resources", "title.obj", "title");
	title_->SetTranslate({ 0.0f, 6.0f, 0.0f });
	title_->SetRotate({ 3.14f/2.0f,3.14f,0.0f });

	space_ = std::make_unique<Object3DInstance>();
	space_->Initialize(object3DManager_, dxCore_, "Resources", "space.obj", "space");
	space_->SetTranslate({ 0.0f, 3.0f, 0.0f });
	space_->SetRotate({ 3.14f / 2.0f,3.14f,0.0f });

	// スポットライトの設定
	auto* lightMgr = LightManager::GetInstance();
	lightMgr->SetSpotLightCount(1);

	// title用のスポットライト
	lightMgr->SetSpotLightPosition(0, { 0.0f, 10.0f, 0.0f });
	lightMgr->SetSpotLightDirection(0, Normalize({ 0.0f, -1.0f, 0.0f }));
	lightMgr->SetSpotLightColor(0, { 1.0f, 1.0f, 1.0f, 1.0f });
	lightMgr->SetSpotLightIntensity(0, 7.0f);
	lightMgr->SetSpotLightDistance(0, 15.0f);
	lightMgr->SetSpotLightDecay(0, 2.0f);
	lightMgr->SetSpotLightCosAngle(0, std::cos(std::numbers::pi_v<float> / 6.0f));       // 30°
	lightMgr->SetSpotLightCosFalloffStart(0, std::cos(std::numbers::pi_v<float> / 8.0f)); // 22.5

	// DirectionalLight の設定（弱め）
	lightMgr->SetDirectionalLightColor({ 1.0f, 1.0f, 1.0f, 1.0f });
	lightMgr->SetDirectionalLightDirection(Normalize({ 0.5f, -1.0f, 0.5f }));
	lightMgr->SetDirectionalLightIntensity(0.3f);  // 弱めの強度

	std::random_device rd;
	randomEngine_ = std::mt19937(rd());
}

void TitleScene::Finalize() {
	// このシーンで作ったパーティクルグループを削除
	ParticleManager::GetInstance()->RemoveParticleGroup("qr");
}

void TitleScene::Update() {
	// トランジション中は入力を受け付けない
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	// SPACE: フェードトランジションでゲームシーンへ
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		SceneManager::GetInstance()->ChangeScene("CHARACTERSELECT", TransitionType::Fade);
		return;
	}

	// title_ を Y 軸でゆっくり回転
	Vector3 titleRotate = title_->GetRotate();
	titleRotate.y += 0.01f;  // 回転速度（調整可能）
	title_->SetRotate(titleRotate);

	// QR パーティクルをまばらに発生させる
	particleTimer_++;
	if (particleTimer_ >= 10) {  // 10フレームごとに1個発生（頻度調整可能）
		particleTimer_ = 0;

		// 広い範囲でランダムな位置を生成
		std::uniform_real_distribution<float> distX(-15.0f, 15.0f);  // X方向の範囲
		std::uniform_real_distribution<float> distY(0.0f, 12.0f);    // Y方向の範囲
		std::uniform_real_distribution<float> distZ(-15.0f, 15.0f);  // Z方向の範囲

		Vector3 randomPos = {
			distX(randomEngine_),
			distY(randomEngine_),
			distZ(randomEngine_)
		};

		ParticleManager::GetInstance()->Emit("qr", randomPos, 1);
	}

	camera_->Update();
	title_->Update();
	space_->Update();

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update();

	

}

void TitleScene::Draw() {
	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());

	title_->Draw(dxCore_);
	space_->Draw(dxCore_);

	// パーティクル描画
	ParticleManager::GetInstance()->Draw();
}