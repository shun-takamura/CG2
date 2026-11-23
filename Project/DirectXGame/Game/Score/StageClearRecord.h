#pragma once
#include <cstdint>
#include <string>

/// <summary>
/// クリアの記録（23_StageClear.md §2・§3）。ScoreManager と同じく GameSession が 1 つだけ持つ。
/// StagePlayScene が書き、リザルト（ResultScene）が読む。シーンの切り替えをまたいで残る。
///   - ステージ名・クリアタイム（ステージの開始＝レールに出てからボス撃破まで。ポーズ中・スタート演出中は足さない）
///   - 撃破の瞬間のキャプチャの受け渡し口（§3）。**撮るのは B、読むのは A**
///     B：撮った RT を TextureManager::RegisterExternalTexture で名前を付けて登録し、SetCapture で「有る」にする。
///        RT・SRV の寿命は B（エンジン側の持ち主）が持つ。ステージの開始とリトライでは ResetForNewRun が「無い」に戻す
///     A：HasCapture が false なら仮の背景。true ならテクスチャ名で画面全体に描く
/// ボス戦からのリトライでも StagePlayScene::Initialize で ResetForNewRun が走るので、クリアタイムはその回の分だけになる。
/// </summary>
class StageClearRecord {
public:
	/// <summary>記録無し（キャプチャ無し）で作る</summary>
	StageClearRecord() = default;
	~StageClearRecord() = default;


	/// <summary>ステージの開始・リトライ（StagePlayScene::Initialize）。タイムを 0 に、キャプチャを「無い」に戻す</summary>
	void ResetForNewRun();

	// ----- ステージ名 -----
	/// <summary>ステージ名を設定する</summary>
	/// <param name="name">ステージ名</param>
	void SetStageName(const std::string& name) { stageName_ = name; }
	/// <summary>ステージ名を取得する</summary>
	/// <returns>ステージ名</returns>
	const std::string& GetStageName() const { return stageName_; }

	// ----- クリアタイム -----
	/// <summary>プレイ中の実時間を足す（クリア済みなら足さない）</summary>
	/// <param name="seconds">足す時間 [秒]（0 以下なら足さない）</param>
	void AddPlayTime(float seconds) { if (!cleared_ && seconds > 0.0f) clearTime_ += seconds; }
	/// <summary>ボス撃破：タイムを止める</summary>
	void MarkCleared() { cleared_ = true; }
	/// <summary>ボスを倒してタイムを止めたか</summary>
	/// <returns>止めていれば true</returns>
	bool IsCleared() const { return cleared_; }
	/// <summary>クリアタイムを取得する</summary>
	/// <returns>クリアタイム [秒]</returns>
	float GetClearTime() const { return clearTime_; }

	// ----- 撃破の瞬間のキャプチャ（§3） -----
	/// <summary>
	/// B が撮った後に呼ぶ。textureName は RegisterExternalTexture で付けた名前、srvIndex はその SRV、
	/// width/height は画像の画素数（描画解像度。UI 座標とは違う）
	/// </summary>
	/// <param name="textureName">RegisterExternalTexture で付けた名前</param>
	/// <param name="srvIndex">その SRV の番号</param>
	/// <param name="width">画像の横の画素数</param>
	/// <param name="height">画像の縦の画素数</param>
	void SetCapture(const std::string& textureName, uint32_t srvIndex, uint32_t width, uint32_t height);
	/// <summary>「無い」に戻す（画像の解放は B の持ち主がする）</summary>
	void ClearCapture();
	/// <summary>撃破の瞬間のキャプチャがあるか</summary>
	/// <returns>あれば true</returns>
	bool HasCapture() const { return hasCapture_; }
	/// <summary>キャプチャのテクスチャ名を取得する</summary>
	/// <returns>テクスチャ名</returns>
	const std::string& GetCaptureTextureName() const { return captureTextureName_; }
	/// <summary>キャプチャの SRV の番号を取得する</summary>
	/// <returns>SRV の番号</returns>
	uint32_t GetCaptureSrvIndex() const { return captureSrvIndex_; }
	/// <summary>キャプチャの横の画素数を取得する</summary>
	/// <returns>横の画素数</returns>
	uint32_t GetCaptureWidth() const { return captureWidth_; }
	/// <summary>キャプチャの縦の画素数を取得する</summary>
	/// <returns>縦の画素数</returns>
	uint32_t GetCaptureHeight() const { return captureHeight_; }

	/// <summary>ステージ名・クリアタイム・キャプチャの状態を ImGui に出す</summary>
	void OnImGui();

private:
	StageClearRecord(const StageClearRecord&) = delete;
	StageClearRecord& operator=(const StageClearRecord&) = delete;

	std::string stageName_ = "ステージ1";   // コクピットのメニューの表示と同じ（CockpitConsole）
	float clearTime_ = 0.0f;               // [s]
	bool cleared_ = false;

	bool hasCapture_ = false;
	std::string captureTextureName_;
	uint32_t captureSrvIndex_ = 0;
	uint32_t captureWidth_ = 0;
	uint32_t captureHeight_ = 0;
};
