#include "GameOver.h"

#include "InputManager.h"
#include "KeyboardInput.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include"Game.h"

GameOver::~GameOver() = default;

void GameOver::Initialize()
{
	Game::GetPostEffect()->ResetEffects();
	// スプライトの初期化
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager_, "Resources/GAMEOVER.png");
	sprite_->SetSize({ 1280.0f,720.0f });
}

void GameOver::Finalize()
{
}

void GameOver::Update()
{
	// --- キーボード入力 ---
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		SceneManager::GetInstance()->ChangeScene("TITLE", TransitionType::Fade);
		return;
	}

	sprite_->Update();
}

void GameOver::Draw()
{
	// スプライト描画
	spriteManager_->DrawSetting();
	sprite_->Draw();
}
