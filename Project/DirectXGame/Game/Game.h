#pragma once

#include "Framework.h"
#include "PostEffect.h"

/// <summary>
/// ゲーム（ゲーム固有の処理）
/// SceneManagerとSceneFactoryを使用してシーン管理を行う
/// </summary>
class Game : public Framework {
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	Game();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~Game();

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 毎フレーム更新
	/// </summary>
	void Update() override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw() override;

	static Game* GetInstance() { return instance_; }
	static PostEffect* GetPostEffect() { return postEffect_.get(); }

private:

	static Game* instance_;

	// ポストエフェクト（RenderTextureも内部で管理）
	static std::unique_ptr<PostEffect> postEffect_;
};
