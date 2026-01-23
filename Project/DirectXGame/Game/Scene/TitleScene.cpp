#include "TitleScene.h"
#include "SceneManager.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "InputManager.h"
#include "KeyboardInput.h"

TitleScene::TitleScene() {
}

TitleScene::~TitleScene() {
}

void TitleScene::Initialize() {
	// タイトルスプライトの初期化（uvChecker）
	titleSprite_ = new SpriteInstance();
	titleSprite_->Initialize(spriteManager_, "Resources/uvChecker.png");

	// 画面全体に表示
	titleSprite_->SetPosition({ 0.0f, 0.0f });
	titleSprite_->SetSize({ 1280.0f, 720.0f });
}

void TitleScene::Finalize() {
	// スプライトの解放
	if (titleSprite_) {
		delete titleSprite_;
		titleSprite_ = nullptr;
	}
}

void TitleScene::Update() {
	// 0キーでゲームシーンへ遷移
	if (input_->GetKeyboard()->TriggerKey(DIK_0)) {
		// シーン名を文字列で指定
		SceneManager::GetInstance()->ChangeScene("GAMEPLAY");
		return;
	}

	// スプライトの更新
	if (titleSprite_) {
		titleSprite_->Update();
	}
}

void TitleScene::Draw() {
	// スプライト描画設定
	spriteManager_->DrawSetting();

	// タイトルスプライトの描画
	if (titleSprite_) {
		titleSprite_->Draw();
	}
}