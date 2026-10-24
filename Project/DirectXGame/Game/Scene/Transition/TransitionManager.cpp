#include "TransitionManager.h"
#include "StripeTransition.h"
#include "FadeTransition.h"
#include "InkTransition.h"
#include "FeatherLandTransition.h"
#include "FeatherRiseTransition.h"
#include "TextureManager.h"
#include "WindowsApplication.h"
#include "Json/JsonParser.h"
#include "Json/JsonValue.h"
#include "Json/JsonWriter.h"
#include <filesystem>
#include <random>
#include <cassert>
#include <cstdio>
#include <Windows.h>

#ifdef _DEBUG
#include "imgui.h"
#endif

namespace {
	constexpr const char* kTuningPath = "Resources/Json/Tuning/Transition.json";
}

TransitionManager* TransitionManager::GetInstance() {
	static TransitionManager instance;
	return &instance;
}

void TransitionManager::Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
	float screenWidth, float screenHeight) {
	spriteManager_ = spriteManager;
	dxCore_ = dxCore;

	// 0 以下なら実際のクライアントサイズを使う。
	// 以前は 1280x720 が既定値だったため、1600x900 のウィンドウでは
	// 画面の右下がフェードで覆われないバグになっていた。
	if (screenWidth <= 0.0f) {
		screenWidth = static_cast<float>(WindowsApplication::kUIWidth);
	}
	if (screenHeight <= 0.0f) {
		screenHeight = static_cast<float>(WindowsApplication::kUIHeight);
	}

	screenWidth_ = screenWidth;
	screenHeight_ = screenHeight;

	// トランジション用の白テクスチャを生成（トランジション初期化より先に！）
	TextureManager::GetInstance()->CreateSolidColorTexture("Resources/Textures/white1x1.dds", 255, 255, 255, 255);

	// テクスチャが正しく生成されたか確認
	if (!TextureManager::GetInstance()->HasTexture("Resources/Textures/white1x1.dds")) {
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

	auto ink = std::make_unique<InkTransition>();
	ink->Initialize(spriteManager, dxCore, screenWidth, screenHeight);
	RegisterTransition(TransitionType::Ink, std::move(ink));

	auto featherLand = std::make_unique<FeatherLandTransition>();
	featherLand->Initialize(spriteManager, dxCore, screenWidth, screenHeight);
	RegisterTransition(TransitionType::FeatherLand, std::move(featherLand));

	auto featherRise = std::make_unique<FeatherRiseTransition>();
	featherRise->Initialize(spriteManager, dxCore, screenWidth, screenHeight);
	RegisterTransition(TransitionType::FeatherRise, std::move(featherRise));

	// 履歴の初期化
	lastRecord_ = { TransitionType::None, "", "", "" };

	LoadTuning();
}

void TransitionManager::LoadTuning() {
	if (!std::filesystem::exists(kTuningPath)) return;
	const auto result = JsonParser::ParseFile(kTuningPath);
	if (!result.success) return;
	for (auto& pair : transitions_) {
		const std::string name = pair.second->GetName();
		if (result.value.Contains(name)) {
			pair.second->LoadTuning(result.value[name]);
		}
	}
}

void TransitionManager::SaveTuning() const {
	JsonValue root = JsonValue::MakeObject();
	for (const auto& pair : transitions_) {
		JsonValue json = JsonValue::MakeObject();
		pair.second->SaveTuning(json);
		// 調整値を持たないトランジション（Stripe / Fade）は欄を作らない
		if (json.IsObject() && json.Size() > 0) {
			root[pair.second->GetName()] = std::move(json);
		}
	}
	JsonWriter::WriteFile(kTuningPath, root, { true, 2 });
}

void TransitionManager::Finalize() {
	for (auto& pair : transitions_) {
		pair.second->Finalize();
	}
	transitions_.clear();
	history_.clear();
	currentTransition_ = nullptr;
	pendingReveal_ = nullptr;
}

void TransitionManager::Update() {
	if (!currentTransition_) return;

	currentTransition_->Update();
	if (currentTransition_->IsTransitioning()) return;

	// 終わった。覆う側と開く側が別なら、同じフレームのうちに開く側を覆い切った所から始める
	// （Draw の前に引き継ぐので、覆いが1フレームも途切れない）
	currentTransition_ = pendingReveal_;
	pendingReveal_ = nullptr;
	if (currentTransition_) {
		currentTransition_->StartReveal();
		if (skipPendingReveal_) currentTransition_->RequestSkip();
	}
	skipPendingReveal_ = false;
}

bool TransitionManager::IsSkippable() const {
	if (!currentTransition_ || !currentTransition_->IsTransitioning()) return false;
	// 何度も見るトランジションだけ（死ぬたびに見る羽根の着水）
	return currentTransition_->GetType() == TransitionType::FeatherLand;
}

void TransitionManager::RequestSkip() {
	if (!IsSkippable()) return;
	currentTransition_->RequestSkip();
	if (pendingReveal_) skipPendingReveal_ = true;
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
	StartTransition(type, type, fromScene, toScene, std::move(onSceneChange));
}

void TransitionManager::StartTransition(TransitionType coverType, TransitionType revealType,
	const std::string& fromScene, const std::string& toScene, std::function<void()> onSceneChange) {
	const TransitionType type = coverType;

#ifdef DEBUG

	// デバッグ出力
	OutputDebugStringA("=== StartTransition called ===\n");
	if (type == TransitionType::Stripe) {
		OutputDebugStringA("Type: Stripe\n");
	} else if (type == TransitionType::Fade) {
		OutputDebugStringA("Type: Fade\n");
	} else {
		OutputDebugStringA("Type: Unknown\n");
	}

#endif // DEBUG
	// 既にトランジション中なら無視
	if (IsTransitioning()) {
		OutputDebugStringA("Already transitioning, ignored\n");
		return;
	}

	if (coverType == kCoverByScene) {
		StartCoveredByScene(revealType, fromScene, toScene, std::move(onSceneChange));
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

	// 開く側：同じ種類なら覆う側がそのまま開く。kRevealByScene なら開く演出なし（行き先のシーンが自分で開く）。
	// 別の種類が見つからなければ覆う側が開く
	pendingReveal_ = nullptr;
	bool coverPlaysReveal = true;
	if (revealType == kRevealByScene) {
		coverPlaysReveal = false;
	} else if (revealType != coverType) {
		auto revealIt = transitions_.find(revealType);
		if (revealIt != transitions_.end()) {
			pendingReveal_ = revealIt->second.get();
			coverPlaysReveal = false;
		}
	}

	// 記録を保存
	lastRecord_ = {
		type,
		currentTransition_->GetName(),
		fromScene,
		toScene
	};
	history_.push_back(lastRecord_);

	// トランジション開始
	currentTransition_->Start(onSceneChange, coverPlaysReveal);
	OutputDebugStringA("Transition started!\n");
}

void TransitionManager::StartCoveredByScene(TransitionType revealType,
	const std::string& fromScene, const std::string& toScene, std::function<void()> onSceneChange) {
	// 開く側が決まっていればそれを覆い切った所から始め、ロードしてから開く。
	// 開く側もシーンが演じるなら、黒の Fade を「真っ暗のまま差し替える係」にして開かずに終わる
	// （ロードのフレームと Hold の間を黒で押さえる。行き先のシーンは最初の1枚を真っ暗で描く取り決め）
	bool playReveal = true;
	auto it = transitions_.end();
	if (revealType != kRevealByScene) {
		it = transitions_.find(revealType);
	}
	if (it == transitions_.end()) {
		it = transitions_.find(TransitionType::Fade);
		playReveal = false;
	}
	if (it == transitions_.end()) {
		if (onSceneChange) onSceneChange();
		return;
	}

	currentTransition_ = it->second.get();
	pendingReveal_ = nullptr;

	lastRecord_ = { kCoverByScene, "(scene)", fromScene, toScene };
	history_.push_back(lastRecord_);

	currentTransition_->StartCovered(std::move(onSceneChange), playReveal);
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

TransitionState TransitionManager::GetPhase() const {
	return IsTransitioning() ? currentTransition_->GetState() : TransitionState::None;
}

bool TransitionManager::IsCovering() const {
	const TransitionState phase = GetPhase();
	return phase == TransitionState::FadeIn || phase == TransitionState::Hold;
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
#ifdef _DEBUG

	// 調整値（墨・羽根の着水・羽根の飛翔）
	if (ImGui::Button("Save Tuning")) SaveTuning();
	ImGui::SameLine();
	if (ImGui::Button("Reload Tuning")) LoadTuning();
	ImGui::SameLine();
	ImGui::TextDisabled("%s", kTuningPath);
	ImGui::Separator();

	// StripeTransitionのOnImGuiを呼ぶ
	auto* stripe = GetTransition<StripeTransition>(TransitionType::Stripe);
	if (stripe) {
		stripe->OnImGui();
	}

	// 欄ごとに ID を分ける（どの欄にも「Hold Duration」等の同じ名前の項目があるため）
	if (auto* ink = GetTransition<InkTransition>(TransitionType::Ink)) {
		ImGui::Separator();
		ImGui::PushID("Ink");
		if (ImGui::CollapsingHeader("Ink", ImGuiTreeNodeFlags_DefaultOpen)) {
			ink->OnImGui();
		}
		ImGui::PopID();
	}
	if (auto* featherLand = GetTransition<FeatherLandTransition>(TransitionType::FeatherLand)) {
		ImGui::Separator();
		ImGui::PushID("FeatherLand");
		if (ImGui::CollapsingHeader("FeatherLand")) {
			featherLand->OnImGui();
		}
		ImGui::PopID();
	}
	if (auto* featherRise = GetTransition<FeatherRiseTransition>(TransitionType::FeatherRise)) {
		ImGui::Separator();
		ImGui::PushID("FeatherRise");
		if (ImGui::CollapsingHeader("FeatherRise")) {
			featherRise->OnImGui();
		}
		ImGui::PopID();
	}

#endif // _DEBUG
}