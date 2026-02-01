#pragma once
#include <memory>
#include <string>
#include "BaseTransition.h"

// 前方宣言
class BaseScene;
class AbstractSceneFactory;
class SpriteManager;
class Object3DManager;
class DirectXCore;
class SRVManager;
class InputManager;

/// <summary>
/// シーン管理クラス
/// </summary>
class SceneManager {
public:
	/// <summary>
	/// シングルトンインスタンスの取得
	/// </summary>
	static SceneManager* GetInstance();

	/// <summary>
	/// 初期化（各マネージャーのポインタを設定）
	/// </summary>
	void Initialize(
		SpriteManager* spriteManager,
		Object3DManager* object3DManager,
		DirectXCore* dxCore,
		SRVManager* srvManager,
		InputManager* input
	);

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
	/// シーン変更（トランジション指定）
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <param name="transitionType">トランジションの種類</param>
	void ChangeScene(const std::string& sceneName, TransitionType transitionType);

	/// <summary>
	/// シーン変更（ランダムトランジション）
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	void ChangeSceneRandom(const std::string& sceneName);

	/// <summary>
	/// シーン変更（トランジションなし）
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	void ChangeSceneImmediate(const std::string& sceneName);

	/// <summary>
	/// シーンファクトリーのセッター
	/// </summary>
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) { sceneFactory_ = sceneFactory; }

	/// <summary>
	/// 現在のシーンを取得
	/// </summary>
	BaseScene* GetCurrentScene() const { return currentScene_.get(); }

	/// <summary>
	/// 現在のシーン名を取得
	/// </summary>
	const std::string& GetCurrentSceneName() const { return currentSceneName_; }

	/// <summary>
	/// トランジション中かどうか
	/// </summary>
	bool IsTransitioning() const;

	/// <summary>
	/// 最後に使用されたトランジションの種類を取得
	/// </summary>
	TransitionType GetLastUsedTransitionType() const;

	/// <summary>
	/// 最後に使用されたトランジション名を取得
	/// </summary>
	std::string GetLastUsedTransitionName() const;

	//====================
	// マネージャーのゲッター
	//====================
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	Object3DManager* GetObject3DManager() const { return object3DManager_; }
	DirectXCore* GetDirectXCore() const { return dxCore_; }
	SRVManager* GetSRVManager() const { return srvManager_; }
	InputManager* GetInputManager() const { return input_; }

private:
	SceneManager() = default;
	~SceneManager() = default;
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	void SetupScene(BaseScene* scene);
	void ExecuteSceneChange();

	// 現在のシーン
	std::unique_ptr<BaseScene> currentScene_;
	std::string currentSceneName_;

	// 次のシーン（即時切り替え用）
	std::unique_ptr<BaseScene> nextScene_;

	// 予約されたシーン名（トランジション用）
	std::string pendingSceneName_;

	// シーンファクトリー
	AbstractSceneFactory* sceneFactory_ = nullptr;

	// 各マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
};