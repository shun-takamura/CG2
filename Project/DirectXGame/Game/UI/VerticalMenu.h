#pragma once

#include "Vector2.h"
#include "Vector4.h"
#include <string>
#include <vector>

class InputActionMap;
struct UIPointer;

/// <summary>
/// キーボード/パッド/マウスで操作する縦並びの選択メニュー。
/// MenuUp/MenuDown で選択、MenuConfirm で決定、MenuCancel でキャンセルを返す。
/// マウスは項目の上にカーソルを動かすと選択、項目の上で左クリックすると決定。
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

	// pointer が null ならマウスは使わない
	Result Update(InputActionMap* actions, const UIPointer* pointer = nullptr);

	// 配置。center.x を中央揃えの基準、center.y を項目ブロック全体の縦中心にする。
	// Draw とマウスの当たり判定の両方がこの位置を使うので、Update より前に設定する
	void SetPosition(const Vector2& center) { center_ = center; }
	void Draw() const;

	int GetSelectedIndex() const { return selectedIndex_; }
	void SetSelectedIndex(int index);

	void SetScale(float scale) { scale_ = scale; }
	void SetLineHeight(float lineHeight) { lineHeight_ = lineHeight; }

private:
	// 画面上のその位置にある項目の番号（無ければ -1）
	int HitTest(const Vector2& position) const;
	std::string MakeLabel(size_t index, bool selected) const;

	std::vector<std::string> items_;
	Vector2 center_{};
	int selectedIndex_ = 0;

	float scale_ = 1.5f;
	float lineHeight_ = 60.0f;
	Vector4 normalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 selectedColor_ = { 1.0f, 0.85f, 0.2f, 1.0f };
	Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
	float outlineThickness_ = 2.0f;
};
