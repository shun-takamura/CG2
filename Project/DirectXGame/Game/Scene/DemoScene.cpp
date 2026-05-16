#include "DemoScene.h"
#include "SceneManager.h"
#include <algorithm>

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
#include "GPUParticleManager.h"
#include "SoundManager.h"
#include "ControllerInput.h"
#include "MouseInput.h"
#include "InputManager.h"
#include "Debug.h"
#include "LightManager.h"
#include "KeyboardInput.h"
#include "TextureManager.h"
#include"Game.h"
#include "ImGuiManager.h"
#include "Skybox.h"
#include "Primitive/PrimitiveMesh.h"
#include "Primitive/PrimitiveGenerator.h"
#include "Easing.h"
#include "Interpolator.h"
#include "Log.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Json/JsonWriter.h"
#include <numbers>
#include <sstream>
#include "AnimatedModelInstance.h"
#include "AnimatedObject3DInstance.h"
#include "ModelManager.h"
#include "IImGuiEditable.h"
#include "LineRenderer.h"

DemoScene::DemoScene() {
	std::random_device rd;
	randomEngine_.seed(rd());
}

DemoScene::~DemoScene() {
}

namespace {
	// JSONライブラリの動作確認用セルフテスト
	// 結果はOutputDebugStringに出力される（VS出力ウィンドウで確認）
	void RunJsonSelfTest() {
		Log("===== Json Self Test =====\n");

		// 1) コメント付きJSONをパース
		const char* source = R"(
{
    // これは行コメント
    "name": "Pit",      // プレイヤー名
    "hp": 100,
    "ratio": 0.75,
    /* これは
       ブロックコメント */
    "alive": true,
    "tags": ["fast", "ranged", "human"],
    "binds": {
        "fire": "RT",
        "dodge": "A",
        "melee_strong": "Y"
    },
    "escaped": "tab=\there\nand newline",
    "unicode": "あ"
}
)";
		auto parsed = JsonParser::Parse(source);
		if (!parsed.success) {
			std::ostringstream oss;
			oss << "[FAIL] Parse error at line " << parsed.errorLine
				<< " col " << parsed.errorColumn << ": " << parsed.errorMessage << "\n";
			Log(oss.str());
			return;
		}
		Log("[OK] Parsed comment-aware JSON\n");

		// 2) 値を取得して検証
		const JsonValue& root = parsed.value;
		{
			std::ostringstream oss;
			oss << "  name        = " << root["name"].AsString() << "\n"
				<< "  hp          = " << root["hp"].AsInt() << "\n"
				<< "  ratio       = " << root["ratio"].AsDouble() << "\n"
				<< "  alive       = " << (root["alive"].AsBool() ? "true" : "false") << "\n"
				<< "  tags.size   = " << root["tags"].Size() << "\n"
				<< "  tags[0]     = " << root["tags"][0].AsString() << "\n"
				<< "  binds.fire  = " << root["binds"]["fire"].AsString() << "\n"
				<< "  missing     = " << root["this_key_does_not_exist"].AsString("(default)") << "\n";
			Log(oss.str());
		}

		// 3) 書き込み（コメントは出力されない=純JSON準拠）
		JsonValue out = JsonValue::MakeObject();
		out["title"] = "CG2_0_1";
		out["version"] = 2;
		out["scale"] = 1.5;
		out["enabled"] = false;

		JsonValue list = JsonValue::MakeArray();
		list.Push(JsonValue(1));
		list.Push(JsonValue(2));
		list.Push(JsonValue(3));
		out["list"] = std::move(list);

		out["nested"]["inner"]["deep"] = "via operator[] chain";

		std::string pretty = JsonWriter::Write(out, { true, 2 });
		Log("[OK] Pretty output:\n");
		Log(pretty);
		Log("\n");

		std::string compact = JsonWriter::Write(out, { false, 0 });
		Log("[OK] Compact output:\n");
		Log(compact);
		Log("\n");

		// 4) ラウンドトリップ: 書き出し→再パース→値一致確認
		auto reparsed = JsonParser::Parse(pretty);
		if (!reparsed.success) {
			Log("[FAIL] Roundtrip parse failed\n");
		} else {
			bool ok = reparsed.value["title"].AsString() == "CG2_0_1"
				&& reparsed.value["version"].AsInt() == 2
				&& reparsed.value["list"][1].AsInt() == 2
				&& reparsed.value["nested"]["inner"]["deep"].AsString() == "via operator[] chain";
			Log(ok ? "[OK] Roundtrip values match\n" : "[FAIL] Roundtrip mismatch\n");
		}

		// 5) エラー位置の確認（わざと壊れたJSONを渡す）
		auto bad = JsonParser::Parse("{\n  \"a\": 1,\n  \"b\": ,\n}");
		if (!bad.success) {
			std::ostringstream oss;
			oss << "[OK] Detected malformed JSON at line " << bad.errorLine
				<< " col " << bad.errorColumn << " (" << bad.errorMessage << ")\n";
			Log(oss.str());
		} else {
			Log("[FAIL] Malformed JSON was incorrectly accepted\n");
		}

		Log("===== Json Self Test End =====\n");
	}
}

void DemoScene::Initialize() {
	RunJsonSelfTest();

	Game::GetPostEffect()->ResetEffects();

	// デバッグカメラの生成
	debugCamera_ = std::make_unique<DebugCamera>();

	//// スプライトの初期化
	//sprite_ = std::make_unique<SpriteInstance>();
	//sprite_->Initialize(spriteManager_, "DistributionAssets/Textures/uvChecker.png");

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 15.0f, -20.0f });
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());
	skyboxManager_->SetDefaultCamera(camera_.get());

	// Skybox生成
	skybox_ = std::make_unique<Skybox>();
	skybox_->Initialize(skyboxManager_, dxCore_, "Resources/Cubemaps/rogland_clear_night_4k.dds");

	// ここで環境マップをモデルにセット
	object3DManager_->SetEnvironmentTexture("Resources/Cubemaps/rogland_clear_night_4k.dds");


	//// パーティクルの設定
	//ParticleManager::GetInstance()->SetCamera(camera_.get());
	//ParticleManager::GetInstance()->CreateParticleGroup("circle", "DistributionAssets/Textures/circle2.png");

	//// GPU Particle 初期化
	//gpuParticleManager_ = std::make_unique<GPUParticleManager>();
	//gpuParticleManager_->Initialize(dxCore_, srvManager_, "DistributionAssets/Textures/circle2.png");

	//// 加速度フィールドの設定
	//AccelerationField field;
	//field.acceleration = { 15.0f, 0.0f, 0.0f };
	//field.area.min = { -1.0f, -1.0f, -1.0f };
	//field.area.max = { 10.0f, 10.0f, 10.0f };
	//ParticleManager::GetInstance()->SetAccelerationField(field);
	//ParticleManager::GetInstance()->SetAccelerationFieldEnabled(true);

	//// 交互に使うスプライト
	//const std::string textures[2] = {
	//	"DistributionAssets/Textures/uvChecker.png",
	//	"DistributionAssets/Models/MonsterBall/monsterBall.png"
	//};

	//// 5枚生成
	//for (uint32_t i = 0; i < 5; ++i) {
	//	auto newSprite = std::make_unique<SpriteInstance>();
	//	const std::string& texturePath = textures[i % 2];

	//	std::string spriteName = "Sprite_" + std::to_string(i);
	//	newSprite->Initialize(spriteManager_, texturePath, spriteName);

	//	newSprite->SetSize({ 100.0f, 100.0f });
	//	newSprite->SetPosition({ i * 2.0f, 0.0f });
	//	sprites_.push_back(std::move(newSprite));
	//}

	// 3Dオブジェクトを配列で管理
	/*const std::string modelFiles[] = { 
		"Models/MonsterBall/monsterBall.obj", 
		"Models/Terrain/terrain.obj", 
		"Models/Plane/plane.gltf"
	};

	const std::string objectNames[] = { 
		"MonsterBall", 
		"terrain", 
		"plane"
	};

	for (int i = 0; i < 3; ++i) {
		auto obj = std::make_unique<Object3DInstance>();
		obj->Initialize(
			object3DManager_,
			dxCore_,
			"DistributionAssets/",
			modelFiles[i],
			objectNames[i]
		);
		obj->SetTranslate({ 0.0f, 0.0f, 0.0f });
		object3DInstances_.push_back(std::move(obj));
	}*/

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/Sounds/fanfare.wav");

	//// Ringのテスト（角度範囲指定版）
	//testRing_ = std::make_unique<PrimitiveMesh>();

	//PrimitiveGenerator::RingParams ringParams;
	//ringParams.outerRadius = 1.0f;
	//ringParams.innerRadius = 0.0f;    // 内径0にすると円盤っぽくなる
	//// 波打つ輪っか
	//ringParams.divisions = 64;
	//ringParams.outerRadiusPerDivision.clear();
	//for (int i = 0; i < 64; ++i) {
	//	float angle = i / 64.0f * 2.0f * std::numbers::pi_v<float>;
	//	float wave = 1.0f + 0.2f * std::sin(angle * 5.0f);  // 5波
	//	ringParams.outerRadiusPerDivision.push_back(wave);
	//}
	//ringParams.innerColor = { 1.0f, 1.0f, 1.0f, 1.0f };  // 中央：白不透明
	//ringParams.outerColor = { 1.0f, 1.0f, 1.0f, 0.0f };  // 外側：白透明
	//ringParams.startAngle = 0.0f;
	//ringParams.endAngle = 2.0f * std::numbers::pi_v<float>;
	//ringParams.uvHorizon = true;

	//testRing_->Initialize(PrimitiveGenerator::CreateRing(ringParams));
	//testRing_->SetTexture("Resources/Textures/white1x1.dds");
	//testRing_->SetBlendMode(PrimitivePipeline::kBlendModeAdd);
	//testRing_->GetTransform().translate = { 0.0f, 8.0f, 0.0f };
	////testRing_->SetUVScroll({ 0.0f, 1.0f });

	//// Cylinderのテスト
	//testCylinder_ = std::make_unique<PrimitiveMesh>();
	//testCylinder_->Initialize(PrimitiveGenerator::CreateCylinder(
	//	1.0f, 1.0f, 3.0f, 32,
	//	{ 1.0f, 1.0f, 1.0f, 1.0f },
	//	{ 1.0f, 1.0f, 1.0f, 1.0f }
	//));
	//testCylinder_->SetTexture("DistributionAssets/Textures/gradationLine.png");
	//testCylinder_->SetBlendMode(PrimitivePipeline::kBlendModeNone);
	//testCylinder_->GetTransform().translate = { 0.0f, 2.0f, 0.0f };
	////testCylinder_->SetDepthWrite(true);
	//testCylinder_->SetUVFlipV(true);
	//testCylinder_->SetUVScroll({ 0.1f, 0.0f });
	//testCylinder_->SetAlphaReference(0.5f);

	auto sneakWalkModel = std::make_unique<Object3DInstance>();
	sneakWalkModel->Initialize(
		object3DManager_,
		dxCore_,
		"Resources/Models/Animated/SneakWalk",
		"sneakWalk.mesh",
		"sneakWalk"
	);
	sneakWalkModel->SetScale({ 1.0f, 1.0f, 1.0f });
	sneakWalkModel->SetUseEnvironmentMap(true);
	sneakWalkModel->SetEnvironmentCoefficient(0.5f);
	object3DInstances_.push_back(std::move(sneakWalkModel));


	// sneakWalkのモデルとアニメーションの読み込み
	sneakWalk = std::make_unique<AnimatedModelInstance>();
	sneakWalk->Initialize(
		ModelManager::GetInstance()->GetModelCore(),
		"Resources/Models/Animated/SneakWalk",
		"sneakWalk.mesh"
	);

	// sneakWalkのインスタンスを作成
	sneakWalkInstance_= std::make_unique<AnimatedObject3DInstance>();
	sneakWalkInstance_->Initialize(
		object3DManager_,
		skinningComputeManager_,
		dxCore_,
		srvManager_,
		sneakWalk.get(),
		"sneakWalk"
	);
	sneakWalkInstance_->SetScale({ 1.0f, 1.0f, 1.0f });
	sneakWalkInstance_->SetUseEnvironmentMap(true);
	sneakWalkInstance_->SetEnvironmentCoefficient(0.5f);

	//// AnimatedCubeのモデル＆アニメーション読み込み
	//animatedCubeModel_ = std::make_unique<AnimatedModelInstance>();
	//animatedCubeModel_->Initialize(
	//	ModelManager::GetInstance()->GetModelCore(),
	//	"DistributionAssets/Models/AnimatedCube",
	//	"AnimatedCube.gltf"
	//);

	//// AnimatedCubeのインスタンスを作成
	//animatedCubeInstance_ = std::make_unique<AnimatedObject3DInstance>();
	//animatedCubeInstance_->Initialize(
	//	object3DManager_,
	//	skinningComputeManager_,
	//	dxCore_,
	//	srvManager_,
	//	animatedCubeModel_.get(),
	//	"AnimatedCube"
	//);
	//animatedCubeInstance_->SetTranslate({ 5.0f, 2.0f, 0.0f });
	//animatedCubeInstance_->SetScale({ 1.0f, 1.0f, 1.0f });

	//dxCore_->SetUseFixedFrameRate(false);

#ifdef _DEBUG
	// Camera/RenderTextureはBaseScene::GetCamera経由とGame::Initialize側で中央化済み
	ImGuiManager::Instance().SetGPUParticleManager(gpuParticleManager_.get());
#endif
}

void DemoScene::Finalize() {

#ifdef _DEBUG
	ImGuiManager::Instance().SetGPUParticleManager(nullptr);
#endif

	if (gpuParticleManager_) {
		gpuParticleManager_->Finalize();
		gpuParticleManager_.reset();
	}

	animatedCubeInstance_.reset();
	animatedCubeModel_.reset();

	testCylinder_.reset();
	testRing_.reset();
	sprites_.clear();
	object3DInstances_.clear();
	hitEffectPlanes_.clear();
}

void DemoScene::Update() {

	// カメラプレビューとQRコード読み取り（BaseSceneが自動で更新・描画する）
	UseCameraCapture(true);
	UseQRCodeReader(true);

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
		SoundManager::GetInstance()->Play2DSound("fanfare");
		Debug::Log("trigger [1] - 2D Sound Play");
	}

	if (input_->GetKeyboard()->TriggerKey(DIK_2)) {
		SoundManager::GetInstance()->Stop2DSound("fanfare");
		Debug::Log("trigger [2] - 2D Sound Stop");
	}

	// 3キー: AnimatedCubeの右側(5, 2, 0)で3Dサウンドを再生
    // → 右スピーカーからより大きく聴こえるはず
	if (input_->GetKeyboard()->TriggerKey(DIK_3)) {
		if (sound3DHandle_ != 0) {
			SoundManager::GetInstance()->Stop3DSound(sound3DHandle_);
		}
		sound3DHandle_ = SoundManager::GetInstance()->Play3DSound(
			"fanfare", { -5.0f, 5.0f, 5.0f });
		Debug::Log("trigger [3] - 3D Sound Play at (5, 2, 0)");
	}

	// 4キー: 3Dサウンドを停止
	if (input_->GetKeyboard()->TriggerKey(DIK_4)) {
		SoundManager::GetInstance()->Stop3DSound(sound3DHandle_);
		sound3DHandle_ = 0;
		Debug::Log("trigger [4] - 3D Sound Stop");
	}

	// 5キー: 音源が円周上を回り続けるサウンドをON/OFF
	// → 左右の定位変化 + ドップラー効果による音程変化が聴こえるはず
	if (input_->GetKeyboard()->TriggerKey(DIK_5)) {
		sound3DOrbitActive_ = !sound3DOrbitActive_;
		if (!sound3DOrbitActive_ && sound3DOrbitHandle_ != 0) {
			SoundManager::GetInstance()->Stop3DSound(sound3DOrbitHandle_);
			sound3DOrbitHandle_ = 0;
		}
		Debug::Log(sound3DOrbitActive_ ? "trigger [5] - Orbit ON" : "trigger [5] - Orbit OFF");
	}

	// 音源が半径8で円周上を回る（ドップラー・定位の確認用）
	if (sound3DOrbitActive_) {
		sound3DOrbitTime_ += GetScaledDeltaTime();
		const float orbitRadius = 8.0f;
		const float orbitSpeed = 1.5f; // ラジアン/秒
		float angle = sound3DOrbitTime_ * orbitSpeed;

		Vector3 orbitPos = {
			orbitRadius * std::cosf(angle),
			3.0f,
			orbitRadius * std::sinf(angle)
		};
		// 接線方向の速度（ドップラー計算に使う）
		Vector3 orbitVel = {
			-orbitRadius * orbitSpeed * std::sinf(angle),
			0.0f,
			 orbitRadius * orbitSpeed * std::cosf(angle)
		};

		if (sound3DOrbitHandle_ == 0) {
			// まだ再生していなければ開始
			sound3DOrbitHandle_ = SoundManager::GetInstance()->Play3DSound(
				"fanfare", orbitPos, orbitVel, 30.0f);
		} else {
			// 毎フレーム位置を更新
			SoundManager::GetInstance()->UpdateEmitter(sound3DOrbitHandle_, orbitPos, orbitVel);
		}
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
	const float deltaTime = GetScaledDeltaTime();

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

	//if (sprite_) {
	//	sprite_->SetAnchorPoint({ 0.f, 0.0f });
	//	sprite_->SetPosition({ 0.0f,0.0f });
	//	sprite_->SetSize({ 200.0f,200.0f });
	//	sprite_->Update();
	//	//sprite_->SetIsFlipX(true);

	//	// 回転テスト
	//	float rotation = sprite_->GetRotation();
	//	rotation += 0.01f;
	//	sprite_->SetRotation(rotation);
	//	sprite_->Update();
	//}

	// リスナー（カメラ）の位置をSoundManagerに反映
	SoundManager::GetInstance()->UpdateListener(camera_.get());
	// 3Dサウンドの更新・終了検知
	SoundManager::GetInstance()->Update();

	// カメラの更新は必ずオブジェクトの更新前にやる
	camera_->Update();

	// 3Dオブジェクトの更新
	for (const auto& obj : object3DInstances_) {
		obj->Update();
	}

	// SceneEditorWindow からドロップで追加された動的エンティティ
	UpdateDynamicPrimitives();
	for (auto& a : dynamicAnimated_) {
		a->Update(GetScaledDeltaTime());
	}
	for (auto& s : dynamicSprites_) {
		s->Update();
	}

	// アニメーション付きオブジェクトの更新
	if (sneakWalkInstance_) {
		sneakWalkInstance_->Update(GetScaledDeltaTime());
	}

	//// アニメーション付きオブジェクトの更新
	//if (animatedCubeInstance_) {
	//	animatedCubeInstance_->Update(GetScaledDeltaTime());
	//}

	// Skybox更新を追加
	skybox_->Update();

	// パーティクル更新処理
	ParticleManager::GetInstance()->Update(GetScaledDeltaTime());

	// GPU Particle 更新（PerView書き込み）
	if (gpuParticleManager_) {
		gpuParticleManager_->Update(camera_.get(), GetScaledDeltaTime());
	}

	//// 1秒間に発生させる量を自動制御
	//emitTimer_ += GetScaledDeltaTime();
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

	// Skybox描画は深度を書き込まないのでRead-Only DSVで問題なし
	auto readOnlyDsv = dxCore_->GetReadOnlyDsvHandle();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &readOnlyDsv);

	skyboxManager_->DrawSetting();
	skybox_->Draw(dxCore_);

	// Object3D以降は通常DSVに切り替え（深度書き込みを有効化）
	auto normalDsv = dxCore_->GetDsvHeap()->GetCPUDescriptorHandleForHeapStart();
	commandList->OMSetRenderTargets(1, &rtvHandle, false, &normalDsv);

	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());

	// 3Dオブジェクト描画
	for (const auto& obj : object3DInstances_) {
		obj->Draw(dxCore_);
	}
	// 動的アニメーションモデル
	for (auto& a : dynamicAnimated_) {
		a->Draw(dxCore_);
	}

	// 動的プリミティブ
	DrawDynamicPrimitives();

	// アニメーション付きオブジェクトの描画
	if (sneakWalkInstance_) {
		sneakWalkInstance_->Draw(dxCore_);
	}

	if (animatedCubeInstance_) {
		animatedCubeInstance_->Draw(dxCore_);
	}

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

	// GPU Particle 描画
	if (gpuParticleManager_) {
		gpuParticleManager_->Draw();
	}

#ifdef USE_IMGUI
	// Skeletonのデバッグ描画（全モデル描画完了後にまとめて行う）
	if (sneakWalkInstance_) {
		sneakWalkInstance_->DrawSkeletonDebug(dxCore_);
	}

	if (animatedCubeInstance_) {
		animatedCubeInstance_->DrawSkeletonDebug(dxCore_);
	}

	// SceneEditorWindow からドロップで追加された動的アニメーションモデルも対象
	for (auto& a : dynamicAnimated_) {
		a->DrawSkeletonDebug(dxCore_);
	}
#endif

	// 線描画（Skeletonデバッグ用 など）
	LineRenderer::GetInstance()->SetCamera(camera_.get());
	LineRenderer::GetInstance()->Draw();

	// スプライト描画
	spriteManager_->DrawSetting();
	if (sprite_) sprite_->Draw();
	for (const auto& s : sprites_) {
		s->Draw();
	}
	// 動的スプライト
	for (auto& s : dynamicSprites_) {
		s->Draw();
	}
}

// =============================================================
// 動的オブジェクトの追加・削除（SceneEditorWindow 経由）
// =============================================================
void DemoScene::AddDynamicObject(const std::string& dirPath, const std::string& filename) {
	auto obj = std::make_unique<Object3DInstance>();
	obj->Initialize(object3DManager_, dxCore_, dirPath, filename);
	obj->SetCamera(camera_.get());
	object3DInstances_.push_back(std::move(obj));
}

void DemoScene::RemoveDynamicObject(const std::string& name) {
	auto it = std::find_if(object3DInstances_.begin(), object3DInstances_.end(),
		[&name](const std::unique_ptr<Object3DInstance>& o) { return o->GetName() == name; });
	if (it != object3DInstances_.end()) {
		// GPU 使用中対策で 1 フレーム遅延破棄（BaseScene::ProcessAsyncLoads で clear される）
		deferredDeletes_.emplace_back(std::shared_ptr<Object3DInstance>(it->release()));
		object3DInstances_.erase(it);
	}
}

void DemoScene::AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) {
	auto sprite = std::make_unique<SpriteInstance>();
	sprite->Initialize(spriteManager_, texturePath);
	sprite->SetPosition({ clientX, clientY });
	dynamicSprites_.push_back(std::move(sprite));
}
#ifdef USE_IMGUI

void DemoScene::RemoveDynamicSprite(const std::string& name) {
	// 1. ドロップで追加された動的スプライト
	auto it = std::find_if(dynamicSprites_.begin(), dynamicSprites_.end(),
		[&name](const std::unique_ptr<SpriteInstance>& s) { return s->GetName() == name; });
	if (it != dynamicSprites_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(it->release()));
		dynamicSprites_.erase(it);
		return;
	}
	// 2. シーン初期化済みの sprites_ ベクター
	auto it2 = std::find_if(sprites_.begin(), sprites_.end(),
		[&name](const std::unique_ptr<SpriteInstance>& s) { return s->GetName() == name; });
	if (it2 != sprites_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(it2->release()));
		sprites_.erase(it2);
		return;
	}
	// 3. 名前付きスプライトメンバ
	if (sprite_ && sprite_->GetName() == name) {
		deferredDeletes_.emplace_back(std::shared_ptr<SpriteInstance>(sprite_.release()));
		return;
	}
}

#endif // USE_IMGUI

void DemoScene::AddDynamicAnimated(const std::string& dirPath, const std::string& filename) {
	auto model = std::make_unique<AnimatedModelInstance>();
	model->Initialize(ModelManager::GetInstance()->GetModelCore(), dirPath, filename);

	auto obj = std::make_unique<AnimatedObject3DInstance>();
	obj->Initialize(object3DManager_, skinningComputeManager_, dxCore_, srvManager_,
		model.get(), filename);

	dynamicAnimatedModels_.push_back(std::move(model));
	dynamicAnimated_.push_back(std::move(obj));
}

void DemoScene::RemoveDynamicAnimated(const std::string& name) {
	// 1. ドロップで追加された動的アニメーションモデル
	auto it = std::find_if(dynamicAnimated_.begin(), dynamicAnimated_.end(),
		[&name](const std::unique_ptr<AnimatedObject3DInstance>& a) { return a->GetName() == name; });
	if (it != dynamicAnimated_.end()) {
		deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(it->release()));
		dynamicAnimated_.erase(it);
		return;
	}
	// 2. シーン初期化済みの名前付きアニメーションメンバ
	if (sneakWalkInstance_ && sneakWalkInstance_->GetName() == name) {
		deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(sneakWalkInstance_.release()));
		return;
	}
	if (animatedCubeInstance_ && animatedCubeInstance_->GetName() == name) {
		deferredDeletes_.emplace_back(std::shared_ptr<AnimatedObject3DInstance>(animatedCubeInstance_.release()));
		return;
	}
}

#ifdef USE_IMGUI
bool DemoScene::IsDynamicObject(IImGuiEditable* editable) const {
	// 基底（プリミティブ）でヒットすれば早期 return
	if (BaseScene::IsDynamicObject(editable)) return true;

	for (const auto& obj : object3DInstances_) {
		if (static_cast<IImGuiEditable*>(obj.get()) == editable) return true;
	}
	for (const auto& s : dynamicSprites_) {
		if (static_cast<IImGuiEditable*>(s.get()) == editable) return true;
	}
	for (const auto& a : dynamicAnimated_) {
		if (static_cast<IImGuiEditable*>(a.get()) == editable) return true;
	}
	// シーン初期化済み（sprites_ ベクター + 名前付き animated/sprite メンバ）も削除可能
	for (const auto& s : sprites_) {
		if (static_cast<IImGuiEditable*>(s.get()) == editable) return true;
	}
	if (sprite_ && static_cast<IImGuiEditable*>(sprite_.get()) == editable) return true;
	if (sneakWalkInstance_ && static_cast<IImGuiEditable*>(sneakWalkInstance_.get()) == editable) return true;
	if (animatedCubeInstance_ && static_cast<IImGuiEditable*>(animatedCubeInstance_.get()) == editable) return true;
	return false;
}
#endif