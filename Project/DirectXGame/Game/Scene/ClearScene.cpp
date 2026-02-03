#include "ClearScene.h"

#include "InputManager.h"
#include "KeyboardInput.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include"Game.h"

ClearScene::~ClearScene() = default;

void ClearScene::Initialize()
{
	Game::GetPostEffect()->ResetEffects();
	// スプライトの初期化
	sprite_ = std::make_unique<SpriteInstance>();
	sprite_->Initialize(spriteManager_, "Resources/CLEAR.png");
	sprite_->SetSize({ 1280.0f,720.0f });
}

void ClearScene::Finalize()
{
}

void ClearScene::Update()
{
	// --- キーボード入力 ---
	if (input_->GetKeyboard()->TriggerKey(DIK_SPACE)) {
		SceneManager::GetInstance()->ChangeScene("TITLE", TransitionType::Fade);
		return;
	}

	sprite_->Update();
}

void ClearScene::Draw()
{
	// スプライト描画
	spriteManager_->DrawSetting();
	sprite_->Draw();
}
