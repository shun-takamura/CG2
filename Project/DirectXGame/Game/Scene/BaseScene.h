#pragma once

// 前方宣言
class SpriteManager;
class Object3DManager;
class DirectXCore;
class SRVManager;
class InputManager;

/// <summary>
/// シーンの基底クラス
/// </summary>
class BaseScene {
public:
	/// <summary>
	/// 仮想デストラクタ
	/// </summary>
	virtual ~BaseScene() = default;

	/// <summary>
	/// 初期化
	/// </summary>
	virtual void Initialize() = 0;

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

	//====================
	// セッター
	//====================

	void SetSpriteManager(SpriteManager* spriteManager) { spriteManager_ = spriteManager; }
	void SetObject3DManager(Object3DManager* object3DManager) { object3DManager_ = object3DManager; }
	void SetDirectXCore(DirectXCore* dxCore) { dxCore_ = dxCore; }
	void SetSRVManager(SRVManager* srvManager) { srvManager_ = srvManager; }
	void SetInputManager(InputManager* input) { input_ = input; }

protected:
	// 各マネージャーへのポインタ（Frameworkから受け取る）
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
};
