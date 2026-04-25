#include "DemoScene.h"
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
#include "TextureManager.h"
#include"Game.h"
#include "Skybox.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Easing.h"
#include "Interpolator.h"
#include <numbers>
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "ModelManager.h"

DemoScene::DemoScene() {
	std::random_device rd;
	randomEngine_.seed(rd());
}

DemoScene::~DemoScene() {
}

void DemoScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	// デバッグカメラの生成
	debugCamera_ = std::make_unique<DebugCamera>();

	// スプライトの初期化
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager_, "DistributionAssets/Textures/uvChecker.png");

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 15.0f, -20.0f });
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	// Skybox生成
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, "Resources/Cubemaps/rogland_clear_night_4k.dds");

	object3DManager_->SetEnvironmentTexture("Resources/Cubemaps/rogland_clear_night_4k.dds");

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "DistributionAssets/Textures/circle2.png");

	//// 加速度フィールドの設定
	//AccelerationField field;
	//field.acceleration = { 15.0f, 0.0f, 0.0f };
	//field.area.min = { -1.0f, -1.0f, -1.0f };
	//field.area.max = { 10.0f, 10.0f, 10.0f };
	//ParticleManager::GetInstance()->SetAccelerationField(field);
	//ParticleManager::GetInstance()->SetAccelerationFieldEnabled(true);

	// 交互に使うスプライト
	const std::string textures[2] = {
		"DistributionAssets/Textures/uvChecker.png",
		"DistributionAssets/Models/MonsterBall/monsterBall.png"
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

	//// 3Dオブジェクトを配列で管理
	//const std::string modelFiles[] = { 
	//	"Models/MonsterBall/monsterBall.obj", 
	//	"Models/Terrain/terrain.obj", 
	//	"Models/Plane/plane.gltf"
	//};

	//const std::string objectNames[] = { 
	//	"MonsterBall", 
	//	"terrain", 
	//	"plane"
	//};

	//for (int i = 0; i < 3; ++i) {
	//	auto obj = std::make_unique<Object3DInstance>();
	//	obj->Initialize(
	//		object3DManager_,
	//		dxCore_,
	//		"DistributionAssets/",
	//		modelFiles[i],
	//		objectNames[i]
	//	);
	//	obj->SetTranslate({ 0.0f, 0.0f, 0.0f });
	//	object3DInstances_.push_back(std::move(obj));
	//}

	auto sneakWalk = std::make_unique<Object3DInstance>();
	sneakWalk->Initialize(
		object3DManager_,
		dxCore_,
		"DistributionAssets/",
		"Models/human/sneakWalk.gltf",
		"sneakWalk"
	);
	sneakWalk->SetScale({ 100.0f, 100.0f, 100.0f });
	sneakWalk->SetUseEnvironmentMap(true);
	sneakWalk->SetEnvironmentCoefficient(0.5f);
	object3DInstances_.push_back(std::move(sneakWalk));

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "DistributionAssets/Sounds/fanfare.wav");

	// Ringのテスト（角度範囲指定版）
	testRing_ = std::make_unique<PrimitiveMesh>();

	PrimitiveGenerator::RingParams ringParams;
	ringParams.outerRadius = 1.0f;
	ringParams.innerRadius = 0.0f;    // 内径0にすると円盤っぽくなる
	// 波打つ輪っか
	ringParams.divisions = 64;
	ringParams.outerRadiusPerDivision.clear();
	for (int i = 0; i < 64; ++i) {
		float angle = i / 64.0f * 2.0f * std::numbers::pi_v<float>;
		float wave = 1.0f + 0.2f * std::sin(angle * 5.0f);  // 5波
		ringParams.outerRadiusPerDivision.push_back(wave);
	}
	ringParams.innerColor = { 1.0f, 1.0f, 1.0f, 1.0f };  // 中央：白不透明
	ringParams.outerColor = { 1.0f, 1.0f, 1.0f, 0.0f };  // 外側：白透明
	ringParams.startAngle = 0.0f;
	ringParams.endAngle = 2.0f * std::numbers::pi_v<float>;
	ringParams.uvHorizon = true;

	testRing_->Initialize(PrimitiveGenerator::CreateRing(ringParams));
	testRing_->SetTexture("DistributionAssets/Textures/white1x1.png");
	testRing_->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
	testRing_->GetTransform().translate = { 0.0f, 8.0f, 0.0f };
	//testRing_->SetUVScroll({ 0.0f, 1.0f });

	// Cylinderのテスト
	testCylinder_ = std::make_unique<PrimitiveMesh>();
	testCylinder_->Initialize(PrimitiveGenerator::CreateCylinder(
		1.0f, 1.0f, 3.0f, 32,
		{ 1.0f, 1.0f, 1.0f, 1.0f },
		{ 1.0f, 1.0f, 1.0f, 1.0f }
	));
	testCylinder_->SetTexture("DistributionAssets/Textures/gradationLine.png");
	testCylinder_->SetBlendMode(PrimitivePipeline::kBlendModeNone);
	testCylinder_->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
	testCylinder_->SetDepthWrite(true);
	testCylinder_->SetUVFlipV(true);
	testCylinder_->SetUVScroll({ 0.1f, 0.0f });
	testCylinder_->SetAlphaReference(0.5f);

	// AnimatedCubeのモデル＆アニメーション読み込み
	animatedCubeModel_ = std::make_unique<AnimatedModelInstance>();
	animatedCubeModel_->Initialize(
		ModelManager::GetInstance()->GetModelCore(),
		"DistributionAssets/Models/AnimatedCube",
		"AnimatedCube.gltf"
	);

	// AnimatedCubeのインスタンスを作成
	animatedCubeInstance_ = std::make_unique<AnimatedObject3DInstance>();
	animatedCubeInstance_->Initialize(
		object3DManager_,
		dxCore_,
		animatedCubeModel_.get(),
		"AnimatedCube"
	);
	animatedCubeInstance_->SetTranslate({ 5.0f, 2.0f, 0.0f });
	animatedCubeInstance_->SetScale({ 1.0f, 1.0f, 1.0f });
}

void DemoScene::Finalize() {

	animatedCubeInstance_.reset();
	animatedCubeModel_.reset();

	testCylinder_.reset();
	testRing_.reset();
	sprites_.clear();
	object3DInstances_.clear();
	hitEffectPlanes_.clear();
}

void DemoScene::Update() {

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

	// HキーでHitEffect発生
	if (input_->GetKeyboard()->TriggerKey(DIK_H)) {
		// --- Plane版 HitEffect（8枚の星型） ---
		const int planeCount = 8;
		const Vector3 hitPos = { 0.0f, 5.0f, 0.0f };

		for (int i = 0; i < planeCount; ++i) {
			HitEffectPlane p;

			// Plane生成
			p.mesh = std::make_unique<PrimitiveMesh>();
			p.mesh->Initialize(PrimitiveGenerator::CreatePlane(1.0f, 1.0f));
			p.mesh->SetTexture("DistributionAssets/Textures/circle2.png");
			p.mesh->SetBlendMode(PrimitivePipeline::kBlendModeAdd);

			// 縦長スケール（初期値）
			p.initialScale = { 0.05f, 1.0f, 1.0f };
			p.mesh->GetTransform().scale = p.initialScale;

			// 位置
			p.mesh->GetTransform().translate = hitPos;

			// Z軸回転（-π〜+π のランダム）
			std::uniform_real_distribution<float> distAngle(
				-std::numbers::pi_v<float>,
				std::numbers::pi_v<float>);
			float angle = distAngle(randomEngine_);
			p.mesh->GetTransform().rotate = { 0.0f, 0.0f, angle };

			// 寿命
			p.lifeTime = 0.6f;
			p.currentTime = 0.0f;

			hitEffectPlanes_.push_back(std::move(p));
		}
	}

	// --- HitEffectPlanes の更新 ---
	const float deltaTime = dxCore_->GetDeltaTime();

	for (auto it = hitEffectPlanes_.begin(); it != hitEffectPlanes_.end();) {
		it->currentTime += deltaTime;

		if (it->currentTime >= it->lifeTime) {
			// 寿命尽きた → 削除
			it = hitEffectPlanes_.erase(it);
			continue;
		}

		// 進行率 t (0.0 ~ 1.0)
		float t = it->currentTime / it->lifeTime;

		// スケール：初期サイズ → 3倍に拡大（EaseOutCubic）
		float scaleFactor = 1.0f + 2.0f * Easing::Apply(Easing::Type::EaseOutCubic, t);
		it->mesh->GetTransform().scale = {
			it->initialScale.x * scaleFactor,
			it->initialScale.y * scaleFactor,
			it->initialScale.z
		};

		// アルファ：1.0 → 0.0（フェードアウト、EaseOutCubic）
		float alpha = 1.0f - Easing::Apply(Easing::Type::EaseOutCubic, t);
		it->mesh->SetColor({ 1.0f, 1.0f, 1.0f, alpha });

		// 毎フレーム更新
		it->mesh->Update(camera_.get());

		++it;
	}

	// Update内
	if (testRing_) testRing_->Update(camera_.get());

	// Update内
	if (testCylinder_) testCylinder_->Update(camera_.get());

	if (input_->GetKeyboard()->TriggerKey(DIK_P)) {
		dxCore_->SetUseFixedFrameRate(false);
	}

	// Fキーで場のON/OFF切り替え
	if (input_->GetKeyboard()->TriggerKey(DIK_F)) {
		bool current = ParticleManager::GetInstance()->IsAccelerationFieldEnabled();
		ParticleManager::GetInstance()->SetAccelerationFieldEnabled(!current);
	}

	sprite_->SetAnchorPoint({ 0.f, 0.0f });
	sprite_->SetPosition({ 0.0f,0.0f });
	sprite_->SetSize({ 200.0f,200.0f });
	sprite_->Update();
	//sprite_->SetIsFlipX(true);

	// 回転テスト
	float rotation = sprite_->GetRotation();
	rotation += 0.01f;
	sprite_->SetRotation(rotation);
	sprite_->Update();

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	// 3Dオブジェクトの更新
	for (const auto& obj : object3DInstances_) {
		obj->Update();
	}

	// アニメーション付きオブジェクトの更新
	if (animatedCubeInstance_) {
		animatedCubeInstance_->Update(dxCore_->GetDeltaTime());
	}

	// Skybox更新を追加
	skybox_->Update();

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update();

	//// 1秒間に発生させる量を自動制御
	//emitTimer_ += dxCore_->GetDeltaTime();
	//float emitRate = ParticleManager::GetInstance()->GetEmitterSettings().emitRate;
	//if (emitRate > 0.0f) {
	//	float emitInterval = 1.0f / emitRate;
	//	while (emitTimer_ >= emitInterval) {
	//		ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);
	//		emitTimer_ -= emitInterval;
	//	}
	//}

	//// circleのパーティクルを常に発生させる
	//ParticleManager::GetInstance()->Emit("circle", { 0.0f, 0.0f, 0.0f }, 1);

	//// キー入力でパーティクル発生
	//if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
	//	ParticleManager::GetInstance()->Emit("uvChecker", { 0.0f, 0.0f, 0.0f }, 10);
	//}

	// カメラスプライトが存在する場合のみ更新
	if (cameraSprite_) {
		cameraSprite_->Update();
	}

	for (const auto& s : sprites_) {
		s->Update();
	}

	debugCamera_->Update(input_->GetKeyboard()->keys_);
}

void DemoScene::Draw() {

	// DSVを切り替えてSkybox描画
	// 必ず最初に背景から描画していくこと
	auto commandList = dxCore_->GetCommandList();
	auto rtvHandle = Game::GetPostEffect()->GetSceneRenderTarget()->GetRTVHandle();
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	skybox_->Draw(dxCore_);

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

	// アニメーション付きオブジェクトの描画
	if (animatedCubeInstance_) {
		animatedCubeInstance_->Draw(dxCore_);
	}

	auto normalDsv = dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	if (testCylinder_) {
		testCylinder_->Draw();
	}

	// HitEffectPlanes の描画
	for (auto& p : hitEffectPlanes_) {
		p.mesh->Draw();
	}

	if (testRing_) { 
		testRing_->Draw(); 
	}

	// パーティクル描画
	ParticleManager::GetInstance()->Draw();

	// スプライト描画
	spriteManager_->DrawSetting();
	sprite_->Draw();
	for (const auto& s : sprites_) {
		s->Draw();
	}

	camera_->OnImGui();

	// カメラスプライトが存在する場合のみ描画
	if (cameraSprite_) {
		cameraSprite_->Draw();
	}

	// パーティクルのImGui表示
	ParticleManager::GetInstance()->OnImGui();
}