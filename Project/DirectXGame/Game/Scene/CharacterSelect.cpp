#include "CharacterSelect.h"
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

CharacterSelect::CharacterSelect()
{
}

CharacterSelect::~CharacterSelect()
{
}

void CharacterSelect::Initialize()
{
	// スプライトの初期化
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager_, "Resources/CharacterSelect_UI_QR.png");
	sprite_->SetAnchorPoint({ 0.5f,0.0f });
	sprite_->SetPosition({ 640.0f, 64.0f });
	sprite_->SetSize({ 640.0f, 360.0f });

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 15.0f, -20.0f });
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/Particle.png");

	// 3Dオブジェクトを配列で管理
	const std::string modelFiles[] = { "player1.obj", "player2.obj"};
	const std::string objectNames[] = { "Player1", "Player2"};

	for (int i = 0;  i < std::size(modelFiles); ++i) {
		auto obj = std::make_unique<Object3DInstance>();
		obj->Initialize(
			object3DManager_,
			dxCore_,
			"Resources",
			modelFiles[i],
			objectNames[i]
		);
		obj->SetTranslate({ -2.0f+(i*4.0f), 0.0f, 0.0f});
		obj->SetRotate({ 0.0f,3.14f,0.0f });
		object3DInstances_.push_back(std::move(obj));
	}

	// スポットライトの設定（プレイヤーモデルを上から照らす）
	auto* lightMgr = LightManager::GetInstance();
	lightMgr->SetSpotLightCount(2);

	// Player1用スポットライト（x = -2.0f の真上）
	lightMgr->SetSpotLightPosition(0, { -2.0f, 10.0f, -4.0f });
	lightMgr->SetSpotLightDirection(0, Normalize({ 0.0f, -1.0f, 0.0f }));
	lightMgr->SetSpotLightColor(0, { 1.0f, 1.0f, 1.0f, 1.0f });
	lightMgr->SetSpotLightIntensity(0, 4.0f);
	lightMgr->SetSpotLightDistance(0, 15.0f);
	lightMgr->SetSpotLightDecay(0, 2.0f);
	lightMgr->SetSpotLightCosAngle(0, std::cos(std::numbers::pi_v<float> / 6.0f));       // 30°
	lightMgr->SetSpotLightCosFalloffStart(0, std::cos(std::numbers::pi_v<float> / 8.0f)); // 22.5°

	// Player2用スポットライト（x = 2.0f の真上）
	lightMgr->SetSpotLightPosition(1, { 2.0f, 10.0f, -4.0f });
	lightMgr->SetSpotLightDirection(1, Normalize({ 0.0f, -1.0f, 0.0f }));
	lightMgr->SetSpotLightColor(1, { 1.0f, 1.0f, 1.0f, 1.0f });
	lightMgr->SetSpotLightIntensity(1, 4.0f);
	lightMgr->SetSpotLightDistance(1, 15.0f);
	lightMgr->SetSpotLightDecay(1, 2.0f);
	lightMgr->SetSpotLightCosAngle(1, std::cos(std::numbers::pi_v<float> / 6.0f));
	lightMgr->SetSpotLightCosFalloffStart(1, std::cos(std::numbers::pi_v<float> / 8.0f));

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");

	// デバイスカメラをオープン
	CameraCapture::GetInstance()->EnumerateDevices();
	const auto& devices = CameraCapture::GetInstance()->GetDevices();
	if (!devices.empty()) {
		CameraCapture::GetInstance()->OpenCamera(0); // 最初のカメラを使用
	}

}

void CharacterSelect::Finalize()
{
	// カメラを閉じる
	if (CameraCapture::GetInstance()->IsOpened()) {
		CameraCapture::GetInstance()->CloseCamera();
	}

	// このシーンで作ったパーティクルグループを削除
	ParticleManager::GetInstance()->RemoveParticleGroup("circle");

	sprites_.clear();
	object3DInstances_.clear();

}

void CharacterSelect::Update()
{

	// カメラスプライトの初期化（カメラが開かれてテクスチャが作成されたら）
	if (cameraSprite_ == nullptr && CameraCapture::GetInstance()->IsOpened()) {
		if (TextureManager::GetInstance()->HasTexture(CameraCapture::GetTextureName())) {
			cameraSprite_ = std::make_unique<SpriteInstance>();
			cameraSprite_->Initialize(spriteManager_, CameraCapture::GetTextureName(), "CameraPreview");
			cameraSprite_->SetAnchorPoint({ 0.5f,0.0f });
			cameraSprite_->SetPosition({ 640.0f, 64.0f });
			cameraSprite_->SetSize({ 640.0f, 360.0f });
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

	// 遷移待機中の処理
	if (isTransitioning_) {
		transitionTimer_ += 1.0f / 60.0f;
		if (transitionTimer_ >= kTransitionDelay) {
			SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Fade);
			return;
		}
	}

	// QRコードでプレイヤー選択がされたら選択されたプレイヤーを保存
	if (!isTransitioning_ && QRCodeReader::GetInstance()->HasDetected()) {

		std::string modelName = QRCodeReader::GetInstance()->GetData();

		if (modelName.find(".obj") != std::string::npos ||
			modelName.find(".gltf") != std::string::npos) {

			GameData::GetInstance()->SetSelectedModel(modelName);

			// 該当モデルの位置からパーティクルを発生
			for (const auto& obj : object3DInstances_) {
				if (obj->GetModelFileName() == modelName) {
					Vector3 pos = obj->GetTranslate();
					ParticleManager::GetInstance()->SetVelocityScale(5.0f);
					ParticleManager::GetInstance()->Emit("circle", pos, 10);
					break;
				}
			}

			// 遷移待機開始
			isTransitioning_ = true;
			transitionTimer_ = 0.0f;
		}
	}

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	for (const auto& obj : object3DInstances_) {
		obj->Update();
	}

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update();

	sprite_->Update();

	// カメラスプライトが存在する場合のみ更新
	if (cameraSprite_) {
		cameraSprite_->Update();
	}

	for (const auto& s : sprites_) {
		s->Update();
	}
}

void CharacterSelect::Draw()
{

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
	for (const auto& s : sprites_) {
		s->Draw();
	}

	// カメラスプライトが存在する場合のみ描画
	if (cameraSprite_) {
		cameraSprite_->Draw();
	}

	// QRコードのUIを前面に描画
	sprite_->Draw();

	// パーティクルのImGui表示
	ParticleManager::GetInstance()->OnImGui();

	// カメラのImGui
	camera_->OnImGui();
}
