#pragma once
#include <functional>
#include <string>

// 前方宣言
class SpriteManager;
class DirectXCore;

/// <summary>
/// トランジションの状態
/// </summary>
enum class TransitionState {
	None,		// 何もしていない
	FadeIn,		// 画面を覆う
	Hold,		// 完全に覆われた状態
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
	// 今後追加するトランジションはここに追加
};

/// <summary>
/// トランジション基底クラス
/// </summary>
class BaseTransition {
public:
	BaseTransition() = default;
	virtual ~BaseTransition() = default;

	/// <summary>
	/// 初期化
	/// </summary>
	virtual void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth = 1280.0f, float screenHeight = 720.0f) = 0;

	/// <summary>
	/// 終了処理
	/// </summary>
	virtual void Finalize() = 0;

	/// <summary>
	/// 更新
	/// </summary>
	virtual void Update() = 0;

	/// <summary>
	/// 描画
	/// </summary>
	virtual void Draw() = 0;

	/// <summary>
	/// トランジション開始
	/// </summary>
	virtual void Start(std::function<void()> onSceneChange);

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

protected:
	/// <summary>
	/// 状態を次に進める
	/// </summary>
	void AdvanceState();

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

	// 画面サイズ
	float screenWidth_ = 1280.0f;
	float screenHeight_ = 720.0f;
};
