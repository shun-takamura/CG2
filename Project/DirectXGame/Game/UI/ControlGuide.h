#pragma once

#include <string>
#include <vector>

class InputActionMap;
struct UIPointer;

/// <summary>
/// ポーズ画面から開く操作ガイド（文字だけ・ページ送り）。文面は Resources/Json/UI/ControlGuide.json。
/// ← → / 十字キーの左右・左クリックでページ送り、MenuConfirm / MenuCancel で閉じる。
/// 描画は TextRenderer に積むだけなので、Flush は呼び出し側で行う。
/// </summary>
class ControlGuide {
public:
	enum class Result {
		None,
		Closed,
	};

	/// <summary>先頭のページで開く。開くたびに JSON を読み直す（文面を書き換えたら次に開いたときに反映される）</summary>
	void Open();
	/// <summary>閉じる</summary>
	void Close() { open_ = false; }
	/// <summary>開いているか</summary>
	/// <returns>開いていれば true</returns>
	bool IsOpen() const { return open_; }

	/// <summary>入力を読んでページ送り・閉じるを行う（開いていなければ何もしない）</summary>
	/// <param name="actions">入力アクション（nullptr なら何もしない）</param>
	/// <param name="pointer">マウスの状態（nullptr ならマウスは使わない。左クリックで次のページ）</param>
	/// <returns>MenuConfirm / MenuCancel で閉じたら Closed。それ以外は None</returns>
	Result Update(InputActionMap* actions, const UIPointer* pointer);
	/// <summary>暗いパネルと今のページ（表・本文・ページ番号）を TextRenderer に積む（開いていなければ何もしない）</summary>
	/// <param name="screenW">UI 座標の画面の幅</param>
	/// <param name="screenH">UI 座標の画面の高さ</param>
	void Draw(float screenW, float screenH) const;

private:
	struct Row {
		std::string action;
		std::string keyboard;
		std::string pad;
		std::string note;
	};
	struct Page {
		std::string title;
		std::vector<Row> rows;
		std::vector<std::string> lines;
	};

	/// <summary>Resources/Json/UI/ControlGuide.json から見出し・列名・ページを読む</summary>
	/// <returns>1 ページ以上読めたら true（失敗した時は前の文面のまま）</returns>
	bool Load();
	/// <summary>ページを送る（端は反対側へ回る）。1 ページ以下なら何もしない</summary>
	/// <param name="step">送る数（+1 で次、-1 で前）</param>
	void TurnPage(int step);

	std::string title_ = "操作ガイド";
	std::vector<std::string> columns_{ "動作", "キーボード・マウス", "ゲームパッド" };
	std::vector<Page> pages_;
	int pageIndex_ = 0;
	bool open_ = false;
};
