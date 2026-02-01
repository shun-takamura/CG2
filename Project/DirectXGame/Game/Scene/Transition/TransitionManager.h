#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>
#include <functional>
#include "BaseTransition.h"

class SpriteManager;
class DirectXCore;

/// <summary>
/// トランジション履歴の記録
/// </summary>
struct TransitionRecord {
	TransitionType type;			// 使用したトランジションの種類
	std::string transitionName;		// トランジション名
	std::string fromScene;			// 遷移元シーン名
	std::string toScene;			// 遷移先シーン名
};

/// <summary>
/// トランジション管理クラス
/// </summary>
class TransitionManager {
public:
	/// <summary>
	/// シングルトンインスタンスの取得
	/// </summary>
	static TransitionManager* GetInstance();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize(SpriteManager* spriteManager, DirectXCore* dxCore,
		float screenWidth = 1280.0f, float screenHeight = 720.0f);

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize();

	/// <summary>
	/// 更新
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	void Draw();

	/// <summary>
	/// トランジションを登録
	/// </summary>
	/// <param name="type">トランジションの種類</param>
	/// <param name="transition">トランジションのインスタンス</param>
	void RegisterTransition(TransitionType type, std::unique_ptr<BaseTransition> transition);

	/// <summary>
	/// 指定したトランジションで遷移開始
	/// </summary>
	/// <param name="type">トランジションの種類</param>
	/// <param name="fromScene">遷移元シーン名</param>
	/// <param name="toScene">遷移先シーン名</param>
	/// <param name="onSceneChange">シーン切り替え時のコールバック</param>
	void StartTransition(TransitionType type, const std::string& fromScene,
		const std::string& toScene, std::function<void()> onSceneChange);

	/// <summary>
	/// ランダムなトランジションで遷移開始
	/// </summary>
	void StartRandomTransition(const std::string& fromScene,
		const std::string& toScene, std::function<void()> onSceneChange);

	/// <summary>
	/// トランジション中かどうか
	/// </summary>
	bool IsTransitioning() const;

	/// <summary>
	/// 現在のトランジションを取得
	/// </summary>
	BaseTransition* GetCurrentTransition() const { return currentTransition_; }

	/// <summary>
	/// 現在のトランジションの種類を取得
	/// </summary>
	TransitionType GetCurrentTransitionType() const;

	/// <summary>
	/// 最後に使用したトランジションの記録を取得
	/// </summary>
	const TransitionRecord& GetLastTransitionRecord() const { return lastRecord_; }

	/// <summary>
	/// トランジション履歴を取得
	/// </summary>
	const std::vector<TransitionRecord>& GetTransitionHistory() const { return history_; }

	/// <summary>
	/// 履歴をクリア
	/// </summary>
	void ClearHistory() { history_.clear(); }

	/// <summary>
	/// 登録されているトランジションの種類一覧を取得
	/// </summary>
	std::vector<TransitionType> GetAvailableTransitions() const;

	/// <summary>
	/// ImGuiデバッグ表示
	/// </summary>
	void OnImGui();

	/// <summary>
	/// 特定のトランジションを取得（設定変更用）
	/// </summary>
	template<typename T>
	T* GetTransition(TransitionType type) {
		auto it = transitions_.find(type);
		if (it != transitions_.end()) {
			return dynamic_cast<T*>(it->second.get());
		}
		return nullptr;
	}

private:
	TransitionManager() = default;
	~TransitionManager() = default;
	TransitionManager(const TransitionManager&) = delete;
	TransitionManager& operator=(const TransitionManager&) = delete;

	// 登録されたトランジション
	std::unordered_map<TransitionType, std::unique_ptr<BaseTransition>> transitions_;

	// 現在アクティブなトランジション
	BaseTransition* currentTransition_ = nullptr;

	// 最後のトランジション記録
	TransitionRecord lastRecord_;

	// トランジション履歴
	std::vector<TransitionRecord> history_;

	// マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	float screenWidth_ = 1280.0f;
	float screenHeight_ = 720.0f;
};