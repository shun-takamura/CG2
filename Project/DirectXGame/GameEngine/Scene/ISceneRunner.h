#pragma once

// 前方宣言（すべてエンジン側のマネージャ）
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;

/// <summary>
/// エンジンの Framework がシーン駆動を委譲する先のインターフェース。
/// 実体（ゲームの SceneManager）が実装し、Game が Framework::GetSceneRunner() で返す。
/// これにより Framework はゲームの SceneManager を名指しせずにシーンを回せる（依存性の逆転）。
/// </summary>
class ISceneRunner {
public:
	virtual ~ISceneRunner() = default;

	/// <summary>各マネージャを受け取り初期化する。</summary>
	virtual void Initialize(
		SpriteManager* spriteManager,
		Object3DManager* object3DManager,
		SkyboxManager* skyboxManager,
		DirectXCore* dxCore,
		SRVManager* srvManager,
		InputManager* input,
		SkinningComputeManager* skinningComputeManager) = 0;

	/// <summary>毎フレーム更新（Framework の処理列の所定位置から呼ばれる）。</summary>
	virtual void Update() = 0;

	/// <summary>終了処理。</summary>
	virtual void Finalize() = 0;
};
