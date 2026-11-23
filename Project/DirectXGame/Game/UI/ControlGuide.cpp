#include "ControlGuide.h"

#include "InputAction.h"
#include "Config/GameActions.h"
#include "TextRenderer.h"
#include "SoundManager.h"
#include "UIPointer.h"
#include "Json/JsonValue.h"
#include "Json/JsonParser.h"
#include "Vector2.h"
#include "Vector4.h"
#include <algorithm>
#include <filesystem>

namespace {
	constexpr const char* kPath = "Resources/Json/UI/ControlGuide.json";

	// 配置（UI 座標 1600×900 基準）
	constexpr float kPanelLeft = 110.0f;
	constexpr float kPanelTop = 60.0f;
	constexpr float kPanelRight = 1490.0f;
	constexpr float kPanelBottom = 840.0f;
	constexpr float kContentLeft = 150.0f;
	constexpr float kContentWidth = 1300.0f;
	constexpr float kColumnX[3] = { 150.0f, 640.0f, 1090.0f };
	constexpr float kColumnWidth[3] = { 470.0f, 430.0f, 360.0f };

	constexpr float kCellScale = 0.85f;
	constexpr float kCellLineHeight = 34.0f;
	constexpr float kNoteScale = 0.66f;
	constexpr float kNoteLineHeight = 26.0f;
	constexpr float kRowGap = 8.0f;
	constexpr float kTextScale = 0.76f;
	constexpr float kTextLineHeight = 32.0f;

	// 配置の基準の UI 座標の大きさ [px]
	constexpr float kLayoutWidth = 1600.0f;
	constexpr float kLayoutHeight = 900.0f;
	// 表の列の数（操作・キーボード・パッド）
	constexpr int kColumnCount = 3;
	// 下地の上下の線の太さ [px]
	constexpr float kAccentLineThickness = 3.0f;
	// 文字のアウトラインの太さ [px]
	constexpr float kOutlineThickness = 2.0f;
	// 読み込めなかったときの文言の高さ [px]
	constexpr float kErrorTextY = 120.0f;
	// 見出し（ガイド全体の題）の高さ [px] と倍率
	constexpr float kTitleY = 82.0f;
	constexpr float kTitleScale = 0.8f;
	// ページの題の高さ [px]・倍率・アウトラインの太さ [px]
	constexpr float kPageTitleY = 112.0f;
	constexpr float kPageTitleScale = 1.4f;
	constexpr float kPageTitleOutlineThickness = 3.0f;
	// ページ番号の右端からの空き [px] と高さ [px]
	constexpr float kCounterRightMargin = 40.0f;
	constexpr float kCounterY = 122.0f;
	// 表・説明の書き始めの高さ [px]
	constexpr float kBodyTopY = 185.0f;
	// 表の列見出しの倍率
	constexpr float kHeaderScale = 0.7f;
	// 列見出しの行の高さ [px]
	constexpr float kHeaderHeight = 32.0f;
	// 列見出しの下の線の太さ [px] と、線の下の空き [px]
	constexpr float kHeaderRuleThickness = 2.0f;
	constexpr float kHeaderRuleGap = 12.0f;
	// 補足の字下げ [px]
	constexpr float kNoteIndent = 20.0f;
	// 表の後ろの空き [px]
	constexpr float kTableBottomGap = 14.0f;
	// 説明の段落の間の空き [px]
	constexpr float kParagraphGap = 6.0f;
	// 操作の案内の下端からの高さ [px]
	constexpr float kFooterBottomOffset = 42.0f;

	const Vector4 kWhite{ 1.0f, 1.0f, 1.0f, 1.0f };
	const Vector4 kGray{ 0.72f, 0.74f, 0.78f, 1.0f };
	const Vector4 kKeyColor{ 1.0f, 0.85f, 0.4f, 1.0f };
	const Vector4 kAccent{ 1.0f, 0.85f, 0.2f, 1.0f };
	const Vector4 kBlack{ 0.0f, 0.0f, 0.0f, 1.0f };
	// 下地の色
	const Vector4 kPanelColor{ 0.04f, 0.06f, 0.10f, 0.88f };
	// 列見出しの下の線の色
	const Vector4 kHeaderRuleColor{ 1.0f, 1.0f, 1.0f, 0.35f };

	// UTF-8 の 1 バイト文字の上限（これ未満なら 1 バイト）
	constexpr int kUtf8SingleByteLimit = 0x80;
	// UTF-8 の 2 バイト文字の先頭バイト：右へずらす量・ずらした値・バイト数
	constexpr int kUtf8TwoByteShift = 5;
	constexpr int kUtf8TwoBytePattern = 0x6;
	constexpr int kUtf8TwoByteLength = 2;
	// UTF-8 の 3 バイト文字の先頭バイト：右へずらす量・ずらした値・バイト数
	constexpr int kUtf8ThreeByteShift = 4;
	constexpr int kUtf8ThreeBytePattern = 0xE;
	constexpr int kUtf8ThreeByteLength = 3;
	// UTF-8 の 4 バイト文字の先頭バイト：右へずらす量・ずらした値・バイト数
	constexpr int kUtf8FourByteShift = 3;
	constexpr int kUtf8FourBytePattern = 0x1E;
	constexpr int kUtf8FourByteLength = 4;

	/// <summary>UTF-8 の 1 文字のバイト数を先頭バイトから求める</summary>
	/// <param name="lead">文字の先頭バイト</param>
	/// <returns>バイト数（1〜4。不正なバイトは 1）</returns>
	size_t CodepointLength(unsigned char lead) {
		if (lead < kUtf8SingleByteLimit) return 1;
		if ((lead >> kUtf8TwoByteShift) == kUtf8TwoBytePattern) return kUtf8TwoByteLength;
		if ((lead >> kUtf8ThreeByteShift) == kUtf8ThreeBytePattern) return kUtf8ThreeByteLength;
		if ((lead >> kUtf8FourByteShift) == kUtf8FourBytePattern) return kUtf8FourByteLength;
		return 1;
	}

	/// <summary>maxWidth に収まるよう 1 文字単位で折り返す</summary>
	/// <param name="tr">幅を測る TextRenderer</param>
	/// <param name="text">折り返す文字列（UTF-8）</param>
	/// <param name="scale">文字の倍率</param>
	/// <param name="maxWidth">1 行の最大の幅 [px]</param>
	/// <returns>行の列（空文字列なら空の 1 行）</returns>
	std::vector<std::string> Wrap(TextRenderer* tr, const std::string& text, float scale, float maxWidth) {
		std::vector<std::string> lines;
		std::string current;
		size_t i = 0;
		while (i < text.size()) {
			const size_t len = (std::min)(CodepointLength(static_cast<unsigned char>(text[i])), text.size() - i);
			const std::string ch = text.substr(i, len);
			if (!current.empty() && tr->MeasureWidth(current + ch, scale) > maxWidth) {
				lines.push_back(current);
				current.clear();
			}
			current += ch;
			i += len;
		}
		if (!current.empty() || lines.empty()) lines.push_back(current);
		return lines;
	}
}

void ControlGuide::Open() {
	Load();
	pageIndex_ = 0;
	open_ = true;
}

bool ControlGuide::Load() {
	if (!std::filesystem::exists(kPath)) return false;
	const auto result = JsonParser::ParseFile(kPath);
	if (!result.success) return false;
	const JsonValue& root = result.value;

	title_ = root["title"].AsString(title_);
	if (const JsonValue& cols = root["columns"]; cols.IsArray() && cols.Size() >= kColumnCount) {
		for (size_t c = 0; c < kColumnCount; ++c) columns_[c] = cols[c].AsString(columns_[c]);
	}

	const JsonValue& list = root["pages"];
	if (!list.IsArray()) return false;
	std::vector<Page> pages;
	for (size_t p = 0; p < list.Size(); ++p) {
		const JsonValue& pj = list[p];
		if (!pj.IsObject()) continue;
		Page page;
		page.title = pj["title"].AsString(std::string());
		if (const JsonValue& rows = pj["rows"]; rows.IsArray()) {
			for (size_t r = 0; r < rows.Size(); ++r) {
				const JsonValue& rj = rows[r];
				if (!rj.IsObject()) continue;
				Row row;
				row.action = rj["action"].AsString(std::string());
				row.keyboard = rj["keyboard"].AsString(std::string());
				row.pad = rj["pad"].AsString(std::string());
				row.note = rj["note"].AsString(std::string());
				page.rows.push_back(std::move(row));
			}
		}
		if (const JsonValue& lines = pj["lines"]; lines.IsArray()) {
			for (size_t l = 0; l < lines.Size(); ++l) page.lines.push_back(lines[l].AsString(std::string()));
		}
		pages.push_back(std::move(page));
	}
	if (pages.empty()) return false;
	pages_ = std::move(pages);
	return true;
}

void ControlGuide::TurnPage(int step) {
	const int count = static_cast<int>(pages_.size());
	if (count <= 1) return;
	pageIndex_ = ((pageIndex_ + step) % count + count) % count;
	SoundManager::GetInstance()->Play2DSound("se_ui_cursor");
}

ControlGuide::Result ControlGuide::Update(InputActionMap* actions, const UIPointer* pointer) {
	if (!open_ || !actions) return Result::None;

	if (actions->IsTriggered(static_cast<int>(Action::MenuConfirm))
		|| actions->IsTriggered(static_cast<int>(Action::MenuCancel))) {
		SoundManager::GetInstance()->Play2DSound("se_ui_cancel");
		open_ = false;
		return Result::Closed;
	}
	if (actions->IsTriggered(static_cast<int>(Action::MenuLeft))) {
		TurnPage(-1);
	} else if (actions->IsTriggered(static_cast<int>(Action::MenuRight))
		|| (pointer && pointer->valid && pointer->clicked)) {
		TurnPage(+1);
	}
	return Result::None;
}

void ControlGuide::Draw(float screenW, float screenH) const {
	if (!open_) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	// 1600×900 基準の配置を画面の大きさに合わせて真ん中に置く
	const float offsetX = (screenW - kLayoutWidth) * 0.5f;
	const float offsetY = (screenH - kLayoutHeight) * 0.5f;
	auto at = [&](float x, float y) { return Vector2{ x + offsetX, y + offsetY }; };

	// 下地と上下の線
	tr->DrawRect(at(kPanelLeft, kPanelTop), { kPanelRight - kPanelLeft, kPanelBottom - kPanelTop }, kPanelColor);
	tr->DrawRect(at(kPanelLeft, kPanelTop), { kPanelRight - kPanelLeft, kAccentLineThickness }, kAccent);
	tr->DrawRect(at(kPanelLeft, kPanelBottom - kAccentLineThickness), { kPanelRight - kPanelLeft, kAccentLineThickness }, kAccent);

	if (pages_.empty()) {
		tr->DrawText("操作ガイドを読み込めませんでした", at(kContentLeft, kErrorTextY), 1.0f, kWhite, kOutlineThickness, kBlack);
		return;
	}
	const Page& page = pages_[static_cast<size_t>(pageIndex_)];

	// 見出し・ページ番号
	tr->DrawText(title_, at(kContentLeft, kTitleY), kTitleScale, kGray, kOutlineThickness, kBlack);
	tr->DrawText(page.title, at(kContentLeft, kPageTitleY), kPageTitleScale, kWhite, kPageTitleOutlineThickness, kBlack);
	const std::string counter = std::to_string(pageIndex_ + 1) + " / " + std::to_string(pages_.size());
	const float counterW = tr->MeasureWidth(counter, 1.0f);
	tr->DrawText(counter, at(kPanelRight - kCounterRightMargin - counterW, kCounterY), 1.0f, kAccent, kOutlineThickness, kBlack);

	float y = kBodyTopY;

	// 表
	if (!page.rows.empty()) {
		for (int c = 0; c < kColumnCount; ++c) {
			tr->DrawText(columns_[static_cast<size_t>(c)], at(kColumnX[c], y), kHeaderScale, kGray, kOutlineThickness, kBlack);
		}
		y += kHeaderHeight;
		tr->DrawRect(at(kContentLeft, y), { kContentWidth, kHeaderRuleThickness }, kHeaderRuleColor);
		y += kHeaderRuleGap;

		for (const Row& row : page.rows) {
			const std::string* cells[kColumnCount] = { &row.action, &row.keyboard, &row.pad };
			size_t lineCount = 1;
			for (int c = 0; c < kColumnCount; ++c) {
				const std::vector<std::string> lines = Wrap(tr, *cells[c], kCellScale, kColumnWidth[c]);
				const Vector4& color = (c == 0) ? kWhite : kKeyColor;
				for (size_t l = 0; l < lines.size(); ++l) {
					tr->DrawText(lines[l], at(kColumnX[c], y + kCellLineHeight * static_cast<float>(l)), kCellScale, color, kOutlineThickness, kBlack);
				}
				lineCount = (std::max)(lineCount, lines.size());
			}
			y += kCellLineHeight * static_cast<float>(lineCount);
			if (!row.note.empty()) {
				for (const std::string& line : Wrap(tr, row.note, kNoteScale, kContentWidth - kNoteIndent)) {
					tr->DrawText(line, at(kContentLeft + kNoteIndent, y), kNoteScale, kGray, kOutlineThickness, kBlack);
					y += kNoteLineHeight;
				}
			}
			y += kRowGap;
		}
		y += kTableBottomGap;
	}

	// 説明の文
	for (const std::string& text : page.lines) {
		for (const std::string& line : Wrap(tr, text, kTextScale, kContentWidth)) {
			tr->DrawText(line, at(kContentLeft, y), kTextScale, kWhite, kOutlineThickness, kBlack);
			y += kTextLineHeight;
		}
		y += kParagraphGap;
	}

	// 操作の案内
	const char* footer = "← → / 十字キー左右・左クリック：ページ送り　　Esc / BackSpace / B：ポーズに戻る";
	const float footerScale = 0.66f;
	const float footerW = tr->MeasureWidth(footer, footerScale);
	tr->DrawText(footer, at((kLayoutWidth - footerW) * 0.5f, kPanelBottom - kFooterBottomOffset), footerScale, kGray, kOutlineThickness, kBlack);
}
