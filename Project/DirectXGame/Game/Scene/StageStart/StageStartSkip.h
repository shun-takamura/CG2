#pragma once
#include "Vector2.h"
#include "Vector4.h"

/// <summary>
/// スタート演出のスキップ（16_StageStart.md、F5）。2026-10-19 ユーザー決定：
///   HUB のリフトの降下中と、StagePlay の降下・機体確認の間に、LSHIFT（入力アクション SkipIntro）を 1.5 秒長押しすると
///   雲の上（S4、F6）へ飛ぶ。途中で離すと戻る。飛ぶときは白フェードを挟む。
///   UI は画面の右上（2026-10-19 ユーザー：左上から変更）に白い文字＋黒の縁取り：押していないときは [LSHIFT] SKIP、押している間は円形のゲージ＋ SKIP
///   （TextRenderer は傾いた四角を描けないので、小さい四角を円に並べて時計回りに埋める）。
/// HUB（HubScene）と StagePlay（StageStartPart）の両方から使うので、調整値は専用の JSON（kPath）に持つ。
/// </summary>
class StageStartSkip {
public:
	static constexpr const char* kPath = "Resources/Json/Tuning/StageStartSkip.json";

	/// <summary>調整値を読む</summary>
	StageStartSkip();
	/// <summary>調整値を kPath（StageStartSkip.json）から読む（無ければ既定値のまま）</summary>
	void Load();
	/// <summary>調整値を kPath（StageStartSkip.json）へ書く</summary>
	void Save() const;

	/// <summary>長押しの時間と済んだ印を戻す</summary>
	void Reset() { held_ = 0.0f; done_ = false; }
	/// <summary>長押しの時間を数える（途中で離すと戻る）</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	/// <param name="held">押しているか</param>
	/// <returns>長押しが済んだフレームだけ true（以後は Reset まで false）</returns>
	bool Update(float deltaTime, bool held);
	/// <summary>長押しの進みを返す</summary>
	/// <returns>0〜1</returns>
	float GetProgress() const;
	/// <summary>長押しが済んだかを返す</summary>
	/// <returns>済んでいれば true</returns>
	bool IsDone() const { return done_; }

	/// <summary>画面の右上に UI を積む（TextRenderer。Flush はシーン）</summary>
	/// <param name="screenWidth">画面の幅 [px]</param>
	/// <param name="alpha">全体の不透明度 0〜1</param>
	void Draw(float screenWidth, float alpha = 1.0f) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	float held_ = 0.0f;
	bool done_ = false;
	bool pressed_ = false;   // 今押している（UI の切り替え）

	// 調整値
	float holdSeconds_ = 1.5f;
	Vector2 margin_ = { 28.0f, 24.0f };       // 画面の右上からの余白 [px]（右端にそろえる）
	float textScale_ = 0.75f;
	float outline_ = 2.0f;                    // 黒の縁取りの太さ
	float ringRadius_ = 15.0f;                // 円形のゲージの半径 [px]
	int ringSegments_ = 28;
	float ringDot_ = 4.0f;                    // ゲージの四角の大きさ [px]
	Vector4 color_ = { 1.0f, 1.0f, 1.0f, 1.0f };
	Vector4 outlineColor_ = { 0.0f, 0.0f, 0.0f, 1.0f };
};
