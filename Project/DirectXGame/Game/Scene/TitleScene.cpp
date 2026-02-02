#include "TitleScene.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "SpriteManager.h"
#include "SpriteInstance.h"
#include "InputManager.h"
#include "KeyboardInput.h"
#include "Debug.h"
#include"StripeTransition.h"

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

	// スプライトをvectorで管理
	sprites_.clear();

	// タイトルスプライト（背景）
	// 交互に使うスプライト
	const std::string textures[3] = {
		"Resources/uvChecker.png",
		"Resources/monsterBall.png",
		"Resources/stripe.png"
	};

	{
		auto sprite = std::make_unique<SpriteInstance>();
		sprite->Initialize(spriteManager_,textures[1], "TitleBG");
		sprite->SetSize({ 300.0f, 300.0f });
		sprite->SetPosition({ 0.0f, 0.0f });
		sprites_.push_back(std::move(sprite));
	}

	// 5枚生成
	for (uint32_t i = 0; i < 5; ++i) {
		auto newSprite = std::make_unique<SpriteInstance>();
		const std::string& texturePath = textures[2];

		std::string spriteName = "Sprite_" + std::to_string(i);
		newSprite->Initialize(spriteManager_, texturePath, spriteName);

		newSprite->SetSize({ 100.0f, 100.0f });
		newSprite->SetPosition({ i * 2.0f, 0.0f });
		sprites_.push_back(std::move(newSprite));
	}

	// テスト用スプライト
	char buf[128];
	sprintf_s(buf, "TitleScene: %zu sprites initialized\n", sprites_.size());
	OutputDebugStringA(buf);
}

void TitleScene::Finalize() {
	sprites_.clear();
}

void TitleScene::Update() {
	// トランジション中は入力を受け付けない
	if (SceneManager::GetInstance()->IsTransitioning()) {
		// スプライトの更新は行う
		for (auto& sprite : sprites_) {
			sprite->Update();
		}
		return;
	}

	//// 1キー: ストライプトランジションでゲームシーンへ
	//if (input_->GetKeyboard()->TriggerKey(DIK_1)) {
	//	SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Stripe);
	//	return;
	//}

	//// 2キー: フェードトランジションでゲームシーンへ
	//if (input_->GetKeyboard()->TriggerKey(DIK_2)) {
	//	SceneManager::GetInstance()->ChangeScene("GAMEPLAY", TransitionType::Fade);
	//	return;
	//}

	// SPACE: フェードトランジションでゲームシーンへ
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		SceneManager::GetInstance()->ChangeScene("CHARACTERSELECT", TransitionType::Fade);
		return;
	}

	// スプライトの更新
	for (auto& sprite : sprites_) {
		sprite->Update();
	}
}

void TitleScene::Draw() {
	spriteManager_->DrawSetting();

	// スプライトの描画
	for (auto& sprite : sprites_) {
		sprite->Draw();
	}

	// ストライプトランジションのスプライトを直接取得して描画
	auto* stripeTransition = TransitionManager::GetInstance()->GetTransition<StripeTransition>(TransitionType::Stripe);
	if (stripeTransition) {
		stripeTransition->Draw();
	}
}