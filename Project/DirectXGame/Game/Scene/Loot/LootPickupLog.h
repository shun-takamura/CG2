#pragma once
#include "Vector4.h"

#include <string>
#include <vector>

class LootLedger;

/// <summary>
/// 拾った物を画面の右下に出す（22_ItemDrop.md）。「天輪の歯車 ×3」「エーテル +45」のような行を下から積み、
/// 同じ物を続けて拾ったら行を足さず数だけ増やす。holdTime 秒たつと右へ抜けて消える。
/// LootLedger の「最近拾った物」を毎フレーム取り出して作る。文字は TextRenderer（Flush は呼び出し側）。
/// </summary>
class LootPickupLog {
public:
	/// <summary>LootLedger の「最近拾った物」を取り出して行を足し（同じ物は数を足す）、古い行を消す</summary>
	/// <param name="dt">実時間の経過 [秒]（スローでも表示は普通の速さ）</param>
	/// <param name="ledger">取り出す元の集計（GameSession が持つ物）</param>
	void Update(float dt, LootLedger& ledger);
	/// <summary>行を画面の右下に下から積んで TextRenderer に積む（Flush は呼び出し側）</summary>
	/// <param name="screenWidth">画面の幅（UI の仮想解像度）</param>
	/// <param name="screenHeight">画面の高さ（UI の仮想解像度）</param>
	void Draw(float screenWidth, float screenHeight) const;
	/// <summary>行を全部消す</summary>
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
