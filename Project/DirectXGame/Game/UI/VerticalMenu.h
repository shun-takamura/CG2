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
/// 選べない項目（SetItemEnabled）は灰色で描き、上下キーでは飛ばし、マウスでも選ばない。
/// 描画は TextRenderer に積むだけなので、Flush は呼び出し側で行う。
/// </summary>
class VerticalMenu {
public:
	enum class Result {
		None,
		Confirmed,
		Canceled,
	};

	// 項目を差し替える（すべて選べる状態に戻る）
	void SetItems(std::vector<std::string> items, int initialIndex = 0);

	// 項目を選べる / 選べないにする。選んでいた項目を選べなくしたら、次の選べる項目へ移る
	void SetItemEnabled(int index, bool enabled);
	bool IsItemEnabled(int index) const;

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
	float GetScale() const { return scale_; }
	float GetLineHeight() const { return lineHeight_; }

	// 見た目を自前で描くとき用（Draw と同じ文字列と左上の位置。マウスの当たりもこの位置）
	int GetItemCount() const { return static_cast<int>(items_.size()); }
	const std::string& GetItemText(int index) const { return items_[index]; }
	std::string GetItemLabel(int index) const { return MakeLabel(static_cast<size_t>(index), index == selectedIndex_); }
	Vector2 GetItemDrawPosition(int index) const;

private:
	// 画面上のその位置にある項目の番号（無ければ -1）
	int HitTest(const Vector2& position) const;
	std::string MakeLabel(size_t index, bool selected) const;
	// from から step（+1 / -1）の向きにたどって最初の選べる項目（無ければ from のまま）
	int FindEnabled(int from, int step) const;

	std::vector<std::string> items_;
	std::vector<bool> enabled_;
	Vector2 center_{};
	int selectedIndex_ = 0;

	float scale_ = 1.5f;
	float lineHeight_ = 60.0f;
	Vector4 normalColor_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 selectedColor_ = { 1.0f, 0.85f, 0.2f, 1.0f };
	Vector4 disabledColor_ = { 0.5f, 0.5f, 0.5f, 1.0f };
	Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
	float outlineThickness_ = 2.0f;
};
