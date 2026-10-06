#pragma once

/// <summary>
/// デモモード（展示用の自動進行）。F8 で開始・終了する（Game::Update）。
/// 有効な間、タイトルは一定間隔で Press → スタートと自動で進み、HUB は「タイトルに戻る」を自動で選んでループする。
/// 人がボタンを押せば普通に進む。シーンをまたいで状態を保つのでシングルトン。
/// </summary>
class AttractMode {
public:
	static AttractMode* GetInstance();

	bool IsEnabled() const { return enabled_; }
	void SetEnabled(bool enabled) { enabled_ = enabled; }
	void Toggle() { enabled_ = !enabled_; }

	// ----- 自動で押すまでの待ち時間 [s]（Debug では Title Tuning → Attract で調整） -----
	float titleIdleWait = 4.0f;     // タイトル①：ロゴの登場が終わってから Press するまで
	float titleReadyWait = 2.0f;    // タイトル⑤：メニューが出てから「スタート」を決定するまで
	float hubWait = 2.5f;           // HUB：シーン開始からカーソルを「タイトルに戻る」へ動かすまで
	float hubConfirmDelay = 0.8f;   // HUB：カーソルを動かしてから決定するまで

private:
	AttractMode() = default;
	bool enabled_ = false;
};
