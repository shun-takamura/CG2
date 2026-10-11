#pragma once
#include <memory>
#include <string>
#include "BaseTransition.h"
#include "ISceneRunner.h"

// 前方宣言
class Scene;
class AbstractSceneFactory;
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;

/// <summary>
/// シーン管理クラス。エンジンの Framework からは ISceneRunner 経由で駆動される。
/// </summary>
class SceneManager : public ISceneRunner {
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
		SkyboxManager* skyboxManager,
		DirectXCore* dxCore,
		SRVManager* srvManager,
		InputManager* input,
		SkinningComputeManager* skinningComputeManager
	) override;

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
	void Draw();

	/// <summary>
	/// シーン変更（トランジション指定）
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <param name="transitionType">トランジションの種類</param>
	void ChangeScene(const std::string& sceneName, TransitionType transitionType);

	/// <summary>
	/// シーン変更（トランジションは組み合わせ表 SceneRoutes から引く。ゲームの遷移は基本これを使う）
	/// </summary>
	void ChangeScene(const std::string& sceneName);

	/// <summary>
	/// シーン変更（覆う側と開く側を別の種類で指定。開く側に TransitionManager::kRevealByScene で開く演出なし）
	/// </summary>
	void ChangeScene(const std::string& sceneName, TransitionType coverType, TransitionType revealType);

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
	Scene* GetCurrentScene() const { return currentScene_.get(); }

	/// <summary>
	/// 現在のシーン名を取得
	/// </summary>
	const std::string& GetCurrentSceneName() const { return currentSceneName_; }

	/// <summary>
	/// トランジション中かどうか
	/// </summary>
	bool IsTransitioning() const;

	/// <summary>
	/// トランジションの今の段階（覆う / 隠したまま / 開く / 何もしていない）
	/// </summary>
	TransitionState GetTransitionPhase() const;

	/// <summary>
	/// 画面が覆われていく途中か、覆われているか。シーンはこの間だけ止まればよい（開いている間は見えている）
	/// </summary>
	bool IsTransitionCovering() const;

	/// <summary>
	/// 最後に使用されたトランジションの種類を取得
	/// </summary>
	TransitionType GetLastUsedTransitionType() const;

	/// <summary>
	/// 最後に使用されたトランジション名を取得
	/// </summary>
	std::string GetLastUsedTransitionName() const;

	/// <summary>
	/// フレームの Present（＋GPU完了待ち）直後に呼ぶ。シーン切り替え後の最初のフレームならロード計測を出力する
	/// </summary>
	void OnFramePresented();

	//====================
	// マネージャーのゲッター
	//====================
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	Object3DManager* GetObject3DManager() const { return object3DManager_; }
	SkyboxManager* GetSkyboxManager() const { return skyboxManager_; }
	DirectXCore* GetDirectXCore() const { return dxCore_; }
	SRVManager* GetSRVManager() const { return srvManager_; }
	InputManager* GetInputManager() const { return input_; }
	SkinningComputeManager* GetSkinningComputeManager() const { return skinningComputeManager_; }

private:
	SceneManager() = default;
	~SceneManager() = default;
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	void SetupScene(Scene* scene);
	void ExecuteSceneChange();

	// シーン切り替え1回分の計測（決定 → 最初のフレームが画面に出るまで）
	struct SceneChangeTiming {
		enum Mark { Request, ChangeBegin, Finalized, Created, Initialized, FirstUpdated, Presented, kMarkCount };
		bool active = false;
		bool waitingFirstUpdate = false;
		std::string from;
		std::string to;
		double wallMs[kMarkCount] = {};
		double cpuMs[kMarkCount] = {};
	};
	void MarkSceneChange(SceneChangeTiming::Mark mark);
	void LogSceneChangeTiming() const;
	SceneChangeTiming changeTiming_;

	// 現在のシーン
	std::unique_ptr<Scene> currentScene_;
	std::string currentSceneName_;

	// 次のシーン（即時切り替え用）
	std::unique_ptr<Scene> nextScene_;

	// 予約されたシーン名（トランジション用）
	std::string pendingSceneName_;

	// シーンファクトリー
	AbstractSceneFactory* sceneFactory_ = nullptr;

	// 各マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;
};