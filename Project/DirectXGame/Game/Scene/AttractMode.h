#pragma once

/// <summary>
/// デモモード（展示用の自動進行）。F8 で開始・終了する（Game::Update）。**Release の exe は起動したときから有効**。
/// 有効な間、タイトルは一定間隔で Press → スタートと自動で進んで HUB に入り、「搭乗」→ 乗り込み → ステージ選択から下の一周を繰り返す
/// （18_HubTransitions.md「デモモード」）：
///   ステージ選択「ステージ1」→ 降下 → 機体確認 → 射出 → 雲の上 → 2 秒遊ぶ → ポーズ「ステージセレクトに戻る」→「はい」→ ステージ選択
///   1 周ごとに一人称と三人称を交互にする（三人称の周はカタパルトに着いてから視点を切り替える。StageStartPart）
/// 人がボタンを押せば普通に進む。シーンをまたいで状態を保つのでシングルトン。
/// 待ち時間は実時間（スロー・一時停止の影響を受けない）で、トランジションの最中は数えない。
/// </summary>
class AttractMode {
public:
	static AttractMode* GetInstance();

	bool IsEnabled() const { return enabled_; }
	void SetEnabled(bool enabled);
	void Toggle() { SetEnabled(!enabled_); }

	/// <summary>
	/// 「デモプレイ中　F8 で通常モードに戻る」を左上に描く（有効な間だけ。Flush まで行う）。
	/// Game::Draw がシーンとトランジションを描いた後に、シーンの RT へ描く（どのシーンでも出す）
	/// </summary>
	void DrawLabel() const;

	// ----- 自動で押すまでの待ち時間 [s]（Debug では Title Tuning → Attract で調整） -----
	float titleIdleWait = 4.0f;       // タイトル①：ロゴの登場が終わってから Press するまで
	float titleReadyWait = 2.0f;      // タイトル⑤：メニューが出てから「スタート」を決定するまで
	float hubWait = 2.5f;             // HUB の待機：「搭乗」を選ぶまで（神殿を見せる）
	float menuWait = 1.0f;            // ステージ選択・ポーズ：選べるようになってからカーソルを動かすまで
	float menuConfirmDelay = 0.7f;    // カーソルを動かしてから決定するまで（全部のメニュー共通）
	float stageActionTime = 2.0f;     // ステージ1：操作を受け付けてからポーズを開くまで

	/// <summary>今の周のスタート演出を三人称で見せるか（ポーズでステージ選択へ戻るたびに入れ替える。最初の周は一人称）</summary>
	bool thirdPersonLap = false;

private:
	AttractMode();
	bool enabled_ = false;
};

/// <summary>
/// デモモードでメニューを自動で選ぶ時間軸：wait 秒でカーソルを動かし、さらに confirmDelay 秒で決定する。
/// 持ち主はメニューが選べるようになった間だけ Advance を呼び、決定したら（または別のメニューに変わったら）Reset する
/// </summary>
class AttractMenuTimer {
public:
	enum class Step {
		Wait,        // 何もしない
		MoveCursor,  // このフレームでカーソルを動かす（1 回だけ返す）
		Confirm,     // 決定する（Reset するまで毎フレーム返す）
	};

	void Reset() { time_ = 0.0f; moved_ = false; }
	Step Advance(float deltaTime, float wait, float confirmDelay);

private:
	float time_ = 0.0f;
	bool moved_ = false;
};
