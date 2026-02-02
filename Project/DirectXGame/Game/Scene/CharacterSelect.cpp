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
	sprite_->Initialize(spriteManager_, "Resources/uvChecker.png");

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	camera_->SetTranslate({ 0.0f, 2.0f, -30.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

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

void CharacterSelect::Finalize()
{

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

	// QRコードでプレイヤー選択がされたら選択されたプレイヤーを保存してシーンを切り替える
	if (cameraSprite_) {

		// 選択がされたプレイヤーのモデルを保存

		// 選択がされたプレイヤーからパーティクルを発生させる
		ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 10);

		// シーンを切り替える
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Fade);
		return;
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
		ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 10);
	}

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
