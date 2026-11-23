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

	/// <summary>項目を差し替える（すべて選べる状態に戻る）</summary>
	/// <param name="items">項目の文字列</param>
	/// <param name="initialIndex">最初に選ぶ項目の番号（範囲に収める）</param>
	void SetItems(std::vector<std::string> items, int initialIndex = 0);

	/// <summary>項目を選べる / 選べないにする。選んでいた項目を選べなくしたら、次の選べる項目へ移る</summary>
	/// <param name="index">項目の番号（範囲外なら何もしない）</param>
	/// <param name="enabled">選べるなら true</param>
	void SetItemEnabled(int index, bool enabled);
	/// <summary>項目を選べるか</summary>
	/// <param name="index">項目の番号</param>
	/// <returns>選べるなら true。範囲外なら false</returns>
	bool IsItemEnabled(int index) const;

	/// <summary>入力を読んで選択を動かし、決定・キャンセルを返す</summary>
	/// <param name="actions">入力アクション</param>
	/// <param name="pointer">マウスの状態（null ならマウスは使わない）</param>
	/// <returns>決定なら Confirmed、キャンセルなら Canceled、どちらでもなければ None</returns>
	Result Update(InputActionMap* actions, const UIPointer* pointer = nullptr);

	/// <summary>
	/// 配置を設定する。center.x を中央揃えの基準、center.y を項目ブロック全体の縦中心にする。
	/// Draw とマウスの当たり判定の両方がこの位置を使うので、Update より前に設定する
	/// </summary>
	/// <param name="center">基準の位置（UI 座標）</param>
	void SetPosition(const Vector2& center) { center_ = center; }
	/// <summary>項目を縦に並べて TextRenderer に積む（選択中は色を変えて "> " を付け、選べない項目は灰色）</summary>
	void Draw() const;

	/// <summary>選択中の項目の番号を取得する</summary>
	/// <returns>番号</returns>
	int GetSelectedIndex() const { return selectedIndex_; }
	/// <summary>選ぶ項目を設定する（範囲に収める。項目が無ければ 0）</summary>
	/// <param name="index">項目の番号</param>
	void SetSelectedIndex(int index);

	/// <summary>文字の倍率を設定する</summary>
	/// <param name="scale">倍率</param>
	void SetScale(float scale) { scale_ = scale; }
	/// <summary>行の高さを設定する</summary>
	/// <param name="lineHeight">行の高さ [px]</param>
	void SetLineHeight(float lineHeight) { lineHeight_ = lineHeight; }
	/// <summary>文字の倍率を取得する</summary>
	/// <returns>倍率</returns>
	float GetScale() const { return scale_; }
	/// <summary>行の高さを取得する</summary>
	/// <returns>行の高さ [px]</returns>
	float GetLineHeight() const { return lineHeight_; }

	/// <summary>項目の数を取得する（見た目を自前で描くとき用）</summary>
	/// <returns>項目の数</returns>
	int GetItemCount() const { return static_cast<int>(items_.size()); }
	/// <summary>項目の文字列を取得する</summary>
	/// <param name="index">項目の番号（範囲内であること）</param>
	/// <returns>項目の文字列</returns>
	const std::string& GetItemText(int index) const { return items_[index]; }
	/// <summary>Draw と同じ表示の文字列（選択中なら "> " 付き）を取得する</summary>
	/// <param name="index">項目の番号</param>
	/// <returns>表示の文字列</returns>
	std::string GetItemLabel(int index) const { return MakeLabel(static_cast<size_t>(index), index == selectedIndex_); }
	/// <summary>Draw と同じ項目の左上の位置を求める（マウスの当たりもこの位置）</summary>
	/// <param name="index">項目の番号</param>
	/// <returns>左上の位置（UI 座標）</returns>
	Vector2 GetItemDrawPosition(int index) const;

private:
	/// <summary>画面上のその位置にある項目を探す</summary>
	/// <param name="position">位置（UI 座標）</param>
	/// <returns>項目の番号。無い・TextRenderer が未初期化なら -1</returns>
	int HitTest(const Vector2& position) const;
	/// <summary>表示の文字列を作る</summary>
	/// <param name="index">項目の番号</param>
	/// <param name="selected">選択中か（"> " を付ける）</param>
	/// <returns>表示の文字列</returns>
	std::string MakeLabel(size_t index, bool selected) const;
	/// <summary>from から step の向きにたどって最初の選べる項目を探す（端は反対側へ回る）</summary>
	/// <param name="from">探し始める番号（自身は含まない）</param>
	/// <param name="step">たどる向き（+1 / -1）</param>
	/// <returns>選べる項目の番号。無ければ from のまま</returns>
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
