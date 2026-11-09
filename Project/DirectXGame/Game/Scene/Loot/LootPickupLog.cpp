#include "LootPickupLog.h"

#include "TextRenderer.h"
#include "Score/LootCatalog.h"
#include "Score/LootLedger.h"
#include <algorithm>
#include <cstdio>

namespace {
	float EaseOutCubic(float t) {
		const float u = 1.0f - t;
		return 1.0f - u * u * u;
	}
}

void LootPickupLog::Update(float dt) {
	const LootCatalog* catalog = LootCatalog::GetInstance();
	LootPickup pickup;
	while (LootLedger::GetInstance()->PopPickup(pickup)) {
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
		l.bump = (std::max)(0.0f, l.bump - dt * 6.0f);
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
		const float slide = (1.0f - in) * 40.0f + out * 60.0f;

		char countBuf[24];
		if (l.itemId == LootLedger::kEtherId) std::snprintf(countBuf, sizeof(countBuf), "+%d", l.count);
		else std::snprintf(countBuf, sizeof(countBuf), "×%d", l.count);

		const float countScale = kTextScale * (1.0f + 0.25f * l.bump);
		const float labelW = tr->MeasureWidth(l.label, kTextScale);
		const float countW = tr->MeasureWidth(countBuf, countScale);
		const float gap = 14.0f;
		const float mark = 14.0f;
		const float padX = 14.0f;
		const float width = padX + mark + gap + labelW + gap + countW + padX;
		const float x = right - width + slide;
		const float h = kLineHeight - 6.0f;

		// 帯（暗い下地＋左にアイテムの色の縦線）
		tr->DrawRect({ x, y }, { width, h }, { 0.0f, 0.02f, 0.05f, 0.55f * alpha });
		tr->DrawRect({ x, y }, { 3.0f, h }, { l.color.x, l.color.y, l.color.z, alpha });
		// 色の印（ひし形の代わりに小さな四角）
		const float my = y + (h - mark) * 0.5f;
		tr->DrawRect({ x + padX, my }, { mark, mark }, { l.color.x, l.color.y, l.color.z, alpha });

		const float textY = y + (h - 32.0f * kTextScale) * 0.5f;
		const Vector4 white{ 1.0f, 1.0f, 1.0f, alpha };
		const Vector4 outline{ 0.0f, 0.0f, 0.0f, alpha };
		tr->DrawText(l.label, { x + padX + mark + gap, textY }, kTextScale, white, 1.5f, outline);
		const float countY = y + (h - 32.0f * countScale) * 0.5f;
		tr->DrawText(countBuf, { x + width - padX - countW, countY }, countScale,
			{ l.color.x, l.color.y, l.color.z, alpha }, 1.5f, outline);

		y -= kLineHeight;
	}
}
