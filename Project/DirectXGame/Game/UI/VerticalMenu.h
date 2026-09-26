#pragma once

#include "Vector2.h"
#include "Vector4.h"
#include <string>
#include <vector>

class InputActionMap;

/// <summary>
/// キーボード/パッドで操作する縦並びの選択メニュー。
/// MenuUp/MenuDown で選択、MenuConfirm で決定、MenuCancel でキャンセルを返す。
/// 描画は TextRenderer に積むだけなので、Flush は呼び出し側で行う。
/// </summary>
class VerticalMenu {
public:
	enum class Result {
		None,
		Confirmed,
		Canceled,
	};

	void SetItems(std::vector<std::string> items, int initialIndex = 0);

	Result Update(InputActionMap* actions);

	// center.x を中央揃えの基準、center.y を項目ブロック全体の縦中心として描画する
	void Draw(const Vector2& center) const;

	int GetSelectedIndex() const { return selectedIndex_; }
	void SetSelectedIndex(int index);

	void SetScale(float scale) { scale_ = scale; }
	void SetLineHeight(float lineHeight) { lineHeight_ = lineHeight; }

private:
	std::vector<std::string> items_;
	int selectedIndex_ = 0;

	float scale_ = 1.5f;
	float lineHeight_ = 60.0f;
	Vector4 normalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 selectedColor_ = { 1.0f, 0.85f, 0.2f, 1.0f };
	Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
	float outlineThickness_ = 2.0f;
};
