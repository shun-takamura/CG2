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

	// 開くたびに JSON を読み直す（文面を書き換えたら次に開いたときに反映される）
	void Open();
	void Close() { open_ = false; }
	bool IsOpen() const { return open_; }

	Result Update(InputActionMap* actions, const UIPointer* pointer);
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

	bool Load();
	void TurnPage(int step);

	std::string title_ = "操作ガイド";
	std::vector<std::string> columns_{ "動作", "キーボード・マウス", "ゲームパッド" };
	std::vector<Page> pages_;
	int pageIndex_ = 0;
	bool open_ = false;
};
