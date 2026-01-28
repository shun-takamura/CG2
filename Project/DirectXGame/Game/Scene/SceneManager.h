#pragma once
#include <memory>
#include <string>

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
	/// 次シーン予約
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	void ChangeScene(const std::string& sceneName);

	/// <summary>
	/// シーンファクトリーのセッター
	/// </summary>
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) { sceneFactory_ = sceneFactory; }

	/// <summary>
	/// 現在のシーンを取得
	/// </summary>
	BaseScene* GetCurrentScene() const { return currentScene_.get(); }

	//====================
	// マネージャーのゲッター（シーン初期化時に使用）
	//====================
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	Object3DManager* GetObject3DManager() const { return object3DManager_; }
	DirectXCore* GetDirectXCore() const { return dxCore_; }
	SRVManager* GetSRVManager() const { return srvManager_; }
	InputManager* GetInputManager() const { return input_; }

private:
	// シングルトン用
	SceneManager() = default;
	~SceneManager() = default;
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	/// <summary>
	/// シーンに各マネージャーをセットする
	/// </summary>
	void SetupScene(BaseScene* scene);

	// 現在のシーン
	std::unique_ptr<BaseScene> currentScene_;

	// 次のシーン
	std::unique_ptr<BaseScene> nextScene_;

	// シーンファクトリー（借りてくる）
	AbstractSceneFactory* sceneFactory_ = nullptr;

	// 各マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
};