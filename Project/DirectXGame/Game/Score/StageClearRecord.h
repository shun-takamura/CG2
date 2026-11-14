#pragma once
#include <cstdint>
#include <string>

/// <summary>
/// クリアの記録（23_StageClear.md §2・§3）。ScoreManager と同じ置き方のシングルトン。
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
	static StageClearRecord* GetInstance();

	/// <summary>ステージの開始・リトライ（StagePlayScene::Initialize）。タイムを 0 に、キャプチャを「無い」に戻す</summary>
	void ResetForNewRun();

	// ----- ステージ名 -----
	void SetStageName(const std::string& name) { stageName_ = name; }
	const std::string& GetStageName() const { return stageName_; }

	// ----- クリアタイム -----
	/// <summary>プレイ中の実時間を足す（クリア済みなら足さない）</summary>
	void AddPlayTime(float seconds) { if (!cleared_ && seconds > 0.0f) clearTime_ += seconds; }
	/// <summary>ボス撃破：タイムを止める</summary>
	void MarkCleared() { cleared_ = true; }
	bool IsCleared() const { return cleared_; }
	float GetClearTime() const { return clearTime_; }

	// ----- 撃破の瞬間のキャプチャ（§3） -----
	/// <summary>
	/// B が撮った後に呼ぶ。textureName は RegisterExternalTexture で付けた名前、srvIndex はその SRV、
	/// width/height は画像の画素数（描画解像度。UI 座標とは違う）
	/// </summary>
	void SetCapture(const std::string& textureName, uint32_t srvIndex, uint32_t width, uint32_t height);
	/// <summary>「無い」に戻す（画像の解放は B の持ち主がする）</summary>
	void ClearCapture();
	bool HasCapture() const { return hasCapture_; }
	const std::string& GetCaptureTextureName() const { return captureTextureName_; }
	uint32_t GetCaptureSrvIndex() const { return captureSrvIndex_; }
	uint32_t GetCaptureWidth() const { return captureWidth_; }
	uint32_t GetCaptureHeight() const { return captureHeight_; }

	void OnImGui();

private:
	StageClearRecord() = default;
	~StageClearRecord() = default;
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
