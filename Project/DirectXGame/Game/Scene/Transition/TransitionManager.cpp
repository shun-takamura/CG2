#include "TransitionManager.h"
#include "StripeTransition.h"
#include "FadeTransition.h"
#include "TextureManager.h"
#include <random>
#include <cassert>
#include <cstdio>
#include <Windows.h>

TransitionManager* TransitionManager::GetInstance() {
	static TransitionManager instance;
	return &instance;
}

void TransitionManager::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;
	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	// トランジション用の白テクスチャを生成（トランジション初期化より先に！）
	TextureManager::GetInstance()->CreateSolidColorTexture("Resources/white1x1.png", 255, 255, 255, 255);

	// テクスチャが正しく生成されたか確認
	if (!TextureManager::GetInstance()->HasTexture("Resources/white1x1.png")) {
		OutputDebugStringA("TransitionManager: Failed to create white texture!\n");
		return;
	}

	// デフォルトのトランジションを登録
	auto stripe = std::make_unique<StripeTransition>();
	stripe->Initialize(spriteManager, dxCore, screenWidth, screenHeight);
	RegisterTransition(TransitionType::Stripe, std::move(stripe));

	auto fade = std::make_unique<FadeTransition>();
	fade->Initialize(spriteManager, dxCore, screenWidth, screenHeight);
	RegisterTransition(TransitionType::Fade, std::move(fade));

	// 履歴の初期化
	lastRecord_ = { TransitionType::None, "", "", "" };
}

void TransitionManager::Finalize() {
	for (auto& pair : transitions_) {
		pair.second->Finalize();
	}
	transitions_.clear();
	history_.clear();
	currentTransition_ = nullptr;
}

void TransitionManager::Update() {
	if (currentTransition_) {
		currentTransition_->Update();

		// トランジション完了したらcurrentTransitionをクリア
		if (!currentTransition_->IsTransitioning()) {
			currentTransition_ = nullptr;
		}
	}
}

void TransitionManager::Draw() {
	// デバッグ出力
	static int drawCount = 0;
	drawCount++;
	if (drawCount % 60 == 0) {
		char buf[128];
		sprintf_s(buf, "TransitionManager::Draw - currentTransition_=%s\n",
			currentTransition_ ? "exists" : "null");
		OutputDebugStringA(buf);
	}

	if (currentTransition_) {
		currentTransition_->Draw();
	}
}

void TransitionManager::RegisterTransition(TransitionType type, std::unique_ptr<BaseTransition> transition) {
	transitions_[type] = std::move(transition);
}

void TransitionManager::StartTransition(TransitionType type, const std::string& fromScene,
	const std::string& toScene, std::function<void()> onSceneChange) {

	// デバッグ出力
	OutputDebugStringA("=== StartTransition called ===\n");
	if (type == TransitionType::Stripe) {
		OutputDebugStringA("Type: Stripe\n");
	} else if (type == TransitionType::Fade) {
		OutputDebugStringA("Type: Fade\n");
	} else {
		OutputDebugStringA("Type: Unknown\n");
	}

	// 既にトランジション中なら無視
	if (IsTransitioning()) {
		OutputDebugStringA("Already transitioning, ignored\n");
		return;
	}

	auto it = transitions_.find(type);
	if (it == transitions_.end()) {
		// 指定されたトランジションが見つからない場合はコールバックを直接実行
		OutputDebugStringA("Transition not found!\n");
		if (onSceneChange) {
			onSceneChange();
		}
		return;
	}

	OutputDebugStringA("Transition found, starting...\n");
	currentTransition_ = it->second.get();

	// 記録を保存
	lastRecord_ = {
		type,
		currentTransition_->GetName(),
		fromScene,
		toScene
	};
	history_.push_back(lastRecord_);

	// トランジション開始
	currentTransition_->Start(onSceneChange);
	OutputDebugStringA("Transition started!\n");
}

void TransitionManager::StartRandomTransition(const std::string& fromScene,
	const std::string& toScene, std::function<void()> onSceneChange) {
	if (transitions_.empty()) {
		if (onSceneChange) {
			onSceneChange();
		}
		return;
	}

	// 利用可能なトランジションをリストアップ
	std::vector<TransitionType> available = GetAvailableTransitions();

	if (available.empty()) {
		if (onSceneChange) {
			onSceneChange();
		}
		return;
	}

	// ランダムに選択
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<> dist(0, static_cast<int>(available.size()) - 1);

	TransitionType selectedType = available[dist(gen)];
	StartTransition(selectedType, fromScene, toScene, onSceneChange);
}

bool TransitionManager::IsTransitioning() const {
	return currentTransition_ != nullptr && currentTransition_->IsTransitioning();
}

TransitionType TransitionManager::GetCurrentTransitionType() const {
	if (currentTransition_) {
		return currentTransition_->GetType();
	}
	return TransitionType::None;
}

std::vector<TransitionType> TransitionManager::GetAvailableTransitions() const {
	std::vector<TransitionType> available;
	for (const auto& pair : transitions_) {
		available.push_back(pair.first);
	}
	return available;
}

void TransitionManager::OnImGui() {
	// StripeTransitionのOnImGuiを呼ぶ
	auto* stripe = GetTransition<StripeTransition>(TransitionType::Stripe);
	if (stripe) {
		stripe->OnImGui();
	}
}