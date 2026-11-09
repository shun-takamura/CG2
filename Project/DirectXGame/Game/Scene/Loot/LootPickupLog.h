#pragma once
#include "Vector4.h"

#include <string>
#include <vector>

/// <summary>
/// 拾った物を画面の右下に出す（22_ItemDrop.md）。「天輪の歯車 ×3」「エーテル +45」のような行を下から積み、
/// 同じ物を続けて拾ったら行を足さず数だけ増やす。holdTime 秒たつと右へ抜けて消える。
/// LootLedger の「最近拾った物」を毎フレーム取り出して作る。文字は TextRenderer（Flush は呼び出し側）。
/// </summary>
class LootPickupLog {
public:
	/// <summary>dt は実時間（スローでも表示は普通の速さ）</summary>
	void Update(float dt);
	void Draw(float screenWidth, float screenHeight) const;
	void Clear() { lines_.clear(); }

private:
	struct Line {
		std::string itemId;
		std::string label;
		Vector4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
		int count = 0;
		float age = 0.0f;      // 出てから（数が増えると holdTime を数え直す）
		float bump = 0.0f;     // 数が増えた瞬間に少し大きくする 1→0
	};

	std::vector<Line> lines_;

	// 調整値（仮。画面は UI の仮想解像度）
	static constexpr int   kMaxLines = 5;
	static constexpr float kHoldTime = 2.0f;
	static constexpr float kFadeIn = 0.15f;
	static constexpr float kFadeOut = 0.35f;
	static constexpr float kRightMargin = 40.0f;
	static constexpr float kBottomMargin = 150.0f;
	static constexpr float kLineHeight = 40.0f;
	static constexpr float kTextScale = 0.6f;
};
