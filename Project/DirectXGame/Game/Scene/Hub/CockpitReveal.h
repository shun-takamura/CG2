#pragma once

class JsonValue;

/// <summary>
/// コクピットの起動（18_HubTransitions.md ①の IN）とシャットダウン（②の OUT）の時間軸。
/// 時刻から「灯りの強さ・外が透ける割合（六角形）・ラジアルブラー・背景 UI とメニューの出・表示板の LOCK」に変える。
/// 描画は持たない（HubScene が CockpitLighting / CockpitConsole / HatchSign / PostEffect へ渡す）。
/// 調整値は Hub.json の "cockpitReveal"。
/// </summary>
class CockpitReveal {
public:
	enum class Phase {
		Idle,           // 乗り込む前（ハッチが開いている）
		Booting,        // 閉じ切ってから起動中（入力を受け付ける所まで含む）
		ShuttingDown,   // シャットダウン中
		Off,            // シャットダウンが終わった
	};

	/// <summary>ある時刻の見え方</summary>
	struct State {
		float lightLevel = 0.0f;       // コクピットの灯り（緑のポイントライト・光る部位）0..1
		float reveal = 0.0f;           // 外が透ける割合（六角形のディゾルブ）0..1
		float blurWidth = 0.0f;        // ラジアルブラーの強さ（RadialBlurEffect::SetBlurWidth。0 で切る）
		float backgroundAlpha = 0.0f;  // 背景 UI（上下の枠・中央の円の計器）の出 0..1（チラつき込み）
		float menuTime = -1.0f;        // メニューが出始めてからの秒（負はまだ）
		float collapse = 0.0f;         // シャットダウンで UI が潰れる進み 0..1（1 で消え切った）
		bool signLocked = false;       // 表示板を HATCH LOCKED にする
		float signAlpha = 1.0f;        // 表示板の見え方 0..1（六角形が広がる前に消し、シャットダウンで外を映す壁が戻ったら LOCKED のまま出す）
		bool inputEnabled = false;     // メニューの入力を受け付ける
	};

	CockpitReveal();
	void ResetToDefaults();

	void Reset();           // 乗り込む前（Idle）に戻す
	void StartBoot();       // 閉じ切った所から起動する
	void StartShutdown();   // 起動し終えた所からシャットダウンする
	void Update(float deltaTime);

	Phase GetPhase() const { return phase_; }
	bool IsShutdownFinished() const { return phase_ == Phase::Off; }
	State Evaluate() const;

	/// <summary>
	/// 蛍光灯が点くようなチカチカ（0..1）。t &lt; 0 は 0、seconds を過ぎたら 1。
	/// コクピットの UI・表示板の切り替わりでも使う
	/// </summary>
	static float Flicker(float t, float seconds);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	Phase phase_ = Phase::Idle;
	float time_ = 0.0f;   // 今の段階に入ってからの秒

	// 調整値：乗り込む前
	float idleLightLevel_ = 0.25f;     // ハッチが開いている間の灯り（外から中がかすかに緑に見える）

	// 調整値：起動（閉じ切ってからの秒）
	float lockTime_ = 0.15f;           // 表示板が HATCH LOCKED に変わる
	float signHideTime_ = 0.75f;       // 表示板が消え始める（六角形が広がる前に消し切る）
	float signHideSeconds_ = 0.2f;     // 消えるまで（チカチカ）
	float lightsOnTime_ = 0.25f;       // 灯りが点き始める
	float lightsOnSeconds_ = 0.6f;     // 点き切るまで（チカチカ）
	float hexStartTime_ = 1.0f;        // 六角形が広がり始める
	float hexSeconds_ = 1.4f;          // 晴れ切るまで
	float backgroundTime_ = 2.5f;      // 背景 UI が出る（ラジアルブラーはこの直前に上がり切る）
	float blurRiseSeconds_ = 0.15f;    // ラジアルブラーを上げる秒（線形）
	float menuTime_ = 2.8f;            // 文字のステージ選択が出る（ラジアルブラーはここから下げる）
	float blurFallSeconds_ = 0.2f;     // ラジアルブラーを下げる秒（線形）
	float blurMax_ = 0.035f;           // ラジアルブラーの強さの上限
	float inputDelay_ = 0.35f;         // メニューが出てから入力を受け付けるまで

	// 調整値：シャットダウン（始めてからの秒）
	float collapseSeconds_ = 0.4f;     // UI がブラウン管のように潰れて消えるまで
	float hexCloseStartTime_ = 0.3f;   // 外を映していた壁が戻り始める
	float hexCloseSeconds_ = 0.6f;
	float signShowTime_ = 0.9f;        // 表示板がもう一度出る（ハッチはまだ閉じているので LOCKED のまま。UNLOCK はハッチが開くとき）
	float lightsOffTime_ = 0.9f;       // 灯りが落ち始める
	float lightsOffSeconds_ = 0.3f;
	float shutdownEndTime_ = 1.3f;     // 終わり（持ち主が次へ進む）
};
