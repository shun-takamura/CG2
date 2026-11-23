#pragma once

class JsonValue;

/// <summary>
/// コクピットの起動（18_HubTransitions.md ①の IN）とシャットダウン（②の OUT）の時間軸。
/// 時刻から「灯りの強さ・外が透ける割合（六角形）・背景 UI とメニューの出・表示板の LOCK」に変える。
/// 2026-11-05 ユーザー：UI が出るまでのラジアルブラーは削除し、六角形が広がり始めたらすぐステージ選択を出す。
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
		Arriving,       // 帰着：ステージなどから戻って真っ暗なコクピットに灯りが点く（⑦。終わると Off＝シャットダウンの終わりと同じ見え方）
	};

	/// <summary>ある時刻の見え方</summary>
	struct State {
		float lightLevel = 0.0f;       // コクピットの灯り（緑のポイントライト・光る部位）0..1
		float reveal = 0.0f;           // 外が透ける割合（六角形のディゾルブ）0..1
		float backgroundAlpha = 0.0f;  // 背景 UI（上下の枠・中央の円の計器）の出 0..1（チラつき込み）
		float menuTime = -1.0f;        // メニューが出始めてからの秒（負はまだ）
		float collapse = 0.0f;         // シャットダウンで UI が潰れる進み 0..1（1 で消え切った）
		bool signLocked = false;       // 表示板を HATCH LOCKED にする
		float signAlpha = 1.0f;        // 表示板の見え方 0..1（六角形が広がる前に消し、シャットダウンで外を映す壁が戻ったら LOCKED のまま出す）
		bool inputEnabled = false;     // メニューの入力を受け付ける
		float sunScale = 1.0f;         // 閉じたコクピットに漏れる外の光の倍率（帰着の真っ暗の間だけ 0。墨の黒から途切れずにつなぐ）
	};

	/// <summary>調整値を既定にする</summary>
	CockpitReveal();
	/// <summary>調整値（各段階の秒数・灯りの強さなど）を既定に戻す</summary>
	void ResetToDefaults();

	/// <summary>乗り込む前（Idle）に戻す</summary>
	void Reset();
	/// <summary>閉じ切った所から起動する</summary>
	void StartBoot();
	/// <summary>起動し終えた所からシャットダウンする</summary>
	void StartShutdown();
	/// <summary>真っ暗なコクピットから灯りを点ける（⑦。終わったら Off）</summary>
	void StartArrive();
	/// <summary>時刻を進め、段階の終わりで次の段階へ移る</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);

	/// <summary>今の段階を返す</summary>
	/// <returns>段階</returns>
	Phase GetPhase() const { return phase_; }
	/// <summary>シャットダウン（または帰着）が終わったかを返す。持ち主はここから降りる</summary>
	/// <returns>Off なら true</returns>
	bool IsShutdownFinished() const { return phase_ == Phase::Off; }
	/// <summary>今の段階と時刻から見え方を求める</summary>
	/// <returns>灯り・外の透け具合・UI の出・表示板などの見え方</returns>
	State Evaluate() const;

	/// <summary>蛍光灯が点くようなチカチカを求める（コクピットの UI・表示板の切り替わりでも使う）</summary>
	/// <param name="t">点き始めてからの経過 [秒]</param>
	/// <param name="seconds">点き切るまでの秒数 [秒]</param>
	/// <returns>0〜1。t &lt; 0 は 0、seconds を過ぎたら 1</returns>
	static float Flicker(float t, float seconds);

	/// <summary>調整値（Hub.json の "cockpitReveal"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
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
	float backgroundTime_ = 1.0f;      // 背景 UI が出る
	float menuTime_ = 1.1f;            // 文字のステージ選択が出る
	float inputDelay_ = 0.15f;         // メニューが出てから入力を受け付けるまで

	// 調整値：シャットダウン（始めてからの秒）
	float collapseSeconds_ = 0.4f;     // UI がブラウン管のように潰れて消えるまで
	float hexCloseStartTime_ = 0.3f;   // 外を映していた壁が戻り始める
	float hexCloseSeconds_ = 0.6f;
	float signShowTime_ = 0.9f;        // 表示板がもう一度出る（ハッチはまだ閉じているので LOCKED のまま。UNLOCK はハッチが開くとき）
	float lightsOffTime_ = 0.9f;       // 灯りが落ち始める
	float lightsOffSeconds_ = 0.3f;
	float shutdownEndTime_ = 1.3f;     // 終わり（持ち主が次へ進む）

	// 調整値：帰着（⑦。墨が消えてからの秒）。チカチカの秒数は起動の lightsOnSeconds / signHideSeconds、
	// 灯りが落ちる秒数はシャットダウンの lightsOffSeconds を使う
	float arriveLightsOnTime_ = 0.4f;     // 真っ暗のあと灯りが点き始める（外の光もここから戻す）
	float arriveSignTime_ = 0.7f;         // 表示板 HATCH LOCKED が出る
	float arriveLightsDownTime_ = 1.5f;   // 灯りが待機の明るさへ落ち始める
	float arriveEndTime_ = 1.9f;          // 終わり（Off にして持ち主が降り始める）
};
