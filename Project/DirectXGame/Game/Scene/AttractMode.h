#pragma once

/// <summary>
/// デモモード（展示用の自動進行）。Debug の ImGui（Title Tuning）で開始・終了する。Release の exe でも起動時は無効（2026-11-10。展示はタイトルの放置デモ TitleDemoMovie）。
/// 2026-11-24 から F8 は展示ロック、F9 は全能モードに使う（どちらもこのクラスが持つ。下の「展示ロック」「全能モード」）。
/// 有効な間、タイトルは一定間隔で Press → スタートと自動で進んで HUB に入り、「搭乗」→ 乗り込み → ステージ選択から下の一周を繰り返す
/// （18_HubTransitions.md「デモモード」）：
///   ステージ選択「ステージ1」→ 器の間で純度を 9.0 まで上げて決定（19 番 §4.7）→ 降下 → 機体確認 → 射出 → 雲の上 → 2 秒後に死ぬ
///   → ゲームオーバー「もう一度」→ 2 秒後に死ぬ → ゲームオーバー「引き上げる」→ ステージ選択（2026-10-27 ユーザー）
///   dieInStage を false にすると、前の流れ（2 秒遊ぶ → ポーズ「ステージセレクトに戻る」→「はい」）に戻る
///   1 周ごとに一人称と三人称を交互にする（三人称の周はカタパルトに着いてから視点を切り替える。StageStartPart）
/// 人がボタンを押せば普通に進む。シーンをまたいで状態を保つのでシングルトン。
/// 待ち時間は実時間（スロー・一時停止の影響を受けない）で、トランジションの最中は数えない。
/// シングルトンにする理由：デモモードの状態（周回・死んだ回数）がシーンをまたいで続くため。
/// </summary>
class AttractMode {
public:
	/// <summary>シングルトンのインスタンスを取得</summary>
	/// <returns>唯一のインスタンス</returns>
	static AttractMode* GetInstance();

	/// <summary>デモモードが有効か</summary>
	/// <returns>有効なら true</returns>
	bool IsEnabled() const { return enabled_; }
	/// <summary>デモモードの有効・無効を切り替える（無効から有効にした時は一人称の周・死亡回数 0 から始める）</summary>
	/// <param name="enabled">有効にするなら true</param>
	void SetEnabled(bool enabled);
	/// <summary>デモモードの有効・無効を反転する（2026-11-24 から F8 は展示ロックに使うので、Debug の ImGui から）</summary>
	void Toggle() { SetEnabled(!enabled_); }

	/// <summary>
	/// 「デモプレイ中　F8 で通常モードに戻る」を左上に描く（有効な間だけ。Flush まで行う）。
	/// Game::Draw がシーンとトランジションを描いた後に、シーンの RT へ描く（どのシーンでも出す）
	/// </summary>
	void DrawLabel() const;

	//====================
	// 展示ロック（F8。展示会で来場者がタイトルを進めないように）
	//====================
	/// <summary>展示ロック中か（タイトルは入力を受け付けず、放置デモの映像は触っても止まらない）</summary>
	/// <returns>ロック中なら true（Release の exe はロック中で起動する）</returns>
	bool IsExhibitionLocked() const { return exhibitionLocked_; }
	/// <summary>展示ロックの有効・無効を反転する（F8。どのシーンからでも。Game::Update）</summary>
	void ToggleExhibitionLock() { exhibitionLocked_ = !exhibitionLocked_; }

	//====================
	// 全能モード（F9。動画撮影・プレイ会で見せたい所をすぐ見せる）
	//====================
	/// <summary>全能モードか（必殺技のゲージは常に満タン・クールタイム無し・F で Storm Lock / G で Shatterline）</summary>
	/// <returns>全能モードなら true</returns>
	bool IsShowcase() const { return showcase_; }
	/// <summary>全能モードの有効・無効を反転する（F9。タイトルの間だけ受け付ける。TitleScene）</summary>
	void ToggleShowcase() { showcase_ = !showcase_; }

	/// <summary>
	/// 展示ロック・全能モードの案内を右上に描く（どちらかが有効な間だけ。Flush まで行う）。
	/// タイトルの放置デモの映像より上に出すので、TitleScene の描画の最後で呼ぶ
	/// </summary>
	/// <param name="screenWidth">UI の画面の幅 [px]（右寄せに使う）</param>
	void DrawExhibitionLabels(float screenWidth) const;

	/// <summary>自動で進める時の待ち時間など（Debug では Title Tuning → Attract で調整）</summary>
	struct Settings {
		float titleIdleWait = 4.0f;       // タイトル①：ロゴの登場が終わってから Press するまで [s]
		float titleReadyWait = 2.0f;      // タイトル⑤：メニューが出てから「スタート」を決定するまで [s]
		float hubWait = 2.5f;             // HUB の待機：「搭乗」を選ぶまで（神殿を見せる）[s]
		float menuWait = 1.0f;            // ステージ選択・ポーズ：選べるようになってからカーソルを動かすまで [s]
		float menuConfirmDelay = 0.7f;    // カーソルを動かしてから決定するまで（全部のメニュー共通）[s]
		float stageActionTime = 2.0f;     // ステージ1：操作を受け付けてから死ぬ（またはポーズを開く）まで [s]
		float betRaiseDelay = 1.0f;       // 器の間：入ってから針を上げ始めるまで [s]
		float betStepInterval = 0.05f;    // 器の間：純度を 0.1 上げる間隔 [s]
		float betConfirmWait = 1.5f;      // 器の間：9.0 に着いてから決定するまで [s]
		float betTargetPurity = 9.0f;     // 器の間：デモで選ぶ純度
		bool skipDescent = true;          // HUB のリフトの降下を LSHIFT の長押しのスキップで飛ばす（雲の上から。2026-10-27 ユーザー）
		bool dieInStage = true;           // ステージで 2 秒後に死ぬ（false ならポーズでステージ選択へ戻る＝前の流れ）
		int retriesPerLap = 1;            // ゲームオーバーで「もう一度」を選ぶ回数（超えたら「引き上げる」）
	};

	/// <summary>待ち時間などの設定（書き換える時はコピーして SetSettings で戻す）</summary>
	/// <returns>今の設定</returns>
	const Settings& GetSettings() const { return settings_; }
	/// <summary>待ち時間などの設定をまとめて差し替える</summary>
	/// <param name="settings">新しい設定</param>
	void SetSettings(const Settings& settings) { settings_ = settings; }

	//====================
	// 周回（ステージ選択 → 死亡 → ゲームオーバー → … → ステージ選択 を 1 周と数える）
	//====================
	/// <summary>この周で 1 回死んだことを数える（ゲームオーバー画面の入口で呼ぶ）</summary>
	void RecordLapDeath() { ++lapDeaths_; }
	/// <summary>この周の死んだ回数が「もう一度」を選べる回数を超えたか</summary>
	/// <returns>超えていれば true（ゲームオーバーで「引き上げる」を選ぶ）</returns>
	bool ShouldWithdraw() const { return lapDeaths_ > settings_.retriesPerLap; }
	/// <summary>周を終える（死んだ回数を数え直し、次の周は視点を入れ替える）</summary>
	void FinishLap() { lapDeaths_ = 0; ToggleThirdPersonLap(); }
	/// <summary>次の周の視点を入れ替える（一人称 ↔ 三人称）</summary>
	void ToggleThirdPersonLap() { thirdPersonLap_ = !thirdPersonLap_; }
	/// <summary>今の周のスタート演出を三人称で見せるか</summary>
	/// <returns>三人称なら true（最初の周は一人称）</returns>
	bool IsThirdPersonLap() const { return thirdPersonLap_; }
	/// <summary>今の周の視点を決める（Debug の調整画面から）</summary>
	/// <param name="thirdPerson">三人称にするなら true</param>
	void SetThirdPersonLap(bool thirdPerson) { thirdPersonLap_ = thirdPerson; }

private:
	AttractMode();
	AttractMode(const AttractMode&) = delete;
	AttractMode& operator=(const AttractMode&) = delete;
	bool enabled_ = false;
	// 展示ロック中か（Release の exe はロック中で起動する。コンストラクタで決める）
	bool exhibitionLocked_ = false;
	// 全能モードか
	bool showcase_ = false;
	Settings settings_;
	// この周で死んだ回数
	int lapDeaths_ = 0;
	// 今の周のスタート演出を三人称で見せるか
	bool thirdPersonLap_ = false;
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

	/// <summary>経過時間とカーソル移動済みの印を消して最初から数え直す</summary>
	void Reset() { time_ = 0.0f; moved_ = false; }
	/// <summary>時間を進めて、このフレームにやること（待つ・カーソルを動かす・決定）を返す</summary>
	/// <param name="deltaTime">経過時間 [秒]（実時間）</param>
	/// <param name="wait">カーソルを動かすまでの待ち時間 [秒]</param>
	/// <param name="confirmDelay">カーソルを動かしてから決定するまでの時間 [秒]</param>
	/// <returns>このフレームの段階（MoveCursor は 1 回だけ、Confirm は Reset まで毎フレーム）</returns>
	Step Advance(float deltaTime, float wait, float confirmDelay);

private:
	float time_ = 0.0f;
	bool moved_ = false;
};
