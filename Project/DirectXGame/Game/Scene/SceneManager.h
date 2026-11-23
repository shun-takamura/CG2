#pragma once
#include <memory>
#include <string>
#include "BaseTransition.h"
#include "ISceneRunner.h"

// 前方宣言
class Scene;
class AbstractSceneFactory;
class GameSession;
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;

/// <summary>
/// シーン管理クラス。エンジンの Framework からは ISceneRunner 経由で駆動される。
/// シングルトンにする理由：今のシーンは常に 1 つで、シーンの切り替えの予約をどのシーン・部品からも出せるようにするため。
/// </summary>
class SceneManager : public ISceneRunner {
public:
	/// <summary>
	/// シングルトンインスタンスの取得
	/// </summary>
	/// <returns>唯一のインスタンス</returns>
	static SceneManager* GetInstance();

	/// <summary>
	/// 初期化（各マネージャーのポインタを設定し、トランジションと先読みの仕組みを初期化する）
	/// </summary>
	/// <param name="spriteManager">スプライトの共通描画設定</param>
	/// <param name="object3DManager">3D オブジェクトの共通描画設定</param>
	/// <param name="skyboxManager">Skybox の共通描画設定</param>
	/// <param name="dxCore">DirectX の基盤</param>
	/// <param name="srvManager">SRV の管理</param>
	/// <param name="input">入力の管理</param>
	/// <param name="skinningComputeManager">スキニングのコンピュートの管理</param>
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
	/// 終了処理（先読みのスレッドとトランジションを止め、SRVManager 等より先にシーンを壊す）
	/// </summary>
	void Finalize() override;

	/// <summary>
	/// トランジション・先読み・即時切り替えを進め、今のシーンの更新・当たり判定・共通サービス・非同期ロードを回す
	/// </summary>
	void Update() override;

	/// <summary>
	/// 今のシーンと共通サービスを描き、最前面にトランジションを描く
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
	/// <param name="sceneName">シーン名</param>
	void ChangeScene(const std::string& sceneName);

	/// <summary>
	/// シーン変更（覆う側と開く側を別の種類で指定。開く側に TransitionManager::kRevealByScene で開く演出なし）。
	/// 両方ともシーンが演じる種類なら暗転せずに即時切り替えにする。トランジション中は何もしない
	/// </summary>
	/// <param name="sceneName">シーン名</param>
	/// <param name="coverType">覆う側のトランジション</param>
	/// <param name="revealType">開く側のトランジション</param>
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
	/// 次のシーンのアセットを裏で読み始める（ScenePreloader）。終わるまで今のシーンは動いたまま。
	/// 読み終わる前に ChangeScene しても、シーンを読む前に残りを済ませるので壊れない（その分だけ止まる）
	/// </summary>
	/// <param name="sceneName">先読みするシーン名（マニフェスト名）</param>
	void PreloadScene(const std::string& sceneName);
	/// <summary>PreloadScene の先読みの途中か</summary>
	/// <returns>先読みの途中なら true</returns>
	bool IsPreloading() const;

	/// <summary>
	/// シーンファクトリーのセッター
	/// </summary>
	/// <param name="sceneFactory">シーンを作る工場（参照のみ）</param>
	void SetSceneFactory(AbstractSceneFactory* sceneFactory) { sceneFactory_ = sceneFactory; }

	/// <summary>
	/// シーンをまたいで残るプレイの状態を設定する。作ったシーン（GameScene）へ Initialize の前に渡す
	/// </summary>
	/// <param name="gameSession">Game が持つプレイの状態（参照のみ）</param>
	void SetGameSession(GameSession* gameSession) { gameSession_ = gameSession; }

	/// <summary>
	/// 現在のシーンを取得
	/// </summary>
	/// <returns>現在のシーン（無ければ nullptr）</returns>
	Scene* GetCurrentScene() const { return currentScene_.get(); }

	/// <summary>
	/// 現在のシーン名を取得
	/// </summary>
	/// <returns>現在のシーン名（即時切り替えでは ChangeSceneImmediate の時点で次のシーン名になる）</returns>
	const std::string& GetCurrentSceneName() const { return currentSceneName_; }

	/// <summary>
	/// トランジション中かどうか
	/// </summary>
	/// <returns>トランジション中なら true</returns>
	bool IsTransitioning() const;

	/// <summary>
	/// トランジションの今の段階（覆う / 隠したまま / 開く / 何もしていない）
	/// </summary>
	/// <returns>今の段階</returns>
	TransitionState GetTransitionPhase() const;

	/// <summary>
	/// 画面が覆われていく途中か、覆われているか。シーンはこの間だけ止まればよい（開いている間は見えている）
	/// </summary>
	/// <returns>覆う途中か覆われていれば true</returns>
	bool IsTransitionCovering() const;

	/// <summary>
	/// 最後に使用されたトランジションの種類を取得
	/// </summary>
	/// <returns>トランジションの種類</returns>
	TransitionType GetLastUsedTransitionType() const;

	/// <summary>
	/// 最後に使用されたトランジション名を取得
	/// </summary>
	/// <returns>トランジション名</returns>
	std::string GetLastUsedTransitionName() const;

	/// <summary>
	/// フレームの Present（＋GPU完了待ち）直後に呼ぶ。シーン切り替え後の最初のフレームならロード計測を出力する
	/// </summary>
	void OnFramePresented();

	//====================
	// マネージャーのゲッター
	//====================
	/// <summary>スプライトの共通描画設定を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	/// <summary>3D オブジェクトの共通描画設定を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	Object3DManager* GetObject3DManager() const { return object3DManager_; }
	/// <summary>Skybox の共通描画設定を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	SkyboxManager* GetSkyboxManager() const { return skyboxManager_; }
	/// <summary>DirectX の基盤を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	DirectXCore* GetDirectXCore() const { return dxCore_; }
	/// <summary>SRV の管理を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	SRVManager* GetSRVManager() const { return srvManager_; }
	/// <summary>入力の管理を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	InputManager* GetInputManager() const { return input_; }
	/// <summary>スキニングのコンピュートの管理を取得</summary>
	/// <returns>Initialize で受け取ったポインタ</returns>
	SkinningComputeManager* GetSkinningComputeManager() const { return skinningComputeManager_; }

private:
	SceneManager() = default;
	~SceneManager() = default;
	SceneManager(const SceneManager&) = delete;
	SceneManager& operator=(const SceneManager&) = delete;

	/// <summary>シーンに各マネージャーのポインタを渡し、エンジンの時間スケールの供給元にする</summary>
	/// <param name="scene">設定するシーン（nullptr なら何もしない）</param>
	void SetupScene(Scene* scene);
	/// <summary>トランジションが覆い切った所で呼ばれ、今のシーンを終えて予約されたシーンを作って読む</summary>
	void ExecuteSceneChange();
	/// <summary>currentScene_ の Initialize（先読みの残り・DStorage バッチ・計測・読み込みの内訳のログ込み）</summary>
	void LoadCurrentScene();

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
	/// <summary>シーン切り替えの計測の区切りに今の時刻を記録する（Request で計測を始め直す）</summary>
	/// <param name="mark">区切り</param>
	void MarkSceneChange(SceneChangeTiming::Mark mark);
	/// <summary>シーン切り替えの区間ごとの時間を出力窓と profile.log へ書く</summary>
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

	// シーンをまたいで残るプレイの状態（Game が持つ。参照のみ）
	GameSession* gameSession_ = nullptr;

	// 各マネージャーへのポインタ
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;
};