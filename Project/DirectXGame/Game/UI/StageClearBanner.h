#pragma once

/// <summary>
/// ボス撃破の後の「STAGE CLEAR」の表示（23_StageClear.md §4 B）。
/// 画面中央に暗い帯が左右へ伸びる → 金の細い線が上下を走る → 字間の広い所から詰まりながら文字が出る
/// → 光の筋が横切る → 下に小さくクリアタイム。
/// 文字にブルームは付けない（読みにくくなるため。ユーザー決定）。描画は TextRenderer に積むだけで、Flush は呼び出し側。
/// </summary>
class StageClearBanner {
public:
	/// <summary>表示を最初から始める</summary>
	/// <param name="clearTimeSec">下に出すクリアタイム [秒]</param>
	void Start(float clearTimeSec);
	/// <summary>表示をやめる（非表示に戻す）</summary>
	void Reset();
	/// <summary>表示中なら経過時間を進める</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void Update(float dt);
	/// <summary>表示中か</summary>
	/// <returns>表示中なら true</returns>
	bool IsActive() const { return active_; }
	/// <summary>表示を始めてからの経過時間を取得する</summary>
	/// <returns>経過時間 [秒]</returns>
	float GetTime() const { return time_; }

	/// <summary>帯・金の線・文字・光の筋・クリアタイムを経過時間に合わせて TextRenderer に積む</summary>
	/// <param name="screenW">UI 座標の画面の幅</param>
	/// <param name="screenH">UI 座標の画面の高さ</param>
	void Draw(float screenW, float screenH) const;

	/// <summary>表示の調整値を ImGui に出す（再生のボタン付き）</summary>
	void OnImGui();

private:
	bool  active_ = false;
	float time_ = 0.0f;
	float clearTime_ = 0.0f;

	// 帯
	float centerYRatio_ = 0.42f;   // 帯の中心の高さ（画面の割合）
	float bandHeight_   = 130.0f;
	float bandOpenTime_ = 0.35f;   // 帯が伸びきるまで [s]
	float bandAlpha_    = 0.62f;
	// 金の線
	float lineStart_    = 0.10f;
	float lineTime_     = 0.45f;
	float lineWidthRatio_ = 0.72f; // 画面幅に対する長さ
	float lineGap_      = 8.0f;    // 帯の縁から線までの隙間
	// 文字
	float textScale_     = 3.2f;
	float textStart_     = 0.20f;
	float textTime_      = 0.65f;  // 字間が詰まりきるまで
	float textSpacingFrom_ = 46.0f; // 出始めの字間の足し [px]
	float textSpacingTo_   = 6.0f;  // 落ち着いた字間の足し [px]
	float textFontPx_    = 32.0f;   // フォントの基準の高さ（縦の中央合わせ）
	// 光の筋
	float sweepStart_ = 1.0f;
	float sweepTime_  = 0.7f;
	float sweepWidth_ = 70.0f;
	float sweepAlpha_ = 0.22f;
	// クリアタイム
	float subStart_ = 0.9f;
	float subTime_  = 0.4f;
	float subScale_ = 1.0f;
};
