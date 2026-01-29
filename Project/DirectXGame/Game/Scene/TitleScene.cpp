#include "TitleScene.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Debug.h"

TitleScene::TitleScene() {
}

TitleScene::~TitleScene() {
}

void TitleScene::Initialize() {
	// 前のシーンからどのトランジションで来たか確認
	TransitionType lastType = SceneManager::GetInstance()->GetLastUsedTransitionType();
	std::string lastName = SceneManager::GetInstance()->GetLastUsedTransitionName();

	if (lastType != TransitionType::None) {
		Debug::Log("Arrived via transition: " + lastName);
	}

	// タイトルスプライトの初期化
	titleSprite_ = std::make_unique<SpriteInstance>();
	titleSprite_->Initialize(spriteManager_, "Resources/uvChecker.png");
	titleSprite_->SetPosition({ 0.0f, 0.0f });
	titleSprite_->SetSize({ 640.0f, 360.0f });

	testStripe_ = std::make_unique<SpriteInstance>();
	testStripe_->Initialize(spriteManager_, "Resources/monsterBall.png");
	testStripe_->SetPosition({ 700.0f, 100.0f });
	testStripe_->SetSize({ 200.0f, 200.0f });
}

void TitleScene::Finalize() {
	if (titleSprite_) {
		titleSprite_ = nullptr;
	}

	if (testStripe_) {
		testStripe_ = nullptr;
	}
}

void TitleScene::Update() {
	
	if (titleSprite_) {
		titleSprite_->Update();
	}

	if (testStripe_) {
		testStripe_->Update();
	}

	// 1キー: ストライプトランジションでゲームシーンへ
	if (input_->GetKeyboard()->TriggerKey(DIK_1)) {
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Stripe);
		return;
	}

	// 2キー: フェードトランジションでゲームシーンへ
	if (input_->GetKeyboard()->TriggerKey(DIK_2)) {
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Fade);
		return;
	}

	// 3キー: ランダムトランジションでゲームシーンへ
	if (input_->GetKeyboard()->TriggerKey(DIK_3)) {
		SceneManager::GetInstance()->ChangeSceneRandom("GAMEPLAY");
		return;
	}

	// 0キー: トランジションなしで即時切り替え
	if (input_->GetKeyboard()->TriggerKey(DIK_0)) {
		SceneManager::GetInstance()->ChangeSceneImmediate("GAMEPLAY");
		return;
	}

	if (titleSprite_) {
		titleSprite_->Update();
	}

	if (testStripe_) {
		testStripe_->Update();
	}
}

void TitleScene::Draw() {
	spriteManager_->DrawSetting();

	if (titleSprite_) {
		titleSprite_->Draw();
	}

	if (testStripe_) {
		testStripe_->Draw();
	}

	TransitionManager::GetInstance()->Draw();

}