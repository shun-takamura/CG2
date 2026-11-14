#include "ResultScene.h"

#include "Camera.h"
#include "Object3DManager.h"
#include "SceneManager.h"
#include "InputManager.h"
#include "InputAction.h"
#include "Config/GameActions.h"
#include "Game.h"
#include "DirectXCore.h"
#include "TextRenderer.h"
#include "SpriteInstance.h"
#include "SpriteManager.h"
#include "SoundManager.h"
#include "UIPointer.h"
#include "Score/ScoreManager.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include "Score/EtherState.h"
#include "Score/StageClearRecord.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <iterator>
#include <string_view>

namespace {
	// ----- 出し方（実時間 [s]） -----
	constexpr float kRecordAppear = 0.25f;   // 左上の記録が出始める
	constexpr float kPanelAppear = 0.55f;    // 右上の所持・右のパネルが出始める
	constexpr float kAppearTime = 0.35f;     // 出てくる長さ（横から滑り込む）
	constexpr float kSlideDistance = 40.0f;  // 滑り込む距離
	constexpr float kSlotStart = 0.9f;       // 最初のマスが数え始める
	constexpr float kSlotStagger = 0.12f;    // マスごとのずれ
	constexpr float kSlotCountTime = 0.35f;  // 1 マスが 0 から数え上がる時間
	constexpr float kEtherStart = 0.9f;      // 獲得エーテルが数え始める
	constexpr float kEtherCountTime = 1.0f;
	constexpr float kZoomTime = 10.0f;       // 背景が寄りきるまで
	constexpr float kZoomAmount = 0.05f;     // 寄る量（1.0 → 1.05 倍）
	constexpr float kSoldPopupTime = 1.6f;   // 売ったときの「+N」を出す長さ

	// ----- 配置（UI 座標 1600×900） -----
	constexpr float kRecordX = 40.0f;
	constexpr float kRecordY = 40.0f;
	constexpr float kRecordW = 520.0f;
	constexpr float kRecordH = 300.0f;
	constexpr float kEarnedY = kRecordY + kRecordH + 12.0f;   // 獲得エーテル（記録の下）
	constexpr float kEarnedH = 72.0f;
	constexpr float kPanelX = 1020.0f;       // 右のパネルの左端
	constexpr float kPanelW = 540.0f;
	constexpr float kHandY = 40.0f;          // 所持エーテル（右上）
	constexpr float kHandH = 80.0f;
	constexpr float kPanelY = 140.0f;
	constexpr float kPanelH = 650.0f;
	constexpr float kSlotSize = 78.0f;
	constexpr float kSlotGap = 8.0f;
	constexpr int kSlotColumns = 6;
	constexpr int kStageSlotRows = 2;        // ステージ報酬のマスの段（越えたら段を足す）
	constexpr int kPartBreakSlotRows = 1;
	constexpr float kButtonH = 44.0f;
	constexpr int kHandDisplayMax = 99999;   // 所持の表示の上限（賭ける画面と同じ）

	const Vector4 kGold{ 1.0f, 0.88f, 0.55f, 1.0f };
	const Vector4 kPanelColor{ 0.0f, 0.02f, 0.05f, 0.55f };

	float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
	float EaseOutCubic(float t) { t = 1.0f - Clamp01(t); return 1.0f - t * t * t; }
	Vector4 WithAlpha(const Vector4& c, float a) { return { c.x, c.y, c.z, c.w * a }; }
	int RowsFor(size_t count, int minRows) {
		return (std::max)(minRows, (static_cast<int>(count) + kSlotColumns - 1) / kSlotColumns);
	}
	float GridHeight(int rows) { return rows * kSlotSize + (rows - 1) * kSlotGap; }

	/// <summary>レア度の色（1 白 → 2 緑 → 3 青 → 4 紫 → 5 橙）</summary>
	Vector4 RarityColor(int rarity) {
		switch (rarity) {
		case 2: return { 0.55f, 0.95f, 0.55f, 1.0f };
		case 3: return { 0.45f, 0.75f, 1.0f, 1.0f };
		case 4: return { 0.8f, 0.55f, 1.0f, 1.0f };
		case 5: return { 1.0f, 0.62f, 0.25f, 1.0f };
		default: return { 0.92f, 0.92f, 0.92f, 1.0f };
		}
	}

	/// <summary>"\n" で行に分ける（TextRenderer は改行を扱わない）</summary>
	std::vector<std::string_view> SplitLines(std::string_view text) {
		std::vector<std::string_view> lines;
		size_t begin = 0;
		while (begin <= text.size()) {
			const size_t end = text.find('\n', begin);
			if (end == std::string_view::npos) {
				lines.push_back(text.substr(begin));
				break;
			}
			lines.push_back(text.substr(begin, end - begin));
			begin = end + 1;
		}
		return lines;
	}

	/// <summary>3 桁ごとにカンマ</summary>
	std::string WithCommas(int value) {
		std::string digits = std::to_string(value < 0 ? -value : value);
		std::string out;
		for (size_t i = 0; i < digits.size(); ++i) {
			if (i > 0 && (digits.size() - i) % 3 == 0) out.push_back(',');
			out.push_back(digits[i]);
		}
		return value < 0 ? "-" + out : out;
	}

	bool Triggered(InputActionMap* actions, Action a) {
		return actions && actions->IsTriggered(static_cast<int>(a));
	}
}

ResultScene::ResultScene() = default;
ResultScene::~ResultScene() = default;

void ResultScene::Initialize() {
	Game::GetPostEffect()->ResetEffects();

	camera_ = std::make_unique<Camera>();
	camera_->SetTranslate({ 0.0f, 0.0f, -10.0f });
	camera_->SetRotate({ 0.0f, 0.0f, 0.0f });
	object3DManager_->SetDefaultCamera(camera_.get());

	const StageClearRecord* record = StageClearRecord::GetInstance();
	stageName_ = record->GetStageName();
	clearTime_ = record->GetClearTime();
	purity_ = EtherState::GetInstance()->GetDifficulty();
	score_ = ScoreManager::GetInstance()->GetScore();
	killCount_ = ScoreManager::GetInstance()->GetKillCount();
	earnedEther_ = LootLedger::GetInstance()->GetEther();

	BuildRewards();
	SetupCaptureSprite();

	selected_ = (SelectableCount() > 0) ? 0 : ButtonIndex();
	lastColumn_ = 0;
	sellOpen_ = false;
	sellAmount_ = 1;
	soldEtherPopup_ = 0;
	soldPopupTime_ = 99.0f;
	time_ = 0.0f;
	backgroundOnly_ = false;
	uiAlpha_ = 1.0f;
	backgroundToggleTime_ = 0.0f;
}

void ResultScene::BuildRewards() {
	// 手に入れた素材だけをマスにする（Loot.json の並び）。ステージ報酬 → 部位破壊報酬の順に数え上げる
	stageSlots_.clear();
	partBreakSlots_.clear();
	const LootCatalog* catalog = LootCatalog::GetInstance();
	const LootLedger* ledger = LootLedger::GetInstance();
	for (int i = 0; i < catalog->GetItemCount(); ++i) {
		const LootItemDef* item = catalog->GetItem(i);
		const int stageCount = ledger->GetStageItemCount(item->id);
		if (stageCount > 0) stageSlots_.push_back({ item, stageCount, 0.0f });
		const int partCount = ledger->GetPartBreakItemCount(item->id);
		if (partCount > 0) partBreakSlots_.push_back({ item, partCount, 0.0f });
	}
	float t = kSlotStart;
	for (RewardSlot& s : stageSlots_) { s.startTime = t; t += kSlotStagger; }
	for (RewardSlot& s : partBreakSlots_) { s.startTime = t; t += kSlotStagger; }
}

void ResultScene::SetupCaptureSprite() {
	captureSprite_.reset();
	const StageClearRecord* record = StageClearRecord::GetInstance();
	if (!record->HasCapture() || !spriteManager_) return;

	// B が RegisterExternalTexture で登録した名前で引く（§3）。寿命は B の持ち主
	captureSprite_ = std::make_unique<SpriteInstance>();
	captureSprite_->Initialize(spriteManager_, record->GetCaptureTextureName(), "ResultCapture");
	captureSize_ = { static_cast<float>(record->GetCaptureWidth()), static_cast<float>(record->GetCaptureHeight()) };
}

float ResultScene::CountUpEndTime() const {
	float end = kEtherStart + kEtherCountTime;
	for (const RewardSlot& s : stageSlots_) end = (std::max)(end, s.startTime + kSlotCountTime);
	for (const RewardSlot& s : partBreakSlots_) end = (std::max)(end, s.startTime + kSlotCountTime);
	return end;
}

void ResultScene::Finalize() {
	captureSprite_.reset();
}

// ===== 選ぶ =====

int ResultScene::SelectableCount() const {
	return static_cast<int>(stageSlots_.size() + partBreakSlots_.size());
}

ResultScene::RewardSlot* ResultScene::SelectableAt(int index) {
	return const_cast<RewardSlot*>(static_cast<const ResultScene*>(this)->SelectableAt(index));
}

const ResultScene::RewardSlot* ResultScene::SelectableAt(int index) const {
	if (index < 0) return nullptr;
	const size_t i = static_cast<size_t>(index);
	if (i < stageSlots_.size()) return &stageSlots_[i];
	if (i - stageSlots_.size() < partBreakSlots_.size()) return &partBreakSlots_[i - stageSlots_.size()];
	return nullptr;
}

bool ResultScene::IsPartBreakIndex(int index) const {
	return index >= static_cast<int>(stageSlots_.size()) && index < SelectableCount();
}

void ResultScene::Select(int index, bool playSound) {
	index = std::clamp(index, 0, ButtonIndex());
	if (index == selected_) return;
	selected_ = index;
	if (playSound) SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
}

void ResultScene::MoveHorizontal(int dir) {
	// マスの間を順に（ステージ報酬 → 部位破壊報酬、端で折り返す）。「ステージ選択へ」の上では動かない
	const int count = SelectableCount();
	if (count == 0 || selected_ >= count) return;
	const int next = (selected_ + dir + count) % count;
	const int local = IsPartBreakIndex(next) ? next - static_cast<int>(stageSlots_.size()) : next;
	lastColumn_ = local % kSlotColumns;
	Select(next, true);
}

void ResultScene::MoveVertical(int dir) {
	// 段の一覧（中身のある段だけ）：{ 最初の選択番号, マスの数 }。最後に「ステージ選択へ」
	struct Row { int first; int count; };
	std::vector<Row> rows;
	auto addRows = [&rows](int offset, size_t size) {
		for (size_t i = 0; i < size; i += kSlotColumns) {
			rows.push_back({ offset + static_cast<int>(i), static_cast<int>((std::min)(size - i, static_cast<size_t>(kSlotColumns))) });
		}
	};
	addRows(0, stageSlots_.size());
	addRows(static_cast<int>(stageSlots_.size()), partBreakSlots_.size());
	rows.push_back({ ButtonIndex(), 1 });

	int current = static_cast<int>(rows.size()) - 1;
	for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
		if (selected_ >= rows[r].first && selected_ < rows[r].first + rows[r].count) { current = r; break; }
	}
	const int target = current + dir;
	if (target < 0 || target >= static_cast<int>(rows.size())) return;
	const Row& row = rows[static_cast<size_t>(target)];
	if (row.first == ButtonIndex()) {
		Select(ButtonIndex(), true);
		return;
	}
	Select(row.first + (std::min)(lastColumn_, row.count - 1), true);
}

ResultScene::PanelLayout ResultScene::MakeLayout(float slideX) const {
	PanelLayout l;
	const float gridW = kSlotSize * kSlotColumns + kSlotGap * (kSlotColumns - 1);
	l.panelX = kPanelX + slideX;
	l.innerX = l.panelX + (kPanelW - gridW) * 0.5f;
	l.innerW = gridW;
	l.stageHeaderY = kPanelY + 14.0f;
	l.stageGridY = l.stageHeaderY + 42.0f;
	l.stageRows = RowsFor(stageSlots_.size(), kStageSlotRows);
	l.partHeaderY = l.stageGridY + GridHeight(l.stageRows) + 20.0f;
	l.partGridY = l.partHeaderY + 42.0f;
	l.partRows = RowsFor(partBreakSlots_.size(), kPartBreakSlotRows);
	l.descY = l.partGridY + GridHeight(l.partRows) + 20.0f;
	l.buttonY = kPanelY + kPanelH - 16.0f - kButtonH;
	l.descH = l.buttonY - 12.0f - l.descY;
	return l;
}

ResultScene::Rect ResultScene::SlotRect(const PanelLayout& layout, int selectableIndex) const {
	const bool part = IsPartBreakIndex(selectableIndex);
	const int local = part ? selectableIndex - static_cast<int>(stageSlots_.size()) : selectableIndex;
	const float gridY = part ? layout.partGridY : layout.stageGridY;
	return { { layout.innerX + (local % kSlotColumns) * (kSlotSize + kSlotGap), gridY + (local / kSlotColumns) * (kSlotSize + kSlotGap) },
		{ kSlotSize, kSlotSize } };
}

ResultScene::Rect ResultScene::ButtonRect(const PanelLayout& layout) const {
	return { { layout.innerX, layout.buttonY }, { layout.innerW, kButtonH } };
}

// ===== 売る =====

ResultScene::Rect ResultScene::SellArrowRect(const PanelLayout& layout, int dir) const {
	// 売る数の ▲（dir > 0）と ▼（dir < 0）。数の上と下に置く（小窓は説明の欄に重ねて出す）
	const float centerX = layout.innerX + layout.innerW * 0.5f - 90.0f;
	const float y = (dir > 0) ? layout.descY + 46.0f : layout.descY + 106.0f;
	return { { centerX - 40.0f, y }, { 80.0f, 28.0f } };
}

ResultScene::Rect ResultScene::SellButtonRect(const PanelLayout& layout, bool confirm) const {
	const float w = 200.0f;
	const float y = layout.descY + layout.descH - 16.0f - 40.0f;
	const float centerX = layout.innerX + layout.innerW * 0.5f;
	const float x = confirm ? centerX - w - 10.0f : centerX + 10.0f;
	return { { x, y }, { w, 40.0f } };
}

void ResultScene::OpenSell() {
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item || slot->count <= 0) return;
	if (slot->item->sellPrice <= 0) {
		SoundManager::GetInstance()->Play2DSound("se_ui_cancel");   // 売れない素材
		return;
	}
	sellOpen_ = true;
	sellAmount_ = 1;
	sellFocusCancel_ = false;
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
}

void ResultScene::UpdateSell(InputActionMap* actions, const UIPointer& pointer) {
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) {
		sellOpen_ = false;
		return;
	}
	const PanelLayout layout = MakeLayout(0.0f);
	auto changeAmount = [this, slot](int delta) {
		const int next = std::clamp(sellAmount_ + delta, 1, slot->count);
		if (next == sellAmount_) return;
		sellAmount_ = next;
		SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
	};

	auto focus = [this](bool cancelButton) {
		if (sellFocusCancel_ == cancelButton) return;
		sellFocusCancel_ = cancelButton;
		SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
	};

	// ↑↓：売る数、←→：［売る］［やめる］
	if (Triggered(actions, Action::MenuUp)) changeAmount(+1);
	if (Triggered(actions, Action::MenuDown)) changeAmount(-1);
	if (Triggered(actions, Action::MenuLeft)) focus(false);
	if (Triggered(actions, Action::MenuRight)) focus(true);

	// 決定は選んでいるボタン。キャンセルはいつでもやめる
	bool sell = false;
	bool cancel = Triggered(actions, Action::MenuCancel);
	if (Triggered(actions, Action::MenuConfirm)) {
		if (sellFocusCancel_) cancel = true;
		else sell = true;
	}

	// マウス：ボタンに乗せたら選ぶ、クリックで実行。▲▼ のクリックで数を増減
	if (pointer.valid) {
		const bool overSell = SellButtonRect(layout, true).Contains(pointer.position);
		const bool overCancel = SellButtonRect(layout, false).Contains(pointer.position);
		if (pointer.moved || pointer.clicked) {
			if (overSell) focus(false);
			else if (overCancel) focus(true);
		}
		if (pointer.clicked) {
			if (SellArrowRect(layout, +1).Contains(pointer.position)) changeAmount(+1);
			else if (SellArrowRect(layout, -1).Contains(pointer.position)) changeAmount(-1);
			else if (overSell) sell = true;
			else if (overCancel) cancel = true;
		}
	}

	if (sell) {
		ExecuteSell();
	} else if (cancel) {
		sellOpen_ = false;
		SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
	}
}

void ResultScene::ExecuteSell() {
	RewardSlot* slot = SelectableAt(selected_);
	sellOpen_ = false;
	if (!slot || !slot->item) return;
	const int amount = std::clamp(sellAmount_, 1, slot->count);
	const int gain = slot->item->sellPrice * amount;
	if (gain <= 0) return;

	// 所持へ足す（無限の扱いの間は表示は 99,999 のまま。「+N」で見せる）。素材の数はこの画面の表示だけ減らす（集計は展示用）
	EtherState::GetInstance()->AddHand(static_cast<float>(gain));
	soldEtherPopup_ = (soldPopupTime_ < kSoldPopupTime) ? soldEtherPopup_ + gain : gain;
	soldPopupTime_ = 0.0f;
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");

	slot->count -= amount;
	if (slot->count <= 0) {
		// 売りきったマスは消して詰める
		const bool part = IsPartBreakIndex(selected_);
		if (part) partBreakSlots_.erase(partBreakSlots_.begin() + (selected_ - static_cast<int>(stageSlots_.size())));
		else stageSlots_.erase(stageSlots_.begin() + selected_);
		if (SelectableCount() == 0) selected_ = ButtonIndex();
		else selected_ = (std::min)(selected_, SelectableCount() - 1);
	}
}

void ResultScene::GoToStageSelect() {
	SoundManager::GetInstance()->Play2DSound("se_ui_decide");
	// ポーズの「ステージセレクトに戻る」（⑩）と同じく、コクピットの起動から
	SceneManager::GetInstance()->ChangeScene("STAGESELECT");
}

// ===== 更新 =====

void ResultScene::Update() {
	if (SceneManager::GetInstance()->IsTransitioning()) {
		return;
	}

	const float dt = dxCore_->GetDeltaTime();
	time_ += dt;
	backgroundToggleTime_ += dt;
	soldPopupTime_ += dt;
	const float targetAlpha = backgroundOnly_ ? 0.0f : 1.0f;
	uiAlpha_ += (targetAlpha - uiAlpha_) * (std::min)(1.0f, dt * 12.0f);

	camera_->Update();

	InputActionMap* actions = input_->GetActionMap();
	const UIPointer pointer = UIPointer::FromInput(input_);

	// 売る小窓の間は小窓だけが入力を読む
	if (sellOpen_) {
		UpdateSell(actions, pointer);
		return;
	}

	// 背景を見る：キャンセルで UI を隠す ↔ 戻す。隠している間は決定・クリックでも戻る
	if (Triggered(actions, Action::MenuCancel)
		|| (backgroundOnly_ && (Triggered(actions, Action::MenuConfirm) || (pointer.valid && pointer.clicked)))) {
		backgroundOnly_ = !backgroundOnly_;
		backgroundToggleTime_ = 0.0f;
		SoundManager::GetInstance()->Play2DSound(backgroundOnly_ ? "se_ui_cursor" : "se_ui_cancel");
		return;
	}
	if (backgroundOnly_) return;

	const bool confirm = Triggered(actions, Action::MenuConfirm);
	const bool leave = Triggered(actions, Action::Pause);
	const bool click = pointer.valid && pointer.clicked;

	// 数え上げの途中なら、まず最後まで飛ばす
	if (IsCounting()) {
		if (confirm || leave || click) time_ = CountUpEndTime();
		return;
	}

	// 十字キーで選ぶ
	if (Triggered(actions, Action::MenuLeft)) MoveHorizontal(-1);
	if (Triggered(actions, Action::MenuRight)) MoveHorizontal(+1);
	if (Triggered(actions, Action::MenuUp)) MoveVertical(-1);
	if (Triggered(actions, Action::MenuDown)) MoveVertical(+1);

	// マウス：乗せたら選ぶ、クリックで決定
	bool clickedTarget = false;
	if (pointer.valid && (pointer.moved || click)) {
		const PanelLayout layout = MakeLayout(0.0f);
		for (int i = 0; i < SelectableCount(); ++i) {
			if (!SlotRect(layout, i).Contains(pointer.position)) continue;
			const int local = IsPartBreakIndex(i) ? i - static_cast<int>(stageSlots_.size()) : i;
			lastColumn_ = local % kSlotColumns;
			Select(i, true);
			clickedTarget = click;
			break;
		}
		if (ButtonRect(layout).Contains(pointer.position)) {
			Select(ButtonIndex(), true);
			clickedTarget = click;
		}
	}

	if (confirm || clickedTarget) {
		if (selected_ == ButtonIndex()) GoToStageSelect();
		else OpenSell();
		return;
	}
	if (leave) {
		GoToStageSelect();
		return;
	}
}

// ===== 描く =====

void ResultScene::Draw() {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float screenW = static_cast<float>(dxCore_->GetUIWidth());
	const float screenH = static_cast<float>(dxCore_->GetUIHeight());

	DrawBackground(screenW, screenH);

	const float ui = Clamp01(uiAlpha_);
	if (ui > 0.001f) {
		DrawRecordPanel(ui);
		DrawEarnedEther(ui);
		DrawHandEther(ui);
		DrawRewardPanel(ui);
	}
	DrawHint(screenW, screenH, ui);
	tr->Flush();
}

void ResultScene::DrawBackground(float screenW, float screenH) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float zoom = 1.0f + kZoomAmount * EaseOutCubic(time_ / kZoomTime);

	if (captureSprite_ && captureSize_.x > 0.0f && captureSize_.y > 0.0f) {
		// 画面の縦横比で切り抜いて（はみ出す側を削る）、寄る分だけ真ん中を小さく切り抜く。
		// キャプチャは描画解像度（GetRenderWidth/Height）、画面は UI 座標なので画面全体に引き伸ばす（§3）
		const float screenAspect = screenW / screenH;
		Vector2 crop = captureSize_;
		if (crop.x / crop.y > screenAspect) crop.x = crop.y * screenAspect;
		else crop.y = crop.x / screenAspect;
		crop = { crop.x / zoom, crop.y / zoom };
		captureSprite_->SetPosition({ 0.0f, 0.0f });
		captureSprite_->SetSize({ screenW, screenH });
		captureSprite_->SetTextureLeftTop({ (captureSize_.x - crop.x) * 0.5f, (captureSize_.y - crop.y) * 0.5f });
		captureSprite_->SetTextureSize(crop);
		captureSprite_->SetColor({ 1.0f, 1.0f, 1.0f, 1.0f });
		spriteManager_->DrawSetting();
		captureSprite_->Update();
		captureSprite_->Draw();
	} else {
		DrawFallbackBackground(screenW, screenH);
	}

	// 文字を読みやすくする暗がり（背景を見る間は消す）
	const float ui = Clamp01(uiAlpha_);
	if (ui > 0.001f) {
		tr->DrawRect({ 0.0f, 0.0f }, { screenW, screenH }, { 0.0f, 0.0f, 0.0f, 0.22f * ui });
		// 左上と右側を少し濃く（細い帯を重ねてなめらかに）
		constexpr int kStrips = 16;
		for (int i = 0; i < kStrips; ++i) {
			const float t = static_cast<float>(i) / kStrips;
			const float w = 720.0f / kStrips;
			tr->DrawRect({ t * 720.0f, 0.0f }, { w + 1.0f, screenH }, { 0.0f, 0.0f, 0.0f, 0.28f * (1.0f - t) * ui });
			tr->DrawRect({ screenW - (t + 1.0f / kStrips) * 720.0f, 0.0f }, { w + 1.0f, screenH },
				{ 0.0f, 0.0f, 0.0f, 0.28f * (1.0f - t) * ui });
		}
	}
}

void ResultScene::DrawFallbackBackground(float screenW, float screenH) {
	// 撃破の瞬間の 1 枚が無いとき（F2 のデバッグ遷移・B が無い間）：暗い紺の縦グラデーション＋ぼかした光の帯
	TextRenderer* tr = TextRenderer::GetInstance();
	constexpr int kRows = 30;
	const float rowH = screenH / kRows;
	for (int i = 0; i < kRows; ++i) {
		const float t = static_cast<float>(i) / (kRows - 1);
		const Vector4 c{ 0.03f + 0.03f * (1.0f - t), 0.05f + 0.05f * (1.0f - t), 0.10f + 0.08f * (1.0f - t), 1.0f };
		tr->DrawRect({ 0.0f, rowH * i }, { screenW, rowH + 1.0f }, c);
	}

	// 光の帯：細い横長の矩形を中心ほど濃く重ねる。ゆっくり上下に揺れる
	struct Band { float y; float height; Vector4 color; float speed; float phase; };
	const Band bands[] = {
		{ 0.38f, 140.0f, { 0.0f, 0.641f, 1.0f, 0.10f }, 0.21f, 0.0f },   // エーテルの水色
		{ 0.62f, 90.0f, { 1.0f, 0.82f, 0.45f, 0.07f }, 0.17f, 1.7f },    // 金
		{ 0.20f, 60.0f, { 0.6f, 0.75f, 1.0f, 0.05f }, 0.27f, 3.1f },
	};
	constexpr int kLayers = 10;
	for (const Band& b : bands) {
		const float cy = screenH * b.y + std::sin(time_ * b.speed + b.phase) * 18.0f;
		for (int k = 0; k < kLayers; ++k) {
			const float s = 1.0f - static_cast<float>(k) / kLayers;   // 外側ほど太く薄く
			const float h = b.height * s;
			tr->DrawRect({ 0.0f, cy - h * 0.5f }, { screenW, h }, b.color);
		}
	}
}

// ----- 左上：記録 -----

void ResultScene::DrawRecordPanel(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kRecordAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kRecordX - (1.0f - appear) * kSlideDistance;
	const float y = kRecordY;
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, y }, { kRecordW, kRecordH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, y }, { 4.0f, kRecordH }, WithAlpha(kGold, 0.8f * a));

	tr->DrawText("STAGE CLEAR", { x + 24.0f, y + 14.0f }, 0.55f, WithAlpha(kGold, a), 1.5f, shadow);
	tr->DrawText(stageName_, { x + 24.0f, y + 44.0f }, 1.15f, { 1.0f, 1.0f, 1.0f, a }, 2.0f, shadow);
	tr->DrawRect({ x + 24.0f, y + 96.0f }, { kRecordW - 48.0f, 2.0f }, WithAlpha(kGold, 0.6f * a));

	char purityBuf[32];
	std::snprintf(purityBuf, sizeof(purityBuf), "%.1f", purity_);
	char timeBuf[32];
	{
		const int cs = static_cast<int>(clearTime_ * 100.0f);
		const int mm = (std::min)(cs / 6000, 99);
		std::snprintf(timeBuf, sizeof(timeBuf), "%02d' %02d\" %02d", mm, (cs / 100) % 60, cs % 100);
	}
	const std::string scoreText = WithCommas(score_);
	const std::string killText = WithCommas(killCount_);

	struct Row { const char* label; const char* value; };
	const Row rows[] = {
		{ "純度", purityBuf },
		{ "クリアタイム", timeBuf },
		{ "スコア", scoreText.c_str() },
		{ "撃破数", killText.c_str() },
	};
	constexpr float kLabelScale = 0.6f;
	constexpr float kValueScale = 0.8f;
	constexpr float kRowH = 46.0f;
	float ry = y + 112.0f;
	for (size_t i = 0; i < std::size(rows); ++i) {
		// 1 行ずつ少し遅れて出す
		const float rowA = a * Clamp01((time_ - kRecordAppear - 0.08f * static_cast<float>(i)) / 0.25f);
		tr->DrawText(rows[i].label, { x + 28.0f, ry + 6.0f }, kLabelScale, { 0.8f, 0.85f, 0.9f, rowA }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, rowA });
		const float vw = tr->MeasureWidth(rows[i].value, kValueScale);
		tr->DrawText(rows[i].value, { x + kRecordW - 28.0f - vw, ry }, kValueScale, { 1.0f, 1.0f, 1.0f, rowA }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, rowA });
		ry += kRowH;
	}
}

void ResultScene::DrawEarnedEther(float uiAlpha) {
	// 記録の下：このステージで手に入れたエーテル（数え上がる）
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kRecordAppear - 0.35f) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kRecordX - (1.0f - appear) * kSlideDistance;
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, kEarnedY }, { kRecordW, kEarnedH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, kEarnedY }, { 4.0f, kEarnedH }, WithAlpha(ether.color, 0.8f * a));
	DrawItemIcon(ether, { x + 24.0f, kEarnedY + 18.0f }, 36.0f, a);
	tr->DrawText("獲得エーテル", { x + 74.0f, kEarnedY + 22.0f }, 0.6f, { 0.85f, 0.9f, 0.95f, a }, 1.5f, shadow);

	const float progress = Clamp01((time_ - kEtherStart) / kEtherCountTime);
	const int shown = static_cast<int>(static_cast<float>(earnedEther_) * EaseOutCubic(progress) + 0.5f);
	const std::string text = "+" + WithCommas(shown);
	const float scale = 0.95f;
	const float w = tr->MeasureWidth(text, scale);
	tr->DrawText(text, { x + kRecordW - 28.0f - w, kEarnedY + 14.0f }, scale, WithAlpha(ether.color, a), 2.0f, shadow);
}

// ----- 右上：所持エーテル -----

void ResultScene::DrawHandEther(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kPanelAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const float x = kPanelX + (1.0f - appear) * kSlideDistance;
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ x, kHandY }, { kPanelW, kHandH }, WithAlpha(kPanelColor, a));
	tr->DrawRect({ x, kHandY + kHandH - 3.0f }, { kPanelW, 3.0f }, WithAlpha(ether.color, 0.7f * a));
	DrawItemIcon(ether, { x + 20.0f, kHandY + 22.0f }, 36.0f, a);
	tr->DrawText("所持エーテル", { x + 70.0f, kHandY + 26.0f }, 0.6f, { 0.85f, 0.9f, 0.95f, a }, 1.5f, shadow);

	// 所持が無限の扱いの間は 99,999（賭ける画面と同じ）
	const EtherState* state = EtherState::GetInstance();
	const int hand = state->IsInfiniteHand() ? kHandDisplayMax
		: (std::min)(static_cast<int>(std::lround(state->GetHand())), kHandDisplayMax);
	const std::string text = WithCommas(hand);
	const float scale = 1.0f;
	const float w = tr->MeasureWidth(text, scale);
	const float valueX = x + kPanelW - 24.0f - w;
	tr->DrawText(text, { valueX, kHandY + 18.0f }, scale, WithAlpha(ether.color, a), 2.0f, shadow);

	// 売った分の「+N」：数字の左に出て、少し上がりながら消える
	if (soldPopupTime_ < kSoldPopupTime && soldEtherPopup_ > 0) {
		const float t = soldPopupTime_ / kSoldPopupTime;
		const float pa = a * (1.0f - Clamp01((t - 0.6f) / 0.4f)) * Clamp01(soldPopupTime_ / 0.08f);
		const std::string popup = "+" + WithCommas(soldEtherPopup_);
		const float ps = 0.7f;
		const float pw = tr->MeasureWidth(popup, ps);
		tr->DrawText(popup, { valueX - 16.0f - pw, kHandY + 26.0f - 14.0f * EaseOutCubic(t) }, ps,
			{ 1.0f, 0.95f, 0.6f, pa }, 1.5f, { 0.0f, 0.0f, 0.0f, pa });
	}
}

// ----- 右のパネル：報酬のマスと説明 -----

void ResultScene::DrawRewardPanel(float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float appear = EaseOutCubic((time_ - kPanelAppear) / kAppearTime);
	if (appear <= 0.0f) return;
	const float a = appear * uiAlpha;
	const PanelLayout layout = MakeLayout((1.0f - appear) * kSlideDistance);
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect({ layout.panelX, kPanelY }, { kPanelW, kPanelH }, WithAlpha(kPanelColor, a));

	auto header = [&](const char* text, float y) {
		tr->DrawText(text, { layout.innerX, y }, 0.6f, WithAlpha(kGold, a), 1.5f, shadow);
		tr->DrawRect({ layout.innerX, y + 30.0f }, { layout.innerW, 1.5f }, WithAlpha(kGold, 0.5f * a));
	};

	header("ステージ報酬", layout.stageHeaderY);
	DrawSlotGrid(stageSlots_, { layout.innerX, layout.stageGridY }, kStageSlotRows, 0, a);

	// 部位破壊報酬（壊していなければ空のマス）
	header("部位破壊報酬", layout.partHeaderY);
	DrawSlotGrid(partBreakSlots_, { layout.innerX, layout.partGridY }, kPartBreakSlotRows,
		static_cast<int>(stageSlots_.size()), a);

	if (sellOpen_) DrawSellWindow(layout, a);
	else DrawDescription(layout, a);
	DrawStageSelectButton(layout, a);
}

void ResultScene::DrawSlotGrid(const std::vector<RewardSlot>& slots, const Vector2& origin, int minRows,
	int selectableOffset, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const int total = RowsFor(slots.size(), minRows) * kSlotColumns;

	for (int i = 0; i < total; ++i) {
		const Vector2 pos{ origin.x + (i % kSlotColumns) * (kSlotSize + kSlotGap), origin.y + (i / kSlotColumns) * (kSlotSize + kSlotGap) };
		const bool filled = i < static_cast<int>(slots.size());

		if (!filled) {
			// 空のマス
			tr->DrawRect(pos, { kSlotSize, kSlotSize }, { 1.0f, 1.0f, 1.0f, 0.04f * uiAlpha });
			DrawFrame(pos, { kSlotSize, kSlotSize }, 1.0f, { 1.0f, 1.0f, 1.0f, 0.12f * uiAlpha });
			continue;
		}

		const RewardSlot& slot = slots[static_cast<size_t>(i)];
		const float appear = Clamp01((time_ - slot.startTime + 0.15f) / 0.15f);
		tr->DrawRect(pos, { kSlotSize, kSlotSize }, { 1.0f, 1.0f, 1.0f, 0.06f * uiAlpha });
		DrawFrame(pos, { kSlotSize, kSlotSize }, 1.0f, { 1.0f, 1.0f, 1.0f, 0.18f * uiAlpha });
		if (appear <= 0.0f) continue;
		const float a = appear * uiAlpha;

		// 出たときに少し大きい所から収まる
		const float pop = 1.0f + 0.25f * (1.0f - EaseOutCubic(appear));
		const float iconSize = kSlotSize * 0.5f * pop;
		DrawItemIcon(*slot.item, { pos.x + (kSlotSize - iconSize) * 0.5f, pos.y + 10.0f + (kSlotSize * 0.5f - iconSize) * 0.5f },
			iconSize, a);

		const float progress = Clamp01((time_ - slot.startTime) / kSlotCountTime);
		const int shown = static_cast<int>(static_cast<float>(slot.count) * progress + 0.5f);
		char buf[16];
		std::snprintf(buf, sizeof(buf), "×%d", shown);
		const float cs = 0.5f;
		const float cw = tr->MeasureWidth(buf, cs);
		tr->DrawText(buf, { pos.x + kSlotSize - 6.0f - cw, pos.y + kSlotSize - 24.0f }, cs, { 1.0f, 1.0f, 1.0f, a }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, a });

		// 選んでいるマスの枠（ゆっくり明滅）
		if (selectableOffset + i == selected_) {
			const float pulse = 0.65f + 0.35f * std::sin(time_ * 5.0f);
			DrawFrame({ pos.x - 3.0f, pos.y - 3.0f }, { kSlotSize + 6.0f, kSlotSize + 6.0f }, 3.0f, WithAlpha(kGold, pulse * a));
		}
	}
}

void ResultScene::DrawItemIcon(const LootItemDef& item, const Vector2& pos, float size, float alpha) {
	// アイコンの画像はまだ無い（23 番 §2.1）。色付きの図形で代わりにする。画像ができたらここだけ差し替える
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector4& c = item.color;
	tr->DrawRect(pos, { size, size }, { c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, alpha });
	const float inset = size * 0.18f;
	tr->DrawRect({ pos.x + inset, pos.y + inset }, { size - inset * 2.0f, size - inset * 2.0f }, WithAlpha(c, alpha));
	// 左上のつや
	tr->DrawRect({ pos.x + inset, pos.y + inset }, { (size - inset * 2.0f) * 0.45f, (size - inset * 2.0f) * 0.2f },
		{ 1.0f, 1.0f, 1.0f, 0.35f * alpha });
}

void ResultScene::DrawDescription(const PanelLayout& layout, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Vector2 origin{ layout.innerX, layout.descY };
	const float width = layout.innerW;
	tr->DrawRect(origin, { width, layout.descH }, { 0.0f, 0.0f, 0.0f, 0.35f * uiAlpha });
	DrawFrame(origin, { width, layout.descH }, 1.0f, { 1.0f, 1.0f, 1.0f, 0.15f * uiAlpha });
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, uiAlpha };

	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) {
		const char* text = (SelectableCount() == 0) ? "素材は手に入らなかった" : "素材を選ぶと説明が出る";
		tr->DrawText(text, { origin.x + 18.0f, origin.y + 18.0f }, 0.55f, { 0.8f, 0.8f, 0.8f, 0.8f * uiAlpha }, 1.5f, shadow);
		return;
	}
	// まだ出ていないマスの説明は出さない（数え上げの順を崩さない）
	const float a = uiAlpha * Clamp01((time_ - slot->startTime + 0.15f) / 0.15f);
	if (a <= 0.0f) return;
	const Vector4 textShadow{ 0.0f, 0.0f, 0.0f, a };
	const LootItemDef& item = *slot->item;

	DrawItemIcon(item, { origin.x + 18.0f, origin.y + 18.0f }, 44.0f, a);
	tr->DrawText(item.displayName, { origin.x + 76.0f, origin.y + 16.0f }, 0.7f, { 1.0f, 1.0f, 1.0f, a }, 1.5f, textShadow);

	char rarityBuf[32];
	std::snprintf(rarityBuf, sizeof(rarityBuf), "レア度 %d", item.rarity);
	tr->DrawText(rarityBuf, { origin.x + 78.0f, origin.y + 46.0f }, 0.5f, WithAlpha(RarityColor(item.rarity), a), 1.5f,
		textShadow);

	char countBuf[48];
	std::snprintf(countBuf, sizeof(countBuf), "%s ×%d", IsPartBreakIndex(selected_) ? "部位破壊" : "今回", slot->count);
	const float cw = tr->MeasureWidth(countBuf, 0.55f);
	tr->DrawText(countBuf, { origin.x + width - 18.0f - cw, origin.y + 16.0f }, 0.55f, { 0.9f, 0.9f, 0.9f, a }, 1.5f,
		textShadow);
	if (item.sellPrice > 0) {
		const std::string price = "売値 " + WithCommas(item.sellPrice);
		const float pw = tr->MeasureWidth(price, 0.5f);
		tr->DrawText(price, { origin.x + width - 18.0f - pw, origin.y + 46.0f }, 0.5f, { 0.75f, 0.85f, 0.95f, a }, 1.5f,
			textShadow);
	}

	tr->DrawRect({ origin.x + 18.0f, origin.y + 76.0f }, { width - 36.0f, 1.0f }, { 1.0f, 1.0f, 1.0f, 0.2f * a });
	float ly = origin.y + 88.0f;
	for (std::string_view line : SplitLines(item.description)) {
		tr->DrawText(line, { origin.x + 18.0f, ly }, 0.5f, { 0.88f, 0.9f, 0.94f, a }, 1.5f, textShadow);
		ly += 26.0f;
	}
}

void ResultScene::DrawSellWindow(const PanelLayout& layout, float uiAlpha) {
	// 売る数の小窓（説明の欄に重ねる）：素材の名前 → 左に ▲ 数 ▼・右に合計 → [売る] [やめる]
	TextRenderer* tr = TextRenderer::GetInstance();
	const RewardSlot* slot = SelectableAt(selected_);
	if (!slot || !slot->item) return;
	const LootItemDef& item = *slot->item;
	const Vector2 origin{ layout.innerX, layout.descY };
	const float width = layout.innerW;
	const float a = uiAlpha;
	const Vector4 shadow{ 0.0f, 0.0f, 0.0f, a };

	tr->DrawRect(origin, { width, layout.descH }, { 0.02f, 0.04f, 0.08f, 0.85f * a });
	DrawFrame(origin, { width, layout.descH }, 2.0f, WithAlpha(kGold, 0.8f * a));

	DrawItemIcon(item, { origin.x + 18.0f, origin.y + 16.0f }, 36.0f, a);
	const std::string title = item.displayName + " を売る";
	tr->DrawText(title, { origin.x + 66.0f, origin.y + 18.0f }, 0.62f, { 1.0f, 1.0f, 1.0f, a }, 1.5f, shadow);

	// 左：▲ 数 / 持っている数 ▼（↑↓・クリックで増減）
	const Rect up = SellArrowRect(layout, +1);
	const Rect down = SellArrowRect(layout, -1);
	const bool canUp = sellAmount_ < slot->count;
	const bool canDown = sellAmount_ > 1;
	auto arrow = [&](const Rect& r, const char* glyph, bool enabled) {
		const float gw = tr->MeasureWidth(glyph, 0.6f);
		tr->DrawText(glyph, { r.pos.x + (r.size.x - gw) * 0.5f, r.pos.y + 3.0f }, 0.6f,
			WithAlpha(kGold, (enabled ? 1.0f : 0.3f) * a), 1.5f, shadow);
	};
	arrow(up, "▲", canUp);
	arrow(down, "▼", canDown);
	char amountBuf[32];
	std::snprintf(amountBuf, sizeof(amountBuf), "%d / %d", sellAmount_, slot->count);
	const float amountCenterX = up.pos.x + up.size.x * 0.5f;
	const float aw = tr->MeasureWidth(amountBuf, 0.85f);
	tr->DrawText(amountBuf, { amountCenterX - aw * 0.5f, layout.descY + 76.0f }, 0.85f, { 1.0f, 1.0f, 1.0f, a }, 1.5f, shadow);

	// 右：合計
	const LootItemDef& ether = LootCatalog::GetInstance()->GetEther();
	const float totalCenterX = origin.x + width * 0.5f + 100.0f;
	const char* totalLabel = "合計";
	const float lw = tr->MeasureWidth(totalLabel, 0.5f);
	tr->DrawText(totalLabel, { totalCenterX - lw * 0.5f, layout.descY + 56.0f }, 0.5f, { 0.8f, 0.85f, 0.9f, a }, 1.5f, shadow);
	const std::string total = "エーテル +" + WithCommas(item.sellPrice * sellAmount_);
	const float tw = tr->MeasureWidth(total, 0.7f);
	tr->DrawText(total, { totalCenterX - tw * 0.5f, layout.descY + 84.0f }, 0.7f, WithAlpha(ether.color, a), 1.5f, shadow);

	// [売る] [やめる]：←→・マウスで選んだほうに明滅する枠
	auto button = [&](const Rect& r, const char* label, bool focused) {
		tr->DrawRect(r.pos, r.size, focused ? WithAlpha(kGold, 0.28f * a) : Vector4{ 1.0f, 1.0f, 1.0f, 0.06f * a });
		if (focused) {
			const float pulse = 0.65f + 0.35f * std::sin(time_ * 5.0f);
			DrawFrame({ r.pos.x - 3.0f, r.pos.y - 3.0f }, { r.size.x + 6.0f, r.size.y + 6.0f }, 3.0f, WithAlpha(kGold, pulse * a));
		} else {
			DrawFrame(r.pos, r.size, 1.0f, { 1.0f, 1.0f, 1.0f, 0.3f * a });
		}
		const float bw = tr->MeasureWidth(label, 0.6f);
		tr->DrawText(label, { r.pos.x + (r.size.x - bw) * 0.5f, r.pos.y + 8.0f }, 0.6f, { 1.0f, 1.0f, 1.0f, a }, 1.5f, shadow);
	};
	button(SellButtonRect(layout, true), "売る", !sellFocusCancel_);
	button(SellButtonRect(layout, false), "やめる", sellFocusCancel_);
}

void ResultScene::DrawStageSelectButton(const PanelLayout& layout, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const Rect r = ButtonRect(layout);
	const bool selected = (selected_ == ButtonIndex()) && !sellOpen_;
	const float a = uiAlpha * (IsCounting() ? 0.5f : 1.0f);
	tr->DrawRect(r.pos, r.size, selected ? WithAlpha(kGold, 0.28f * a) : Vector4{ 1.0f, 1.0f, 1.0f, 0.06f * a });
	if (selected) {
		const float pulse = 0.65f + 0.35f * std::sin(time_ * 5.0f);
		DrawFrame({ r.pos.x - 3.0f, r.pos.y - 3.0f }, { r.size.x + 6.0f, r.size.y + 6.0f }, 3.0f, WithAlpha(kGold, pulse * a));
	} else {
		DrawFrame(r.pos, r.size, 1.0f, { 1.0f, 1.0f, 1.0f, 0.25f * a });
	}
	const char* label = "ステージ選択へ";
	const float lw = tr->MeasureWidth(label, 0.62f);
	tr->DrawText(label, { r.pos.x + (r.size.x - lw) * 0.5f, r.pos.y + 9.0f }, 0.62f, { 1.0f, 1.0f, 1.0f, a }, 1.5f,
		{ 0.0f, 0.0f, 0.0f, a });
}

// ----- 下：操作の案内 -----

void ResultScene::DrawHint(float screenW, float screenH, float uiAlpha) {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float hs = 0.55f;

	if (backgroundOnly_) {
		// 背景を見ている間：戻り方だけ少しの間出して消す
		const float a = 1.0f - Clamp01((backgroundToggleTime_ - 1.5f) / 0.5f);
		if (a <= 0.0f) return;
		const char* hint = "キャンセル：戻る";
		const float hw = tr->MeasureWidth(hint, hs);
		tr->DrawText(hint, { (screenW - hw) * 0.5f, screenH - 70.0f }, hs, { 1.0f, 1.0f, 1.0f, 0.75f * a }, 1.5f,
			{ 0.0f, 0.0f, 0.0f, a });
		return;
	}

	const float appear = Clamp01((time_ - kPanelAppear) / 0.3f) * uiAlpha;
	if (appear <= 0.0f) return;
	const char* hint = nullptr;
	if (IsCounting()) hint = "決定：スキップ　　キャンセル：背景を見る";
	else if (sellOpen_) hint = "↑↓：売る数　　←→：売る／やめる　　決定：選んだほう　　キャンセル：やめる";
	else if (selected_ == ButtonIndex()) hint = "十字キー：素材を選ぶ　　決定：ステージ選択へ　　キャンセル：背景を見る";
	else hint = "十字キー：素材を選ぶ　　決定：売る　　キャンセル：背景を見る　　Esc：ステージ選択へ";
	const float hw = tr->MeasureWidth(hint, hs);
	tr->DrawRect({ 0.0f, screenH - 84.0f }, { screenW, 50.0f }, { 0.0f, 0.0f, 0.0f, 0.35f * appear });
	tr->DrawText(hint, { (screenW - hw) * 0.5f, screenH - 72.0f }, hs, { 1.0f, 1.0f, 1.0f, 0.85f * appear }, 1.5f,
		{ 0.0f, 0.0f, 0.0f, appear });
}

void ResultScene::DrawFrame(const Vector2& pos, const Vector2& size, float thickness, const Vector4& color) {
	TextRenderer* tr = TextRenderer::GetInstance();
	tr->DrawRect(pos, { size.x, thickness }, color);
	tr->DrawRect({ pos.x, pos.y + size.y - thickness }, { size.x, thickness }, color);
	tr->DrawRect({ pos.x, pos.y }, { thickness, size.y }, color);
	tr->DrawRect({ pos.x + size.x - thickness, pos.y }, { thickness, size.y }, color);
}

Camera* ResultScene::GetCamera() {
	return camera_.get();
}
