#include "GameScene.h"
#include "SceneManager.h"

//============================
// 自作ヘッダーのインクルード
//============================
#include "DebugCamera.h"
#include "WindowsApplication.h"
#include "DirectXCore.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "Object3DManager.h"
#include "Object3DInstance.h"
#include "Camera.h"
#include "SRVManager.h"
#include "ParticleManager.h"
#include "SoundManager.h"
#include "ControllerInput.h"
#include "MouseInput.h"
#include "InputManager.h"
#include "Debug.h"
#include "LightManager.h"
#include "KeyboardInput.h"
#include "CameraCapture.h"
#include "QRCodeReader.h"
#include "GameData.h"

GameScene::GameScene() {
}

GameScene::~GameScene() {
}

void GameScene::Initialize() {
	// デバッグカメラの生成
	debugCamera_ = std::make_unique<DebugCamera>();

	// スプライトの初期化
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager_, "Resources/uvChecker.png");

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 15.0f, -20.0f });
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("uvChecker", "Resources/uvChecker.png");
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

	// 交互に使うスプライト
	const std::string textures[2] = {
		"Resources/uvChecker.png",
		"Resources/monsterBall.png"
	};

	// 5枚生成
	for (uint32_t i = 0; i < 5; ++i) {
		auto newSprite = std::make_unique<SpriteInstance>();
		const std::string& texturePath = textures[i % 2];

		std::string spriteName = "Sprite_" + std::to_string(i);
		newSprite->Initialize(spriteManager_, texturePath, spriteName);

		newSprite->SetSize({ 100.0f, 100.0f });
		newSprite->SetPosition({ i * 2.0f, 0.0f });
		sprites_.push_back(std::move(newSprite));
	}

	// 選択されたモデルを取得
	std::string modelName = GameData::GetInstance()->GetSelectedModel();

	// デバッグ出力で中身を確認
	OutputDebugStringA("Selected model: [");
	OutputDebugStringA(modelName.c_str());
	OutputDebugStringA("]\n");
	OutputDebugStringA(("Length: " + std::to_string(modelName.size()) + "\n").c_str());

	// 3Dオブジェクトを配列で管理
	const std::string modelFiles[] = { modelName, "enemy.obj"};
	const std::string objectNames[] = { "Player", "Enemy"};

	for (int i = 0; i < std::size(modelFiles); ++i) {
		auto obj = std::make_unique<Object3DInstance>();
		obj->Initialize(
			object3DManager_,
			dxCore_,
			"Resources",
			modelFiles[i],
			objectNames[i]
		);
		obj->SetTranslate({ 0.0f, 0.0f, 0.0f });
		object3DInstances_.push_back(std::move(obj));
	}

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");
}

void GameScene::Finalize() {

	sprites_.clear();
	object3DInstances_.clear();

}

void GameScene::Update() {

	if (input_->GetKeyboard()->PuhsKey(DIK_W)) {
		// Wキーを押している間
	}

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	for (const auto& obj : object3DInstances_) {
		obj->Update();
	}

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update();

	// キー入力でパーティクル発生
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
	}

	sprite_->Update();

	for (const auto& s : sprites_) {
		s->Update();
	}

}

void GameScene::Draw() {

	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
	LightManager::GetInstance()->OnImGui();

	// 3Dオブジェクト描画
	for (const auto& obj : object3DInstances_) {
		obj->Draw(dxCore_);
	}

	// パーティクル描画
	ParticleManager::GetInstance()->Draw();

	// スプライト描画
	spriteManager_->DrawSetting();
	sprite_->Draw();
	for (const auto& s : sprites_) {
		s->Draw();
	}
	
	// パーティクルのImGui表示
	ParticleManager::GetInstance()->OnImGui();

	// カメラのImGui
	camera_->OnImGui();
}