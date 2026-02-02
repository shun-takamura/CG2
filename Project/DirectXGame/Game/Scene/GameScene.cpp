#include "GameScene.h"
#include "SceneManager.h"
#include <cmath>
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
#include "Bullet.h"

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

	// QRから取得したデータ
	std::string modelName = GameData::GetInstance()->GetSelectedModel();
	std::string bulletType = GameData::GetInstance()->GetBulletModel();
	std::string bulletMode = GameData::GetInstance()->GetBulletMode();

	// プレイヤー生成
	player_ = std::make_unique<Player>();
	player_->Initialize(object3DManager_, dxCore_, modelName, bulletType, bulletMode);

	enemy_ = std::make_unique<Enemy>();
	enemy_->Initialize(
		object3DManager_,
		dxCore_,
		"enemy.obj",
		"enemyBullet.obj",
		Vector3{ 10.0f, 0.0f, 0.0f }  // スポーン位置
	);

	// プレイヤーのAABBサイズ設定（モデルの大きさに合わせて調整）
	playerCollider_.halfSize = { 0.5f, 0.5f, 0.5f };

	// エネミーのAABBサイズ設定
	enemyCollider_.halfSize = { 0.5f, 0.5f, 0.5f };

	// ステージ生成
	stage_ = std::make_unique<Object3DInstance>();
	stage_->Initialize(
		object3DManager_,
		dxCore_,
		"Resources",
		"stage.obj",
		"Stage"
	);
	stage_->SetTranslate({ 0.0f, -1.0f, 0.0f }); // 地面の位置に合わせて調整
	stage_->SetRotate({ 0.0f, 0.0f, 0.0f });

	// ===== ライティング設定 =====

    // DirectionalLight: 弱めの下向き環境光
	auto* lm = LightManager::GetInstance();
	lm->SetDirectionalLightColor({ 0.8f, 0.85f, 1.0f, 1.0f }); // やや青白い
	lm->SetDirectionalLightDirection({ 0.0f, -1.0f, 0.2f });     // ほぼ真下
	lm->SetDirectionalLightIntensity(0.05f);                       // 弱め

	// SpotLight: プレイヤーとエネミーの上から照らす（2灯）
	lm->SetSpotLightCount(2);

	// [0] プレイヤー用スポットライト
	lm->SetSpotLightColor(0, { 0.3f, 0.3f, 1.0f, 1.0f }); // 青っぽく
	lm->SetSpotLightPosition(0, { player_->GetPosition().x, 10.0f, -2.0f });
	lm->SetSpotLightDirection(0, { 0.0f, -1.0f, 0.0f });  // 真下
	lm->SetSpotLightIntensity(0, 10.0f);
	lm->SetSpotLightDistance(0, 25.0f);
	lm->SetSpotLightDecay(0, 2.0f);
	lm->SetSpotLightCosAngle(0, std::cos(3.14159f / 4.0f));        // 45度
	lm->SetSpotLightCosFalloffStart(0, std::cos(3.14159f / 8.0f)); // 30度

	// [1] エネミー用スポットライト
	lm->SetSpotLightColor(1, { 1.0f, 0.3f, 0.3f, 1.0f });  // 赤っぽく
	lm->SetSpotLightPosition(1, { enemy_->GetPosition().x, 10.0f, -2.0f });
	lm->SetSpotLightDirection(1, { 0.0f, -1.0f, 0.0f });
	lm->SetSpotLightIntensity(1, 10.0f);
	lm->SetSpotLightDistance(1, 25.0f);
	lm->SetSpotLightDecay(1, 2.0f);
	lm->SetSpotLightCosAngle(1, std::cos(3.14159f / 4.0f));
	lm->SetSpotLightCosFalloffStart(1, std::cos(3.14159f / 8.0f));

	// サウンドのロード
	SoundManager::GetInstance()->LoadFile("fanfare", "Resources/fanfare.wav");
}

void GameScene::Finalize() {

	// このシーンで作ったパーティクルグループを削除
	ParticleManager::GetInstance()->RemoveParticleGroup("circle");

	stage_.reset();
	sprites_.clear();
	object3DInstances_.clear();

}

void GameScene::Update() {

	float deltaTime = dxCore_->GetDeltaTime();

	// ===== スマブラ風カメラ（X軸 + ズーム + Y補正） =====
	{
		Vector3 playerPos = player_->GetPosition();
		Vector3 enemyPos = enemy_->GetPosition();

		float midX = (playerPos.x + enemyPos.x) * 0.5f;
		float dist = std::abs(playerPos.x - enemyPos.x);

		float t = (dist - cameraDistMin_) / (cameraDistMax_ - cameraDistMin_);
		t = std::clamp(t, 0.0f, 1.0f);

		float targetZ = cameraMinZ_ + t * (cameraMaxZ_ - cameraMinZ_);

		// 寄る時ほどYを下げる（近い=cameraCloseUpY_, 遠い=cameraBaseY_）
		float targetY = cameraCloseUpY_ + t * (cameraBaseY_ - cameraCloseUpY_);

		float lerpFactor = 1.0f - std::exp(-cameraSmoothSpeed_ * deltaTime);
		currentCameraPos_.x += (midX - currentCameraPos_.x) * lerpFactor;
		currentCameraPos_.y += (targetY - currentCameraPos_.y) * lerpFactor;
		currentCameraPos_.z += (targetZ - currentCameraPos_.z) * lerpFactor;

		camera_->SetTranslate(currentCameraPos_);
	}

	camera_->Update();

	stage_->Update();

	// プレイヤー更新
	player_->Update(input_, deltaTime);

	// 敵更新
	enemy_->Update(player_->GetPosition(), deltaTime);

	// コライダーの位置をキャラの座標に同期
	playerCollider_.center = player_->GetPosition();
	enemyCollider_.center = enemy_->GetPosition();

	// スポットライトをキャラに追従
	auto* lm = LightManager::GetInstance();
	lm->SetSpotLightPosition(0, { player_->GetPosition().x, 15.0f, -2.0f });
	lm->SetSpotLightPosition(1, { enemy_->GetPosition().x, 15.0f, -2.0f });

	// チャージ中はプレイヤーのライトを強くする
	if (player_->IsCharging()) {
		float chargeT = player_->GetChargeRatio();
		// 通常10 → 最大チャージで25
		float intensity = 10.0f + chargeT * 15.0f;
		lm->SetSpotLightIntensity(0, intensity);
		// 色も白っぽくする
		lm->SetSpotLightColor(0, {
			0.3f + chargeT * 0.7f,
			0.3f + chargeT * 0.7f,
			1.0f,
			1.0f
			});
	} else {
		// 通常状態に戻す
		lm->SetSpotLightIntensity(0, 10.0f);
		lm->SetSpotLightColor(0, { 0.3f, 0.3f, 1.0f, 1.0f });
	}

	// 当たり判定
	CheckCollisions();

	if (!player_->GetIsAlive()) {
		SceneManager::GetInstance()->ChangeScene("GAMEOVER", TransitionType::Fade);
		return;
	}

	if (!enemy_->GetIsAlive()) {
		SceneManager::GetInstance()->ChangeScene("CLEAR", TransitionType::Fade);
		return;
	}

	// パーティクル更新
	ParticleManager::GetInstance()->Update();
}

void GameScene::Draw() {

	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
	LightManager::GetInstance()->OnImGui();

	// ステージ描画
	stage_->Draw(dxCore_);

	// プレイヤー描画（モデル＋弾）
	player_->Draw(dxCore_);

	// 敵描画
	enemy_->Draw(dxCore_);

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

void GameScene::CheckCollisions()
{

	// プレイヤーの弾とエネミー
	for (auto& bullet : player_->GetBullets()) {
		if (!bullet->IsActive()) continue;

		ColliderSphere bulletSphere;
		bulletSphere.center = bullet->GetPosition();
		bulletSphere.radius = bullet->GetRadius();

		if (CollisionHelper::IsCollisionAABBSphere(enemyCollider_, bulletSphere)) {
			// 弾を消す
			bullet->OnHit();
			// 敵にダメージ
			enemy_->TakeDamage(bullet->GetDamage());

			// TODO: ヒットエフェクト（パーティクル等）
			// ParticleManager::GetInstance()->Emit("circle", bullet->GetPosition(), 5);
		}
	}

	// エネミーの弾とプレイヤー
	for (auto& bullet : enemy_->GetBullets()) {
		if (!bullet->IsActive()) continue;

		ColliderSphere bulletSphere;
		bulletSphere.center = bullet->GetPosition();
		bulletSphere.radius = bullet->GetRadius();

		if (CollisionHelper::IsCollisionAABBSphere(playerCollider_, bulletSphere)) {
			// 弾を消す
			bullet->OnHit();
			// プレイヤーにダメージ
			player_->TakeDamage(bullet->GetDamage());

			// TODO: ヒットエフェクト
		}
	}

	// === プレイヤー vs エネミー 体当たり（AABB vs AABB） ===
	// 必要ならここに追加
	// if (CollisionHelper::IsCollisionAABBAABB(playerCollider_, enemyCollider_)) {
	//     // 押し返し処理など
	// }
}
