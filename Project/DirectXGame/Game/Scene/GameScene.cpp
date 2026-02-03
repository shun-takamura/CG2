#include "GameScene.h"
#include "SceneManager.h"
#include <cmath>
#include <algorithm>
#include <cstdlib>

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
#include "Game.h"

GameScene::GameScene() {
}

GameScene::~GameScene() {
}

void GameScene::Initialize() {

	// デバッグカメラの生成
	debugCamera_ = std::make_unique<DebugCamera>();

	// スプライトの初期化
	operation_ = std::make_unique<SpriteInstance>();
	operation_->Initialize(spriteManager_, "Resources/operation.png");
	operation_->SetAnchorPoint({ 0.0f,0.0f });
	operation_->SetSize({ 1280.0f,720.0f });
	operation_->SetPosition({ 0.0f,0.0f });

	// カメラの生成
	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 15.0f, -20.0f });
	camera_->SetRotate({ 0.5f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	// パーティクルの設定
	ParticleManager::GetInstance()->SetCamera(camera_.get());
	ParticleManager::GetInstance()->CreateParticleGroup("circle", "Resources/circle.png");

	// ===== UI用スプライト初期化 =====

    // プレイヤーHP用
	hpGaugeBack_ = std::make_unique<SpriteInstance>();
	hpGaugeBack_->Initialize(spriteManager_, "Resources/hp_gauge_back.png", "HPGaugeBack");

	hpGaugeFill_ = std::make_unique<SpriteInstance>();
	hpGaugeFill_->Initialize(spriteManager_, "Resources/hp_gauge_fill.png", "HPGaugeFill");

	// エネミーHP用
	hpGaugeBackEnemy_ = std::make_unique<SpriteInstance>();
	hpGaugeBackEnemy_->Initialize(spriteManager_, "Resources/hp_gauge_back.png", "HPGaugeBackEnemy");

	hpGaugeFillEnemy_ = std::make_unique<SpriteInstance>();
	hpGaugeFillEnemy_->Initialize(spriteManager_, "Resources/hp_gauge_fill.png", "HPGaugeFillEnemy");

	// 弾数ゲージ用
	ammoGaugeBack_ = std::make_unique<SpriteInstance>();
	ammoGaugeBack_->Initialize(spriteManager_, "Resources/hp_gauge_back.png", "AmmoGaugeBack");

	ammoGaugeFill_ = std::make_unique<SpriteInstance>();
	ammoGaugeFill_->Initialize(spriteManager_, "Resources/hp_gauge_fill.png", "AmmoGaugeFill");

	// リロードゲージ
	reloadGauge_ = std::make_unique<SpriteInstance>();
	reloadGauge_->Initialize(spriteManager_, "Resources/white1x1.png", "ReloadGauge");

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

	// ===== 天球生成 =====
	skydome_ = std::make_unique<Object3DInstance>();
	skydome_->Initialize(
		object3DManager_,
		dxCore_,
		"Resources",
		"skydome.obj",
		"Skydome"
	);
	skydome_->SetTranslate({ 0.0f, 0.0f, 0.0f });
	skydome_->SetScale({ 100.0f, 100.0f, 100.0f });  // 十分大きくする
	skydome_->SetRotate({ 0.0f, 0.0f, 0.0f });

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

	// ポストエフェクトをリセット
	Game::GetInstance()->GetPostEffect()->ResetEffects();
}

void GameScene::Finalize() {

	// このシーンで作ったパーティクルグループを削除
	ParticleManager::GetInstance()->RemoveParticleGroup("circle");

	skydome_.reset();
	stage_.reset();
	sprites_.clear();
	object3DInstances_.clear();

	// UI用スプライト解放
	hpGaugeBack_.reset();
	hpGaugeFill_.reset();
	hpGaugeBackEnemy_.reset();
	hpGaugeFillEnemy_.reset();
	ammoGaugeBack_.reset();
	ammoGaugeFill_.reset();
	reloadGauge_.reset();
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

		// カメラシェイク更新
		UpdateCameraShake(deltaTime);

		// シェイクオフセットを適用した最終位置
		Vector3 finalCameraPos = currentCameraPos_;
		finalCameraPos.x += cameraShakeOffset_.x;
		finalCameraPos.y += cameraShakeOffset_.y;
		finalCameraPos.z += cameraShakeOffset_.z;

		camera_->SetTranslate(finalCameraPos);
	}

	camera_->Update();

	// 天球をカメラに追従（常にカメラを中心に）
	skydome_->SetTranslate({ currentCameraPos_.x, 0.0f, 0.0f });
	skydome_->Update();

	stage_->Update();

	// プレイヤー更新
	player_->Update(input_, deltaTime);

	// プレイヤーがダメージを受けたらカメラシェイク
	if (player_->JustTookDamage()) {
		StartCameraShake(0.5f, 0.3f);  // 強さ0.5、0.3秒
		player_->ClearJustTookDamage();
	}

	// ダメージ演出をHPに応じて適用
	Game::GetInstance()->GetPostEffect()->ApplyDamageEffect(player_->GetDamageRatio());

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

	operation_->Update();
	hpGaugeBack_->Update();
	hpGaugeFill_->Update();
	hpGaugeBackEnemy_->Update();
	hpGaugeFillEnemy_->Update();
	ammoGaugeBack_->Update();
	ammoGaugeFill_->Update();
	reloadGauge_->Update();

	// パーティクル更新
	ParticleManager::GetInstance()->Update();
}

void GameScene::Draw() {

	// 3Dオブジェクトの共通描画設定
	object3DManager_->DrawSetting();
	LightManager::GetInstance()->BindLights(dxCore_->GetCommandList());
#ifdef _DEBUG
	//LightManager::GetInstance()->OnImGui();
#endif // !_DEBUG


	// ===== 天球描画（最初に描画、深度テスト無効推奨） =====
	skydome_->Draw(dxCore_);

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
	operation_->Draw();
	for (const auto& s : sprites_) {
		s->Draw();
	}

	// ===== UI描画 =====
	DrawUI();
	
	// パーティクルのImGui表示
#ifdef _DEBUG
	ParticleManager::GetInstance()->OnImGui();

	// カメラのImGui
	camera_->OnImGui();
#endif // _DEBUG

}

// =============================================================
// カメラシェイク
// =============================================================
void GameScene::StartCameraShake(float intensity, float duration) {
	cameraShakeIntensity_ = intensity;
	cameraShakeDuration_ = duration;
	cameraShakeTimer_ = duration;
}

void GameScene::UpdateCameraShake(float deltaTime) {
	if (cameraShakeTimer_ > 0.0f) {
		cameraShakeTimer_ -= deltaTime;

		// 残り時間に応じて強度を減衰
		float ratio = cameraShakeTimer_ / cameraShakeDuration_;
		float currentIntensity = cameraShakeIntensity_ * ratio;

		// ランダムなオフセットを生成
		cameraShakeOffset_.x = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * currentIntensity;
		cameraShakeOffset_.y = ((float)std::rand() / RAND_MAX - 0.5f) * 2.0f * currentIntensity;
		cameraShakeOffset_.z = ((float)std::rand() / RAND_MAX - 0.5f) * currentIntensity * 0.5f;

		if (cameraShakeTimer_ <= 0.0f) {
			cameraShakeTimer_ = 0.0f;
			cameraShakeOffset_ = { 0.0f, 0.0f, 0.0f };
		}
	}
}

// =============================================================
// UI描画
// =============================================================
void GameScene::DrawUI() {
	
	// プレイヤーHPゲージ（左上）
	DrawHPGauge(20.0f, 20.0f, 200.0f, 20.0f, 
		player_->GetHP(), player_->GetMaxHP(), true);

	// エネミーHPゲージ（右上）
	DrawHPGauge(1060.0f, 20.0f, 200.0f, 20.0f, 
		enemy_->GetHP(), enemy_->GetMaxHP(), false);

	// 弾数ゲージ（左下）
	DrawAmmoGauge(20.0f, 680.0f);

	// 操作説明（右下）
	DrawControlsUI();
}

void GameScene::DrawHPGauge(float x, float y, float width, float height,
	int currentHP, int maxHP, bool isPlayer) {

	float hpRatio = static_cast<float>(currentHP) / static_cast<float>(maxHP);
	hpRatio = std::clamp(hpRatio, 0.0f, 1.0f);

	int hpPercent = static_cast<int>(hpRatio * 100.0f);

	// HP残量で色を決定
	float r, g, b;
	if (hpPercent >= 60) {
		// 60~100%: 緑
		r = 0.2f;  g = 0.8f;  b = 0.2f;
	} else if (hpPercent >= 30) {
		// 30~59%: 黄色
		r = 1.0f;  g = 0.8f;  b = 0.0f;
	} else {
		// 0~29%: 赤
		r = 1.0f;  g = 0.2f;  b = 0.2f;
	}

	if (isPlayer) {
		// 背景
		hpGaugeBack_->SetPosition({ x, y });
		hpGaugeBack_->SetSize({ 200.0f, 20.0f });
		hpGaugeBack_->SetColor({ 0.1f, 0.1f, 0.1f, 0.9f });
		hpGaugeBack_->Draw();

		// ゲージ
		hpGaugeFill_->SetPosition({ x + 2.0f, y + 2.0f });
		hpGaugeFill_->SetSize({ 196.0f * hpRatio, 16.0f });
		hpGaugeFill_->SetColor({ r, g, b, 1.0f });
		hpGaugeFill_->Draw();
	} else {
		// エネミー用（赤固定にしたい場合はここで色を変更）
		hpGaugeBackEnemy_->SetPosition({ x, y });
		hpGaugeBackEnemy_->SetSize({ 200.0f, 20.0f });
		hpGaugeBackEnemy_->SetColor({ 0.1f, 0.1f, 0.1f, 0.9f });
		hpGaugeBackEnemy_->Draw();

		hpGaugeFillEnemy_->SetPosition({ x + 2.0f, y + 2.0f });
		hpGaugeFillEnemy_->SetSize({ 196.0f * hpRatio, 16.0f });
		hpGaugeFillEnemy_->SetColor({ 0.8f, 0.2f, 0.2f, 1.0f }); // 赤固定
		hpGaugeFillEnemy_->Draw();
	}
}

void GameScene::DrawAmmoGauge(float x, float y) {
	float ammoRatio = static_cast<float>(player_->GetCurrentAmmo()) / 
		static_cast<float>(player_->GetMaxAmmo());
	ammoRatio = std::clamp(ammoRatio, 0.0f, 1.0f);

	const float width = 150.0f;
	const float height = 15.0f;

	// 背景
	ammoGaugeBack_->SetPosition({ x, y });
	ammoGaugeBack_->SetSize({ width, height });
	ammoGaugeBack_->SetColor({ 0.2f, 0.2f, 0.2f, 0.8f });
	ammoGaugeBack_->Draw();

	// リロード中かどうかで表示を切り替え
	if (player_->IsReloading()) {
		// リロード進行度
		float reloadRatio = player_->GetReloadProgress();
		reloadGauge_->SetPosition({ x + 2.0f, y + 2.0f });
		reloadGauge_->SetSize({ (width - 4.0f) * reloadRatio, height - 4.0f });
		reloadGauge_->SetColor({ 1.0f, 0.8f, 0.0f, 1.0f }); // 黄色
		reloadGauge_->Draw();
	} else {
		// 弾数ゲージ（青系）
		ammoGaugeFill_->SetPosition({ x + 2.0f, y + 2.0f });
		ammoGaugeFill_->SetSize({ (width - 4.0f) * ammoRatio, height - 4.0f });
		ammoGaugeFill_->SetColor({ 0.2f, 0.5f, 1.0f, 1.0f });
		ammoGaugeFill_->Draw();
	}

	// TODO: 弾数テキスト表示（フォント実装が必要）
	// 例: "10 / 10" や "RELOADING..."
}

void GameScene::DrawControlsUI() {
	// 操作説明は画面右下に固定テキストで表示
	// フォント描画が実装されている場合はここで描画
	// 現状はスプライトベースで簡易表示、または後で実装

	// TODO: 以下のようなテキストを表示
	// "A/D: Move"
	// "W: Jump / Glide"
	// "SPACE: Shoot"
	// "R: Reload"
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

			// ===== ノックバック =====
			// 弾の進行方向を基にノックバック方向を計算
			Vector3 knockbackDir = {
				bullet->GetPosition().x - player_->GetPosition().x,
				0.0f,
				0.0f
			};
			// 正規化
			float len = std::abs(knockbackDir.x);
			if (len > 0.01f) {
				knockbackDir.x /= len;
			} else {
				knockbackDir.x = 1.0f;  // デフォルトは右方向
			}

			// ダメージに応じたノックバック力
			float knockbackForce = static_cast<float>(bullet->GetDamage()) * 0.3f;
			enemy_->ApplyKnockback(knockbackDir, knockbackForce);

			// ヒットエフェクト（パーティクル）
			ParticleManager::GetInstance()->Emit("circle", bullet->GetPosition(), 5);
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

			// ===== ノックバック =====
			Vector3 knockbackDir = {
				bullet->GetPosition().x - enemy_->GetPosition().x,
				0.0f,
				0.0f
			};
			float len = std::abs(knockbackDir.x);
			if (len > 0.01f) {
				knockbackDir.x /= len;
			} else {
				knockbackDir.x = -1.0f;  // デフォルトは左方向
			}

			float knockbackForce = static_cast<float>(bullet->GetDamage()) * 0.2f;
			player_->ApplyKnockback(knockbackDir, knockbackForce);

			// ヒットエフェクト
			ParticleManager::GetInstance()->Emit("circle", bullet->GetPosition(), 3);
		}
	}

	// === プレイヤー vs エネミー 体当たり（AABB vs AABB） ===
	// 必要ならここに追加
	// if (CollisionHelper::IsCollisionAABBAABB(playerCollider_, enemyCollider_)) {
	//     // 押し返し処理など
	// }
}
