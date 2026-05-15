#pragma once

#include "BaseScene.h"
#include <memory>
#include <vector>
#include <string>

// 前方宣言
class DebugCamera;
class SpriteInstance;
class Object3DInstance;
class Camera;
class AnimatedModelInstance;
class AnimatedObject3DInstance;
class IImGuiEditable;

class CharacterSelect : public BaseScene
{
public:
	/// <summary>
	/// コンストラクタ
	/// </summary>
	CharacterSelect();

	/// <summary>
	/// デストラクタ
	/// </summary>
	~CharacterSelect() override;

	/// <summary>
	/// 初期化
	/// </summary>
	void Initialize() override;

	/// <summary>
	/// 終了処理
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// 更新
	/// </summary>
	void Update() override;

	/// <summary>
	/// 描画
	/// </summary>
	void Draw() override;

	Camera* GetCamera() override { return camera_.get(); }

	void AddDynamicObject(const std::string& dirPath, const std::string& filename) override;
	void RemoveDynamicObject(const std::string& name) override;
	void AddDynamicSprite(const std::string& texturePath, float clientX, float clientY) override;
	void RemoveDynamicSprite(const std::string& name) override;
	void AddDynamicAnimated(const std::string& dirPath, const std::string& filename) override;
	void RemoveDynamicAnimated(const std::string& name) override;
#ifdef USE_IMGUI
	bool IsDynamicObject(IImGuiEditable* editable) const override;
#endif

private:

	// シーン遷移の待機用
	bool isTransitioning_ = false;
	float transitionTimer_ = 0.0f;
	static constexpr float kTransitionDelay = 1.5f; // パーティクル表示時間（秒）

	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_;

	// スプライト関連
	std::unique_ptr<SpriteInstance> cameraSprite_;
	std::unique_ptr<SpriteInstance> sprite_;
	std::unique_ptr<SpriteInstance> playerQRNormal_;
	std::unique_ptr<SpriteInstance> playerQRCharge_;

	// 3Dオブジェクト
	std::vector<std::unique_ptr<Object3DInstance>> object3DInstances_;

	// SceneEditorWindow からドロップで追加された動的エンティティ
	std::vector<std::unique_ptr<SpriteInstance>> dynamicSprites_;
	std::vector<std::unique_ptr<AnimatedModelInstance>> dynamicAnimatedModels_;
	std::vector<std::unique_ptr<AnimatedObject3DInstance>> dynamicAnimated_;

	// カメラ
	std::unique_ptr<Camera> camera_;

	// パーティクル用タイマー
	float emitTimer_ = 0.0f;
};

