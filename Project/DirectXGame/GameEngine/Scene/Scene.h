#pragma once
#include <memory>
#include <string>
#include <vector>

// PrimitiveInstance はシーン基底が直接 std::unique_ptr で保持するため、完全型が必要
#include "Primitive/PrimitiveInstance.h"
#include "TimeGroup.h"
#include "ITimeScaleProvider.h"
#include "Vector3.h"

// 前方宣言
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;
class Camera;
class DebugCamera;
class IImGuiEditable;
class CameraPreviewSprite;
class Object3DInstance;
class SpriteInstance;
class AnimatedObject3DInstance;
class AnimatedModelInstance;

/// <summary>
/// シーンの基底（エンジン足場）。
/// ライフサイクル、各マネージャ参照、デバッグカメラ、タイムスケール、
/// 動的オブジェクト管理、エフェクト、非同期ロード、ID パス、シーン共通サービスを提供する。
/// ゲーム固有のロジック（弾・敵・近接・プレハブ・スプライン）は派生 GameScene が担う。
/// エンジン側コード（SceneManager / パーティクル等）はこの基底だけに依存する。
/// </summary>
class Scene : public ITimeScaleProvider {
public:
	/// <summary>
	/// コンストラクタ / 仮想デストラクタ
	/// （PrimitiveInstance, CameraPreviewSprite の incomplete type 問題回避のため cpp で定義）
	/// </summary>
	Scene();
	virtual ~Scene();

	virtual void Initialize() = 0;
	virtual void Finalize() = 0;
	virtual void Update() = 0;
	virtual void Draw() = 0;

	/// <summary>
	/// PostEffect 適用後に最終 RT へ直接重ねるオーバーレイ描画。デフォルトは no-op。
	/// </summary>
	virtual void DrawAfterPostEffect(struct ID3D12GraphicsCommandList* commandList) {}

	/// <summary>
	/// シーン内オブジェクト数・読み込み済みリソース数を P.E.P.P.E.R. ゲージへ報告する。
	/// 毎フレーム SceneManager::Update から呼ぶ（USE_PEPPER 未定義時は中身が空展開）。
	/// </summary>
	void ReportProfileGauges();

	/// <summary>
	/// シーンが使用しているアクティブな Camera を返す（無ければ nullptr）。
	/// </summary>
	virtual Camera* GetCamera() { return nullptr; }

	//====================
	// デバッグカメラ（シーン共通機能）
	//====================
	DebugCamera* GetDebugCamera();
	bool GetUseDebugCamera() const { return useDebugCamera_; }
	void SetUseDebugCamera(bool enabled);
	void UpdateDebugCameraIfActive();
	void DrawSceneCameraGizmo();

	//====================
	// 動的オブジェクト（エディタからのドロップで追加される汎用要素）
	//====================
	virtual void AddDynamicObject(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});
	virtual void RemoveDynamicObject(const std::string& name);

	virtual void AddDynamicSprite(const std::string& texturePath, float clientX, float clientY);
	virtual void RemoveDynamicSprite(const std::string& name);

	virtual void AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});
	virtual void RemoveDynamicAnimated(const std::string& name);

	virtual void AddDynamicPrimitive(int primitiveType, const Vector3& worldPos = {});
	virtual void RemoveDynamicPrimitive(const std::string& name);

	/// <summary>
	/// スプラインを動的追加する。tagInt は EntityTag を int キャストしたもの。
	/// 実体はゲーム側（GameScene）が override する。基底は no-op。
	/// </summary>
	virtual void AddDynamicSpline(int tagInt, const Vector3& worldPos = {}) {}
	virtual void RemoveDynamicSpline(const std::string& name) {}

	/// <summary>
	/// プレハブ名でシーンに動的配置する。実体はゲーム側（GameScene）が override する。基底は no-op。
	/// </summary>
	virtual void InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {}) {}

	/// <summary>
	/// 動的エンティティ（Primitive / Object3D / Animated）を遅延破棄キューへ移送する。
	/// 衝突コールバック等から安全に呼べる。GameScene はゲーム状態（弾・敵）の dangling
	/// 参照を掃除してから基底実装を呼ぶ。
	/// </summary>
	virtual void DestroyDynamicEntity(IImGuiEditable* e);

	/// <summary>
	/// 指定ポインタが現在の動的エンティティ群に生存しているか。
	/// </summary>
	bool IsDynamicEntityAlive(IImGuiEditable* e) const;

	//====================
	// ID Pass（ハイライト対象を idMaskRT に書き込む）
	//====================
	void AddHighlight(IImGuiEditable* e);
	void RemoveHighlight(IImGuiEditable* e);
	void ClearHighlights();
	const std::vector<IImGuiEditable*>& GetHighlights() const { return highlightedEntities_; }
	void RunIdPass(struct ID3D12GraphicsCommandList* commandList);

	/// <summary>動的 Object3D をシャドウパスへ描画する。</summary>
	void DrawShadowCasters();
	/// <summary>動的 AnimatedObject3D のスキニングを Dispatch する。</summary>
	void DispatchDynamicAnimatedSkinning();

	//====================
	// 非同期ロード
	//====================
	void ProcessAsyncLoads();

	//====================
	// シーン保存/読込（JSON）。デフォルトは no-op、受け付けるシーンが override する。
	//====================
	virtual bool SaveSceneToJson(const std::string& filePath);
	virtual bool LoadSceneFromJson(const std::string& filePath);

#ifdef USE_IMGUI
	/// <summary>
	/// 指定 editable がこのシーンの動的オブジェクト（Remove 可能）か。
	/// 基底はエンジン管理コンテナを確認する。GameScene はスプラインも確認する。
	/// </summary>
	virtual bool IsDynamicObject(IImGuiEditable* editable) const;
#endif

	//====================
	// エフェクト（EffectManager / GPUParticle）
	//====================
	// deltaTime は unscaled な実 delta（realDt）を渡すこと。各コンポーネント/グループの TimeGroup で
	// EffectInstance / GPUParticleManager が内部スケールする（既定 World＝従来どおりシーンのスローに従う）。
	void UpdateGlobalEffects(Camera* camera, float deltaTime);
	void DrawGlobalEffects();

	//====================
	// シーン共通サービス（カメラキャプチャ / QRコード）
	//====================
	void UseCameraCapture(bool enabled);
	void UseQRCodeReader(bool enabled);
	void UpdateSceneServices();
	void DrawSceneServices();

	//====================
	// シーンタイムライン
	//====================
	float GetElapsedSeconds() const { return elapsedSeconds_; }
	void SetElapsedSeconds(float seconds) { elapsedSeconds_ = (seconds < 0.0f) ? 0.0f : seconds; }
	void TickElapsedSeconds(float delta) { elapsedSeconds_ += delta; }
	virtual void Seek(float seconds) { SetElapsedSeconds(seconds); }
	virtual float GetCameraProgressT() const { return -1.0f; }

	//====================
	// タイムスケール
	//====================
	float GetScaledDeltaTime() const;
	float GetSceneTimeScale() const { return timeScales_[static_cast<int>(TimeGroup::World)]; }
	void SetSceneTimeScale(float scale) {
		timeScales_[static_cast<int>(TimeGroup::World)] = (scale < 0.0f) ? 0.0f : scale;
	}
	float GetTimeScale(TimeGroup g) const {
		int i = static_cast<int>(g);
		if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return 1.0f;
		return timeScales_[i];
	}
	void SetTimeScale(TimeGroup g, float scale) {
		int i = static_cast<int>(g);
		if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return;
		timeScales_[i] = (scale < 0.0f) ? 0.0f : scale;
	}
	/// <summary>ITimeScaleProvider の実装。EngineTime 経由でエンジン側に dt を供給する。</summary>
	float GetScaledDeltaTime(TimeGroup g) const override;

	//====================
	// セッター（Framework / SceneManager から各マネージャを注入）
	//====================
	void SetSpriteManager(SpriteManager* spriteManager) { spriteManager_ = spriteManager; }
	void SetObject3DManager(Object3DManager* object3DManager) { object3DManager_ = object3DManager; }
	void SetSkyboxManager(SkyboxManager* skyboxManager) { skyboxManager_ = skyboxManager; }
	void SetDirectXCore(DirectXCore* dxCore) { dxCore_ = dxCore; }
	void SetSRVManager(SRVManager* srvManager) { srvManager_ = srvManager; }
	void SetInputManager(InputManager* input) { input_ = input; }
	void SetSkinningComputeManager(SkinningComputeManager* manager) { skinningComputeManager_ = manager; }

protected:
	//====================
	// 動的オブジェクトの Update / Draw（派生シーンの Update / Draw から呼ぶ）
	//====================
	void UpdateDynamicObjects();
	void DrawDynamicObjects();
	void UpdateDynamicAnimated(float deltaTime);
	void DrawDynamicAnimated();
	void DrawDynamicAnimatedSkeletonDebug();
	void UpdateDynamicSprites();
	void DrawDynamicSprites();
	void UpdateDynamicPrimitives();
	void DrawDynamicPrimitives();

	// GPU がまだ使用中のリソースを即破棄するとエラーになるため、Remove 時は一旦ここへ退避し
	// 次フレームの ProcessAsyncLoads で破棄する（型消去で include を増やさない）
	std::vector<std::shared_ptr<void>> deferredDeletes_;

	// 動的プリミティブ（SceneEditorWindow からのドロップで追加される）
	std::vector<std::unique_ptr<PrimitiveInstance>> dynamicPrimitives_;

	// ハイライト対象（ID Pass の書き込み対象）
	std::vector<IImGuiEditable*> highlightedEntities_;

	// 動的 3D オブジェクト / Sprite / Animated
	std::vector<std::unique_ptr<Object3DInstance>>          object3DInstances_;
	std::vector<std::unique_ptr<SpriteInstance>>            dynamicSprites_;
	std::vector<std::unique_ptr<AnimatedModelInstance>>     dynamicAnimatedModels_;
	std::vector<std::unique_ptr<AnimatedObject3DInstance>>  dynamicAnimated_;

	// 各マネージャーへのポインタ（Frameworkから受け取る）
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;

	// グループ別タイムスケール [0]=World, [1]=Player, [2]=UI, [3]=Effect
	float timeScales_[static_cast<int>(TimeGroup::Count)] = { 1.0f, 1.0f, 1.0f, 1.0f };

	// シーン開始からの経過秒
	float elapsedSeconds_ = 0.0f;

	// シーン共通サービス
	std::unique_ptr<CameraPreviewSprite> cameraPreview_;
	bool useCameraCapture_ = false;
	bool useQRCodeReader_ = false;

	// デバッグカメラ
	std::unique_ptr<DebugCamera> debugCamera_;
	bool useDebugCamera_ = false;

	// 切替時にスナップショットを取って復帰時に戻す
	struct SceneCameraSnapshot {
		Vector3 translate;
		Vector3 rotate;
		float fovY = 0.0f;
		float aspectRatio = 0.0f;
		float nearClip = 0.0f;
		float farClip = 0.0f;
	} sceneCameraSnapshot_{};
};
