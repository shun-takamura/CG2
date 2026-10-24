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
	/// 初期化
	/// </summary>
	/// <param name="screenWidth">画面幅。呼び出し側が実ウィンドウサイズを渡すこと</param>
	/// <param name="screenHeight">画面高さ</param>
	virtual void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth, float screenHeight) = 0;

	/// <summary>
	/// 終了処理
	/// </summary>
	virtual void Finalize() = 0;

	/// <summary>
	/// 更新（状態を進めてから見た目を更新する）
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	virtual void Draw() = 0;

	/// <summary>
	/// トランジション開始（覆うところから）
	/// </summary>
	/// <param name="onSceneChange">覆い切ったら1回呼ぶ（シーンの差し替え＝ロード）</param>
	/// <param name="playReveal">false なら覆い切って Hold を抜けたところで終わる（開く側は別の種類が担当する）</param>
	void Start(std::function<void()> onSceneChange, bool playReveal = true);

	/// <summary>
	/// 覆い切った状態から開くところだけ再生する（別の種類が覆った後の引き継ぎ用）
	/// </summary>
	void StartReveal();

	/// <summary>
	/// 覆い切った状態の Hold から始める（覆う側をシーンが自分で演じ終えたとき。TransitionManager::kCoverByScene）。
	/// 次のフレームでシーンを差し替え、Hold の秒数が経ったら開く（playReveal が false なら開かずに終わる）
	/// </summary>
	void StartCovered(std::function<void()> onSceneChange, bool playReveal = true);

	/// <summary>
	/// 残りを飛ばす（入力でスキップ。19_GameOverAndDifficulty.md §4.2.3）。
	/// 覆う途中なら覆い切った所へ飛んでシーンを差し替え、開く途中ならそこで開き切って終わる。一度呼べば最後まで飛ぶ
	/// </summary>
	void RequestSkip() { if (isTransitioning_) skipRequested_ = true; }
	bool IsSkipRequested() const { return skipRequested_; }

	/// <summary>
	/// トランジション中かどうか
	/// </summary>
	bool IsTransitioning() const { return isTransitioning_; }

	/// <summary>
	/// 現在の状態を取得
	/// </summary>
	TransitionState GetState() const { return state_; }

	/// <summary>
	/// トランジションの種類を取得
	/// </summary>
	virtual TransitionType GetType() const = 0;

	/// <summary>
	/// トランジション名を取得
	/// </summary>
	virtual std::string GetName() const = 0;

	/// <summary>
	/// 隠したままにする最低の秒数（シーンの差し替えが終わってから数える）
	/// </summary>
	void SetHoldDuration(float seconds) { holdDuration_ = seconds; }

	/// <summary>
	/// 調整値を JSON から読む / JSON へ書く（TransitionManager が Transition.json の GetName() の欄を渡す）。
	/// 調整値を持たないトランジションは何もしない
	/// </summary>
	virtual void LoadTuning(const JsonValue& /*json*/) {}
	virtual void SaveTuning(JsonValue& /*json*/) const {}

protected:
	/// <summary>覆う動きを始める前の準備（進行度を 0 に戻す等）</summary>
	virtual void OnStartCover() {}
	/// <summary>覆う動きを1フレーム進める。覆い切ったら true</summary>
	virtual bool UpdateCover(float dt) = 0;
	/// <summary>開く動きを1フレーム進める。開き切ったら true</summary>
	virtual bool UpdateReveal(float dt) = 0;
	/// <summary>覆い切った見た目にする（StartReveal の最初に呼ぶ）</summary>
	virtual void SetFullyCovered() = 0;
	/// <summary>状態に関係なく毎フレーム見た目を更新する（スプライトの位置・色など）</summary>
	virtual void UpdateVisual() {}
	/// <summary>トランジションが終わったとき（開き切った / 開かずに終わった）</summary>
	virtual void OnFinished() {}

	/// <summary>
	/// トランジションを進める dt。ロードしたフレームは dt が 200ms を超えるので、
	/// そのまま使うと覆う・開く動きが飛ぶ。上限で切る
	/// </summary>
	float GetStepDeltaTime() const;

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

private:
	void Finish();

	static constexpr float kMaxStepDeltaTime = 1.0f / 30.0f;

	// false なら Hold を抜けたところで終わる（開く側は別の種類）
	bool playReveal_ = true;
	// シーンの差し替えが終わってから数える Hold の経過秒
	float holdTime_ = 0.0f;
	// RequestSkip された（終わるまで残りを飛ばす）
	bool skipRequested_ = false;
};
