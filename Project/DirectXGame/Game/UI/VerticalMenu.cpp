#include "VerticalMenu.h"

#include "InputAction.h"
#include "Config/GameActions.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "UIPointer.h"

void VerticalMenu::SetItems(std::vector<std::string> items, int initialIndex) {
	items_ = std::move(items);
	enabled_.assign(items_.size(), true);
	SetSelectedIndex(initialIndex);
}

void VerticalMenu::SetItemEnabled(int index, bool enabled) {
	if (index < 0 || index >= static_cast<int>(enabled_.size())) return;
	enabled_[index] = enabled;
	if (!enabled && index == selectedIndex_) {
		selectedIndex_ = FindEnabled(selectedIndex_, +1);
	}
}

bool VerticalMenu::IsItemEnabled(int index) const {
	return index >= 0 && index < static_cast<int>(enabled_.size()) && enabled_[index];
}

int VerticalMenu::FindEnabled(int from, int step) const {
	const int count = static_cast<int>(items_.size());
	for (int i = 1; i <= count; ++i) {
		const int index = ((from + step * i) % count + count) % count;
		if (enabled_[index]) return index;
	}
	return from;
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
		int hovered = HitTest(pointer->position);
		if (!IsItemEnabled(hovered)) hovered = -1;
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
		selectedIndex_ = FindEnabled(selectedIndex_, +1);
		sm->Play2DSound("se_ui_cursor");
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuUp))) {
		selectedIndex_ = FindEnabled(selectedIndex_, -1);
		sm->Play2DSound("se_ui_cursor");
	}

	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm)) && IsItemEnabled(selectedIndex_)) {
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

Vector2 VerticalMenu::GetItemDrawPosition(int index) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	const float totalHeight = lineHeight_ * static_cast<float>(items_.size());
	const float top = center_.y - totalHeight * 0.5f;
	const float w = tr->IsInitialized() ? tr->MeasureWidth(GetItemLabel(index), scale_) : 0.0f;
	return { center_.x - w * 0.5f, top + lineHeight_ * static_cast<float>(index) };
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
		const Vector4& color = !enabled_[i] ? disabledColor_ : (selected ? selectedColor_ : normalColor_);
		tr->DrawText(label, { center_.x - w * 0.5f, top + lineHeight_ * static_cast<float>(i) }, scale_,
			color, outlineThickness_, outlineColor_);
	}
}
