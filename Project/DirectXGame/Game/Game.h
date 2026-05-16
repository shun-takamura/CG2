#pragma once

#include "Framework.h"
#include "PostEffect.h"
#include "RenderTexture.h"
#include "Config/KeyConfig.h"

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

	// キーコンフィグのオプション（精密射撃モードのhold/toggle など）
	KeyConfig::Options& GetKeyConfigOptions() { return keyConfigOptions_; }
	const KeyConfig::Options& GetKeyConfigOptions() const { return keyConfigOptions_; }

private:

	static Game* instance_;

	// ポストエフェクト（RenderTextureも内部で管理）
	static std::unique_ptr<PostEffect> postEffect_;

	// キーコンフィグのオプション
	KeyConfig::Options keyConfigOptions_;

#ifdef _DEBUG
	// Debug ビルド専用: ImGui ViewportWindow に表示する PostEffect 適用後の最終出力
	std::unique_ptr<RenderTexture> viewportRenderTexture_;
#endif
};
