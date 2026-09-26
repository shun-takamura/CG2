#include "VerticalMenu.h"

#include "InputAction.h"
#include "Config/GameActions.h"
#include "TextRenderer.h"

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

VerticalMenu::Result VerticalMenu::Update(InputActionMap* actions) {
	const int count = static_cast<int>(items_.size());
	if (!actions || count == 0) return Result::None;

	if (actions->IsTriggered(static_cast<int>(Action::MenuDown))) {
		selectedIndex_ = (selectedIndex_ + 1) % count;
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuUp))) {
		selectedIndex_ = (selectedIndex_ + count - 1) % count;
	}

	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))) return Result::Confirmed;
	if (actions->IsTriggered(static_cast<int>(Action::MenuCancel)))  return Result::Canceled;
	return Result::None;
}

void VerticalMenu::Draw(const Vector2& center) const {
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float totalHeight = lineHeight_ * static_cast<float>(items_.size());
	const float top = center.y - totalHeight * 0.5f;

	for (size_t i = 0; i < items_.size(); ++i) {
		const bool selected = (static_cast<int>(i) == selectedIndex_);
		const std::string label = (selected ? "> " : "  ") + items_[i];
		const float w = tr->MeasureWidth(label, scale_);
		tr->DrawText(label, { center.x - w * 0.5f, top + lineHeight_ * static_cast<float>(i) }, scale_,
			selected ? selectedColor_ : normalColor_, outlineThickness_, outlineColor_);
	}
}
