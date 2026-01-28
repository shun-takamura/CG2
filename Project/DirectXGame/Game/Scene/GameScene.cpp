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
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	camera_->SetTranslate({ 0.0f, 2.0f, -30.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("uvChecker", "Resources/uvChecker.png");
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

	// 加速度フィールドの設定
	AccelerationField field;
	field.acceleration = { 15.0f, 0.0f, 0.0f };
	field.area.min = { -1.0f, -1.0f, -1.0f };
	field.area.max = { 10.0f, 10.0f, 10.0f };
	ParticleManager::GetInstance()->SetAccelerationField(field);
	ParticleManager::GetInstance()->SetAccelerationFieldEnabled(true);

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

	// 3Dオブジェクトを配列で管理
	const std::string modelFiles[] = { "monsterBall.obj", "terrain.obj", "plane.gltf" };
	const std::string objectNames[] = { "MonsterBall", "terrain", "plane" };

	for (int i = 0; i < 3; ++i) {
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

	// カメラスプライトの初期化（カメラが開かれてテクスチャが作成されたら）
	if (cameraSprite_ == nullptr && CameraCapture::GetInstance()->IsOpened()) {
		if (TextureManager::GetInstance()->HasTexture(CameraCapture::GetTextureName())) {
			cameraSprite_ = std::make_unique<SpriteInstance>();
			cameraSprite_->Initialize(spriteManager_, CameraCapture::GetTextureName(), "CameraPreview");
			cameraSprite_->SetPosition({ 0.0f, 0.0f });
			cameraSprite_->SetSize({ 640.0f, 480.0f });
			cameraSprite_->SetTextureLeftTop({ 0.0f, 0.0f });
			cameraSprite_->SetTextureSize({
				static_cast<float>(CameraCapture::GetInstance()->GetFrameWidth()),
				static_cast<float>(CameraCapture::GetInstance()->GetFrameHeight())
				});
		}
	}

	// カメラが閉じられたらスプライトを削除
	if (cameraSprite_ != nullptr && !CameraCapture::GetInstance()->IsOpened()) {
		cameraSprite_.reset();
		QRCodeReader::GetInstance()->Reset();
	}

	// カメラスプライトが存在する場合のみ更新
	if (cameraSprite_) {
		cameraSprite_->Update();

		// QRコード読み取り
		const auto& frameData = CameraCapture::GetInstance()->GetFrameData();
		if (!frameData.empty()) {
			QRCodeReader::GetInstance()->Decode(
				frameData.data(),
				CameraCapture::GetInstance()->GetFrameWidth(),
				CameraCapture::GetInstance()->GetFrameHeight()
			);
		}
	}

	// --- キーボード入力 ---
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		// スペースキーを押した瞬間
	}

	if (input_->GetKeyboard()->PuhsKey(DIK_W)) {
		// Wキーを押している間
	}

	// マウス入力
	MouseInput* mouse = input_->GetMouse();

	if (mouse->IsButtonTriggered(MouseInput::Button::Left)) {
		// 左クリックした瞬間
	}

	// --- コントローラー入力 ---
	ControllerInput* controller = input_->GetController();

	if (controller->IsConnected()) {
		if (controller->IsButtonTriggered(XINPUT_GAMEPAD_A)) {
			// Aボタンを押した瞬間
		}

		const auto& leftStick = controller->GetLeftStick();
		float moveX = leftStick.x * leftStick.magnitude;
		float moveY = leftStick.y * leftStick.magnitude;

		if (controller->IsButtonTriggered(XINPUT_GAMEPAD_Y)) {
			controller->SetVibration(32000, 32000);
		}
	}

	// キーボードの入力処理
	if (input_->GetKeyboard()->TriggerKey(DIK_0)) {
		Debug::Log("trigger [0]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_1)) {
		SoundManager::GetInstance()->Play("fanfare");
		Debug::Log("trigger [1]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_2)) {
		SoundManager::GetInstance()->Stop("fanfare");
		Debug::Log("trigger [2]");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_P)) {
		dxCore_->SetUseFixedFrameRate(false);
	}

	// Fキーで場のON/OFF切り替え
	if (input_->GetKeyboard()->TriggerKey(DIK_F)) {
		bool current = ParticleManager::GetInstance()->IsAccelerationFieldEnabled();
		ParticleManager::GetInstance()->SetAccelerationFieldEnabled(!current);
	}

	sprite_->SetAnchorPoint({ 0.5f, 0.5f });
	sprite_->SetIsFlipX(true);

	// 回転テスト
	float rotation = sprite_->GetRotation();
	rotation += 0.01f;
	sprite_->SetRotation(rotation);

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	for (const auto& obj : object3DInstances_) {
		obj->Update();
	}

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update();

	// 1秒間に発生させる量を自動制御
	emitTimer_ += dxCore_->GetDeltaTime();
	float emitRate = ParticleManager::GetInstance()->GetEmitterSettings().emitRate;
	if (emitRate > 0.0f) {
		float emitInterval = 1.0f / emitRate;
		while (emitTimer_ >= emitInterval) {
			ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);
			emitTimer_ -= emitInterval;
		}
	}

	// circleのパーティクルを常に発生させる
	ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);

	// キー入力でパーティクル発生
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
	}

	sprite_->Update();

	// カメラスプライトが存在する場合のみ更新
	if (cameraSprite_) {
		cameraSprite_->Update();
	}

	for (const auto& s : sprites_) {
		s->Update();
	}

	debugCamera_->Update(input_->GetKeyboard()->keys_);
}

void GameScene::Draw() {

	// 描画の最初にカメラテクスチャを更新
	if (CameraCapture::GetInstance()->IsOpened()) {
		OutputDebugStringA("Updating camera texture...\n");
		CameraCapture::GetInstance()->UpdateTexture();
	}

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
	
    // カメラスプライトが存在する場合のみ描画
	if (cameraSprite_) {
		cameraSprite_->Draw();
	}

	// パーティクルのImGui表示
	ParticleManager::GetInstance()->OnImGui();
}