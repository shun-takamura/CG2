#pragma once

// 前方宣言
class BaseScene;
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
	/// シーンの種類
	/// </summary>
	enum Scene {
		TITLE,
		GAME,
	};

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
	/// シーン切り替え
	/// </summary>
	/// <param name="scene">切り替え先のシーン</param>
	void ChangeScene(Scene scene);

	/// <summary>
	/// 更新
	/// </summary>
	void Update();

	/// <summary>
	/// 描画
	/// </summary>
	void Draw();

	/// <summary>
	/// 現在のシーンを取得
	/// </summary>
	BaseScene* GetCurrentScene() const { return currentScene_; }

private:
	// シングルトン用
	SceneManager() = default;
	~SceneManager() = default;
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	// 現在のシーン
	BaseScene* currentScene_ = nullptr;

	// 各マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
};
