#pragma once
#include <functional>
#include <string>

// 前方宣言
class SpriteManager;
class DirectXCore;
class JsonValue;

/// <summary>
/// トランジションの状態
/// </summary>
enum class TransitionState {
	None,		// 何もしていない
	FadeIn,		// 画面を覆う
	Hold,		// 完全に覆われた状態（ここでシーンを差し替える）
	FadeOut		// 覆いが消えていく
};

/// <summary>
/// トランジションの種類を識別するID
/// </summary>
enum class TransitionType {
	None,
	Stripe,			// スマブラ風斜めストライプ
	Fade,			// フェードイン/アウト
	Circle,			// 円形ワイプ
	Ink,			// 墨の雫（画面を水面に見立てて黒が広がる）
	FeatherLand,	// 羽根の着水（死亡 → ゲームオーバー）
	FeatherRise,	// 羽根の飛翔（リトライ → ステージ）
	// 今後追加するトランジションはここに追加
};

/// <summary>
/// トランジション基底クラス。
/// 覆う（FadeIn）→ 隠したまま（Hold）→ 開く（FadeOut）の進め方は基底が持ち、
/// 派生は「覆う1フレーム」「開く1フレーム」「覆い切った見た目」「描画」だけを書く。
/// 覆う側と開く側を別の種類にするときは、覆う側を開かずに終わらせ、開く側を覆い切った所から始める。
/// </summary>
class BaseTransition {
public:
	BaseTransition() = default;
	virtual ~BaseTransition() = default;

	/// <summary>
	/// 共通の受け取り（マネージャーと画面サイズ）をしてから、派生の OnInitialize を呼ぶ。
	/// 派生は Initialize を上書きせず OnInitialize に書く（テンプレートメソッド）
	/// </summary>
	/// <param name="spriteManager">スプライトの描画に使うマネージャー</param>
	/// <param name="dxCore">GPU リソースの作成と経過時間に使う</param>
	/// <param name="screenWidth">画面幅。呼び出し側が実ウィンドウサイズを渡すこと</param>
	/// <param name="screenHeight">画面高さ</param>
	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth, float screenHeight);

	/// <summary>派生が作ったスプライト・GPU リソースを解放する</summary>
	virtual void Finalize() = 0;

	/// <summary>
	/// 状態（覆う → 隠したまま → 開く）を 1 フレーム進め、最後に UpdateVisual で見た目を更新する。
	/// 覆い切った次のフレームでシーンを差し替え、隠したままの秒数が経ったら開く
	/// </summary>
	void Update();

	/// <summary>トランジションを描く</summary>
	virtual void Draw() = 0;

	/// <summary>覆うところからトランジションを始める（トランジション中なら何もしない）</summary>
	/// <param name="onSceneChange">覆い切ったら 1 回呼ぶ（シーンの差し替え＝ロード）</param>
	/// <param name="playReveal">false なら覆い切って Hold を抜けたところで終わる（開く側は別の種類が担当する）</param>
	void Start(std::function<void()> onSceneChange, bool playReveal = true);

	/// <summary>覆い切った状態から開くところだけ再生する（別の種類が覆った後の引き継ぎ用）</summary>
	void StartReveal();

	/// <summary>
	/// 覆い切った状態の Hold から始める（覆う側をシーンが自分で演じ終えたとき。TransitionManager::kCoverByScene）。
	/// 次のフレームでシーンを差し替え、Hold の秒数が経ったら開く。トランジション中なら何もしない
	/// </summary>
	/// <param name="onSceneChange">次のフレームで 1 回呼ぶ（シーンの差し替え＝ロード）</param>
	/// <param name="playReveal">false なら開かずに終わる</param>
	void StartCovered(std::function<void()> onSceneChange, bool playReveal = true);

	/// <summary>
	/// 残りを飛ばす（入力でスキップ。19_GameOverAndDifficulty.md §4.2.3）。
	/// 覆う途中なら覆い切った所へ飛んでシーンを差し替え、開く途中ならそこで開き切って終わる。一度呼べば最後まで飛ぶ。
	/// トランジション中でなければ何もしない
	/// </summary>
	void RequestSkip() { if (isTransitioning_) skipRequested_ = true; }
	/// <summary>スキップが頼まれているかを返す</summary>
	/// <returns>RequestSkip されて、まだ終わっていなければ true</returns>
	bool IsSkipRequested() const { return skipRequested_; }

	/// <summary>トランジション中かどうかを返す</summary>
	/// <returns>覆う・隠したまま・開くのどれかの途中なら true</returns>
	bool IsTransitioning() const { return isTransitioning_; }

	/// <summary>現在の状態を返す</summary>
	/// <returns>覆う / 隠したまま / 開く / 何もしていない</returns>
	TransitionState GetState() const { return state_; }

	/// <summary>トランジションの種類を返す</summary>
	/// <returns>種類</returns>
	virtual TransitionType GetType() const = 0;

	/// <summary>トランジション名を返す（Transition.json の欄の名前にも使う）</summary>
	/// <returns>名前</returns>
	virtual std::string GetName() const = 0;

	/// <summary>隠したままにする最低の秒数を設定する（シーンの差し替えが終わってから数える）</summary>
	/// <param name="seconds">秒数 [秒]</param>
	void SetHoldDuration(float seconds) { holdDuration_ = seconds; }
	/// <summary>隠したままにする最低の秒数を返す</summary>
	/// <returns>秒数 [秒]</returns>
	float GetHoldDuration() const { return holdDuration_; }

	/// <summary>
	/// 調整値を JSON から読む（TransitionManager が Transition.json の GetName() の欄を渡す）。
	/// 調整値を持たないトランジションは何もしない
	/// </summary>
	/// <param name="json">このトランジションの欄</param>
	virtual void LoadTuning([[maybe_unused]] const JsonValue& json) {}
	/// <summary>調整値を JSON へ書く。調整値を持たないトランジションは何もしない</summary>
	/// <param name="json">書き込み先（このトランジションの欄）</param>
	virtual void SaveTuning([[maybe_unused]] JsonValue& json) const {}

protected:
	/// <summary>派生ごとの初期化（スプライト・GPU リソースの作成など）。Initialize から呼ばれ、マネージャーと画面サイズは受け取り済み</summary>
	virtual void OnInitialize() = 0;
	/// <summary>覆う動きを始める前の準備（進行度を 0 に戻す等）</summary>
	virtual void OnStartCover() {}
	/// <summary>覆う動きを 1 フレーム進める</summary>
	/// <param name="dt">経過 [秒]（GetStepDeltaTime で上限を切った値）</param>
	/// <returns>覆い切ったら true</returns>
	virtual bool UpdateCover(float dt) = 0;
	/// <summary>開く動きを 1 フレーム進める</summary>
	/// <param name="dt">経過 [秒]（GetStepDeltaTime で上限を切った値）</param>
	/// <returns>開き切ったら true</returns>
	virtual bool UpdateReveal(float dt) = 0;
	/// <summary>覆い切った見た目にする（StartReveal・StartCovered の最初とスキップ時に呼ぶ）</summary>
	virtual void SetFullyCovered() = 0;
	/// <summary>状態に関係なく毎フレーム見た目を更新する（スプライトの位置・色など）</summary>
	virtual void UpdateVisual() {}
	/// <summary>トランジションが終わったとき（開き切った / 開かずに終わった）に呼ばれる</summary>
	virtual void OnFinished() {}

	/// <summary>
	/// トランジションを進める dt を返す。ロードしたフレームは dt が 200ms を超えるので、
	/// そのまま使うと覆う・開く動きが飛ぶ。上限で切る
	/// </summary>
	/// <returns>経過 [秒]（上限 1/30 秒。DirectXCore が無ければ 1/60 秒）</returns>
	float GetStepDeltaTime() const;

	/// <summary>今の状態（覆う / 隠したまま / 開く）に入ってからの経過秒を返す</summary>
	/// <returns>経過 [秒]</returns>
	float GetPhaseTime() const { return currentTime_; }
	/// <summary>スプライトの描画に使うマネージャーを返す</summary>
	/// <returns>マネージャー</returns>
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	/// <summary>GPU リソースの作成とコマンドリストに使う DirectXCore を返す</summary>
	/// <returns>DirectXCore</returns>
	DirectXCore* GetDxCore() const { return dxCore_; }
	/// <summary>画面幅（Initialize で受け取った実ウィンドウサイズ）を返す</summary>
	/// <returns>幅</returns>
	float GetScreenWidth() const { return screenWidth_; }
	/// <summary>画面高さ（Initialize で受け取った実ウィンドウサイズ）を返す</summary>
	/// <returns>高さ</returns>
	float GetScreenHeight() const { return screenHeight_; }
	/// <summary>何もしていない状態へ戻す（調整画面のテスト再生を止める時）</summary>
	void ResetToIdle();

private:
	/// <summary>何もしていない状態へ戻して OnFinished を呼ぶ</summary>
	void Finish();

	// 状態
	TransitionState state_ = TransitionState::None;
	bool isTransitioning_ = false;

	// タイミング
	float currentTime_ = 0.0f;
	float holdDuration_ = 0.1f;

	// コールバック
	std::function<void()> onSceneChange_;
	bool sceneChangeTriggered_ = false;

	// マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;

	// 画面サイズ（Initialize で実ウィンドウサイズを受け取る）
	float screenWidth_ = 0.0f;
	float screenHeight_ = 0.0f;

	static constexpr float kMaxStepDeltaTime = 1.0f / 30.0f;

	// false なら Hold を抜けたところで終わる（開く側は別の種類）
	bool playReveal_ = true;
	// シーンの差し替えが終わってから数える Hold の経過秒
	float holdTime_ = 0.0f;
	// RequestSkip された（終わるまで残りを飛ばす）
	bool skipRequested_ = false;
};
