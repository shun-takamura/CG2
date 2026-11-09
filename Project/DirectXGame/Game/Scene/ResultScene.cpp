#include "ResultScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "TransitionManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "Score/ScoreManager.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include <algorithm>
#include <cstdio>

namespace {
	// 報酬の行の数え上げ（実時間 [s]）
	constexpr float kRewardStart = 0.6f;   // 最初の行が数え始めるまで
	constexpr float kRewardStagger = 0.25f; // 行ごとのずれ
	constexpr float kRewardCountTime = 0.6f; // 1 行が 0 から数え上がる時間
}

ResultScene::ResultScene() = default;
ResultScene::~ResultScene() = default;

void ResultScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	score_ = ScoreManager::GetInstance()->GetScore();
	killCount_ = ScoreManager::GetInstance()->GetKillCount();

	// 獲得した報酬：素材は Loot.json の並び（0 個の物も薄く出して種類があることを見せる）、最後にエーテル
	rewards_.clear();
	const LootCatalog* catalog = LootCatalog::GetInstance();
	const LootLedger* ledger = LootLedger::GetInstance();
	for (int i = 0; i < catalog->GetItemCount(); ++i) {
		const LootItemDef* item = catalog->GetItem(i);
		rewards_.push_back({ item->displayName, item->color, ledger->GetItemCount(item->id), false });
	}
	rewards_.push_back({ catalog->GetEther().displayName, catalog->GetEther().color, ledger->GetEther(), true });
	time_ = 0.0f;
}

float ResultScene::CountUpEndTime() const {
	if (rewards_.empty()) return 0.0f;
	return kRewardStart + kRewardStagger * static_cast<float>(rewards_.size() - 1) + kRewardCountTime;
}

void ResultScene::Finalize() {}

void ResultScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	time_ += dxCore_->GetDeltaTime();

	auto* actions = input_->GetActionMap();
	if (actions && actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		// 数え上げの途中なら、まず最後まで飛ばす
		if (time_ < CountUpEndTime()) {
			time_ = CountUpEndTime();
			return;
		}
		// ポーズの「ステージセレクトに戻る」（⑩）と同じく、コクピットの起動から
		SceneManager::GetInstance()->ChangeScene("STAGESELECT");
		return;
	}

	camera_->Update();
}

void ResultScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float scale = 1.5f;
	const float lineHeight = 60.0f;
	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const float screenH = static_cast<float>(dxCore_->GetUIHeight());
	const Vector4 white{ 1.0f, 1.0f, 1.0f, 1.0f };
	const Vector4 black{ 0.0f, 0.0f, 0.0f, 1.0f };

	char scoreBuf[32];
	std::snprintf(scoreBuf, sizeof(scoreBuf), "SCORE: %d", score_);
	char killBuf[32];
	std::snprintf(killBuf, sizeof(killBuf), "撃破数: %d", killCount_);

	const float scoreW = tr->MeasureWidth(scoreBuf, scale);
	const float killW = tr->MeasureWidth(killBuf, scale);
	const float y = screenH * 0.14f;

	tr->DrawText(scoreBuf, { (screenW - scoreW) * 0.5f, y }, scale, white, 2.0f, black);
	tr->DrawText(killBuf, { (screenW - killW) * 0.5f, y + lineHeight }, scale, white, 2.0f, black);

	// 獲得した報酬（22_ItemDrop.md）：見出し → 素材ごとの行 → エーテルの合計。行は順に出て数え上がる
	const float panelW = 620.0f;
	const float left = (screenW - panelW) * 0.5f;
	const float right = left + panelW;
	float ry = y + lineHeight * 2.0f + 50.0f;
	{
		const char* title = "獲得した報酬";
		const float titleScale = 0.8f;
		const float a = std::clamp(time_ / 0.3f, 0.0f, 1.0f);
		const float tw = tr->MeasureWidth(title, titleScale);
		tr->DrawText(title, { (screenW - tw) * 0.5f, ry }, titleScale, { 1.0f, 0.92f, 0.6f, a }, 2.0f, { 0.0f, 0.0f, 0.0f, a });
		ry += 44.0f;
		tr->DrawRect({ left, ry }, { panelW, 2.0f }, { 1.0f, 0.92f, 0.6f, 0.6f * a });
		ry += 18.0f;
	}
	const float rowScale = 0.75f;
	const float rowHeight = 54.0f;
	for (size_t i = 0; i < rewards_.size(); ++i) {
		const RewardRow& row = rewards_[i];
		const float start = kRewardStart + kRewardStagger * static_cast<float>(i);
		const float appear = std::clamp((time_ - start + 0.2f) / 0.2f, 0.0f, 1.0f);
		if (appear <= 0.0f) { ry += rowHeight; continue; }
		const float progress = std::clamp((time_ - start) / kRewardCountTime, 0.0f, 1.0f);
		const int shown = static_cast<int>(static_cast<float>(row.count) * progress + 0.5f);

		// 0 個の行は薄く（種類があることだけ見せる）
		const float dim = (row.count > 0) ? 1.0f : 0.4f;
		const float a = appear * dim;
		if (row.ether) {
			// エーテルの前に区切りの線
			tr->DrawRect({ left, ry - 8.0f }, { panelW, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.3f * appear });
		}
		tr->DrawRect({ left, ry }, { panelW, rowHeight - 10.0f }, { 0.0f, 0.02f, 0.05f, 0.45f * appear });
		const float mark = 18.0f;
		const float textY = ry + (rowHeight - 10.0f - 32.0f * rowScale) * 0.5f;
		tr->DrawRect({ left + 18.0f, ry + (rowHeight - 10.0f - mark) * 0.5f }, { mark, mark },
			{ row.color.x, row.color.y, row.color.z, a });
		tr->DrawText(row.label, { left + 18.0f + mark + 16.0f, textY }, rowScale, { 1.0f, 1.0f, 1.0f, a }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, a });

		char countBuf[32];
		if (row.ether) std::snprintf(countBuf, sizeof(countBuf), "+%d", shown);
		else std::snprintf(countBuf, sizeof(countBuf), "×%d", shown);
		const float cw = tr->MeasureWidth(countBuf, rowScale);
		tr->DrawText(countBuf, { right - 18.0f - cw, textY }, rowScale, { row.color.x, row.color.y, row.color.z, a }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, a });
		ry += rowHeight;
	}

	// 操作の案内（数え終わってから）
	if (time_ >= CountUpEndTime()) {
		const char* hint = "決定：ステージ選択へ";
		const float hs = 0.55f;
		const float a = std::clamp((time_ - CountUpEndTime()) / 0.3f, 0.0f, 1.0f);
		const float hw = tr->MeasureWidth(hint, hs);
		tr->DrawText(hint, { (screenW - hw) * 0.5f, screenH - 90.0f }, hs, { 1.0f, 1.0f, 1.0f, 0.75f * a }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, a });
	}
	tr->Flush();
}

Camera* ResultScene::GetCamera() {
	return camera_.get();
}
