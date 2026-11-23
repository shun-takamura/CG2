#include "LootPickupLog.h"

#include "TextRenderer.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include <algorithm>
#include <cstdio>

namespace {
	// 数が増えたときの膨らみが 1 秒に減る量
	constexpr float kBumpDecayPerSecond = 6.0f;
	// 出てくるときに右から滑り込む距離 [px]
	constexpr float kSlideInPixels = 40.0f;
	// 消えるときに右へ滑り出る距離 [px]
	constexpr float kSlideOutPixels = 60.0f;
	// 数が増えたときに数の文字を大きくする割合
	constexpr float kBumpScale = 0.25f;
	// 色の印・名前・数の間のすき間 [px]
	constexpr float kRowGapPixels = 14.0f;
	// 色の印の一辺 [px]
	constexpr float kMarkSizePixels = 14.0f;
	// 帯の左右の余白 [px]
	constexpr float kRowPaddingXPixels = 14.0f;
	// 行どうしの縦のすき間（行の高さから引く分）[px]
	constexpr float kRowVerticalGapPixels = 6.0f;
	// 帯の暗い下地の緑
	constexpr float kBandGreen = 0.02f;
	// 帯の暗い下地の青
	constexpr float kBandBlue = 0.05f;
	// 帯の暗い下地の不透明度
	constexpr float kBandAlpha = 0.55f;
	// 帯の左のアイテムの色の縦線の幅 [px]
	constexpr float kColorBarWidthPixels = 3.0f;
	// 文字の大きさ 1 のときの文字の高さ [px]
	constexpr float kGlyphHeightPixels = 32.0f;
	// 文字の縁取りの太さ
	constexpr float kTextOutlineWidth = 1.5f;

	/// <summary>ease-out の 3 次の緩急をかける</summary>
	/// <param name="t">進み 0〜1</param>
	/// <returns>緩急をかけた進み</returns>
	float EaseOutCubic(float t) {
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
}

void LootPickupLog::Update(float dt, LootLedger& ledger) {
	const LootCatalog* catalog = LootCatalog::GetInstance();
	LootPickup pickup;
	while (ledger.PopPickup(pickup)) {
		// 消えかけていない同じ物の行があれば数を足す
		auto it = std::find_if(lines_.begin(), lines_.end(), [&](const Line& l) {
			return l.itemId == pickup.itemId && l.age < kHoldTime;
		});
		if (it != lines_.end()) {
			it->count += pickup.count;
			it->age = (std::min)(it->age, kFadeIn);
			it->bump = 1.0f;
			continue;
		}
		Line line;
		line.itemId = pickup.itemId;
		line.count = pickup.count;
		if (pickup.itemId == LootLedger::kEtherId) {
			line.label = catalog->GetEther().displayName;
			line.color = catalog->GetEther().color;
		} else if (const LootItemDef* item = catalog->FindItem(pickup.itemId)) {
			line.label = item->displayName;
			line.color = item->color;
		} else {
			line.label = pickup.itemId;
		}
		lines_.push_back(line);
		if (static_cast<int>(lines_.size()) > kMaxLines) lines_.erase(lines_.begin());
	}

	for (Line& l : lines_) {
		l.age += dt;
		l.bump = (std::max)(0.0f, l.bump - dt * kBumpDecayPerSecond);
	}
	lines_.erase(std::remove_if(lines_.begin(), lines_.end(), [](const Line& l) {
		return l.age >= kHoldTime + kFadeOut;
	}), lines_.end());
}

void LootPickupLog::Draw(float screenWidth, float screenHeight) const {
	if (lines_.empty()) return;
	TextRenderer* tr = TextRenderer::GetInstance();
	if (!tr->IsInitialized()) return;

	const float right = screenWidth - kRightMargin;
	float y = screenHeight - kBottomMargin;
	// 新しい行がいちばん下
	for (auto it = lines_.rbegin(); it != lines_.rend(); ++it) {
		const Line& l = *it;
		const float in = EaseOutCubic(std::clamp(l.age / kFadeIn, 0.0f, 1.0f));
		const float out = std::clamp((l.age - kHoldTime) / kFadeOut, 0.0f, 1.0f);
		const float alpha = in * (1.0f - out);
		const float slide = (1.0f - in) * kSlideInPixels + out * kSlideOutPixels;

		char countBuf[24];
		if (l.itemId == LootLedger::kEtherId) std::snprintf(countBuf, sizeof(countBuf), "+%d", l.count);
		else std::snprintf(countBuf, sizeof(countBuf), "×%d", l.count);

		const float countScale = kTextScale * (1.0f + kBumpScale * l.bump);
		const float labelW = tr->MeasureWidth(l.label, kTextScale);
		const float countW = tr->MeasureWidth(countBuf, countScale);
		const float gap = kRowGapPixels;
		const float mark = kMarkSizePixels;
		const float padX = kRowPaddingXPixels;
		const float width = padX + mark + gap + labelW + gap + countW + padX;
		const float x = right - width + slide;
		const float h = kLineHeight - kRowVerticalGapPixels;

		// 帯（暗い下地＋左にアイテムの色の縦線）
		tr->DrawRect({ x, y }, { width, h }, { 0.0f, kBandGreen, kBandBlue, kBandAlpha * alpha });
		tr->DrawRect({ x, y }, { kColorBarWidthPixels, h }, { l.color.x, l.color.y, l.color.z, alpha });
		// 色の印（ひし形の代わりに小さな四角）
		const float my = y + (h - mark) * 0.5f;
		tr->DrawRect({ x + padX, my }, { mark, mark }, { l.color.x, l.color.y, l.color.z, alpha });

		const float textY = y + (h - kGlyphHeightPixels * kTextScale) * 0.5f;
		const Vector4 white{ 1.0f, 1.0f, 1.0f, alpha };
		const Vector4 outline{ 0.0f, 0.0f, 0.0f, alpha };
		tr->DrawText(l.label, { x + padX + mark + gap, textY }, kTextScale, white, kTextOutlineWidth, outline);
		const float countY = y + (h - kGlyphHeightPixels * countScale) * 0.5f;
		tr->DrawText(countBuf, { x + width - padX - countW, countY }, countScale,
			{ l.color.x, l.color.y, l.color.z, alpha }, kTextOutlineWidth, outline);

		y -= kLineHeight;
	}
}
