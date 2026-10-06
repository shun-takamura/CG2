#include "VerticalMenu.h"

#include "InputAction.h"
#include "Config/GameActions.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "UIPointer.h"

void VerticalMenu::SetItems(std::vector<std::string> items, int initialIndex) {
	items_ = std::move(items);
	SetSelectedIndex(initialIndex);
}

void VerticalMenu::SetSelectedIndex(int index) {
	const int count = static_cast<int>(items_.size());
	if (count == 0) {
		selectedIndex_ = 0;
		return;
	}
	selectedIndex_ = (index < 0) ? 0 : (index >= count ? count - 1 : index);
}

VerticalMenu::Result VerticalMenu::Update(InputActionMap* actions, const UIPointer* pointer) {
	const int count = static_cast<int>(items_.size());
	if (!actions || count == 0) return Result::None;

	auto* sm = SoundManager::GetInstance();

	// マウス：動かしたときだけ選択を追う（止まったカーソルがキー操作の選択を上書きしないように）。
	// 項目の上で左クリックしたら決定
	if (pointer && pointer->valid) {
		const int hovered = HitTest(pointer->position);
		if (hovered >= 0 && pointer->moved && hovered != selectedIndex_) {
			selectedIndex_ = hovered;
			sm->Play2DSound("se_ui_cursor");
		}
		if (hovered >= 0 && pointer->clicked) {
			selectedIndex_ = hovered;
			sm->Play2DSound("se_ui_decide");
			return Result::Confirmed;
		}
	}

	if (actions->IsTriggered(static_cast<int>(Action::MenuDown))) {
		selectedIndex_ = (selectedIndex_ + 1) % count;
		sm->Play2DSound("se_ui_cursor");
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuUp))) {
		selectedIndex_ = (selectedIndex_ + count - 1) % count;
		sm->Play2DSound("se_ui_cursor");
	}

	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) {
		sm->Play2DSound("se_ui_decide");
		return Result::Confirmed;
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuCancel))) {
		sm->Play2DSound("se_ui_cancel");
		return Result::Canceled;
	}
	return Result::None;
}

std::string VerticalMenu::MakeLabel(size_t index, bool selected) const {
	return (selected ? "> " : "  ") + items_[index];
}

int VerticalMenu::HitTest(const Vector2& position) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return -1;

	const float totalHeight = lineHeight_ * static_cast<float>(items_.size());
	const float top = center_.y - totalHeight * 0.5f;
	// 行の高さで区切った帯 × 文字の幅（選択時の "> " 込み）に少し余白を足した範囲を当たりにする
	constexpr float kPaddingX = 16.0f;
	for (size_t i = 0; i < items_.size(); ++i) {
		const float halfWidth = tr->MeasureWidth(MakeLabel(i, true), scale_) * 0.5f + kPaddingX;
		const float y0 = top + lineHeight_ * static_cast<float>(i) - lineHeight_ * 0.1f;
		if (position.y < y0 || position.y >= y0 + lineHeight_) continue;
		if (position.x < center_.x - halfWidth || position.x > center_.x + halfWidth) continue;
		return static_cast<int>(i);
	}
	return -1;
}

void VerticalMenu::Draw() const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float totalHeight = lineHeight_ * static_cast<float>(items_.size());
	const float top = center_.y - totalHeight * 0.5f;

	for (size_t i = 0; i < items_.size(); ++i) {
		const bool selected = (static_cast<int>(i) == selectedIndex_);
		const std::string label = MakeLabel(i, selected);
		const float w = tr->MeasureWidth(label, scale_);
		tr->DrawText(label, { center_.x - w * 0.5f, top + lineHeight_ * static_cast<float>(i) }, scale_,
			selected ? selectedColor_ : normalColor_, outlineThickness_, outlineColor_);
	}
}
