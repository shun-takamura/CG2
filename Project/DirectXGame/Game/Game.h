#pragma once

#include "Framework.h"
#include "PostEffect.h"
#include "RenderTexture.h"
#include "Config/KeyConfig.h"

class GPUParticleManager;
class ISceneRunner;

/// <summary>
/// ゲーム（ゲーム固有の処理）
/// SceneManagerとSceneFactoryを使用してシーン管理を行う
/// </summary>
class Game : public Framework {
public:
	/// <summary>
	/// Framework にシーン駆動の実体（SceneManager）を渡す（依存性の逆転）。
	/// </summary>
	ISceneRunner* GetSceneRunner() override;

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

	// 全シーン共通の GPU パーティクル管理（EffectManager もこれを参照する）。
	// シーン側で個別に Initialize する必要はない。
	static GPUParticleManager* GetGPUParticleManager() { return gpuParticleManager_.get(); }

	// キーコンフィグのオプション（精密射撃モードのhold/toggle など）
	KeyConfig::Options& GetKeyConfigOptions() { return keyConfigOptions_; }
	const KeyConfig::Options& GetKeyConfigOptions() const { return keyConfigOptions_; }

private:

	static Game* instance_;

	// ポストエフェクト（RenderTextureも内部で管理）
	static std::unique_ptr<PostEffect> postEffect_;

	// 全シーン共通の GPU パーティクル管理
	static std::unique_ptr<GPUParticleManager> gpuParticleManager_;

	// キーコンフィグのオプション
	KeyConfig::Options keyConfigOptions_;

#ifdef _DEBUG
	// Debug ビルド専用: ImGui ViewportWindow に表示する PostEffect 適用後の最終出力
	std::unique_ptr<RenderTexture> viewportRenderTexture_;
#endif
};
