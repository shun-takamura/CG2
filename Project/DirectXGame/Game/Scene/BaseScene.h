#pragma once
#include <memory>
#include <string>
#include <vector>

// PrimitiveInstance はシーン基底が直接 std::unique_ptr で保持するため、完全型が必要
#include "Primitive/PrimitiveInstance.h"
#include "TimeGroup.h"
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
class SplineCurveActor;

/// <summary>
/// シーンの基底クラス
/// </summary>
class BaseScene {
public:
	/// <summary>
	/// コンストラクタ / 仮想デストラクタ
	/// （PrimitiveInstance, CameraPreviewSprite の incomplete type 問題回避のため cpp で定義）
	/// </summary>
	BaseScene();
	virtual ~BaseScene();

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

	/// <summary>
	/// シーンが使用しているアクティブなCameraを返す（無ければnullptr）
	/// SceneManager が ImGuiManager に通知するために使う
	/// </summary>
	virtual Camera* GetCamera() { return nullptr; }

	//====================
	// デバッグカメラ（シーン共通機能）
	//====================
	/// <summary>
	/// シーン共通の Orbit/Pan/Zoom デバッグカメラ。
	/// 有効化すると Scene 描画は本カメラの視点で行われ、元のカメラ位置に視錐台ギズモが描かれる。
	/// </summary>
	DebugCamera* GetDebugCamera();
	bool GetUseDebugCamera() const { return useDebugCamera_; }
	void SetUseDebugCamera(bool enabled);

	/// <summary>
	/// 有効ならマウス入力をデバッグカメラに反映し、シーンカメラ行列を上書きする。
	/// 各派生シーンの Update から、シーンカメラ Update の直後に呼ぶ。
	/// </summary>
	void UpdateDebugCameraIfActive();

	/// <summary>
	/// 有効ならシーンカメラ位置に視錐台ギズモを描画する（LineRenderer::Pass::Main を使用）。
	/// 各派生シーンの Draw から、Object3D 描画後・LineRenderer::Draw の前に呼ぶ。
	/// </summary>
	void DrawSceneCameraGizmo();

	/// <summary>
	/// シーンに動的にオブジェクトを追加（SceneEditorWindow から呼ばれる）
	/// worldPos: ビューポートでドロップした位置（Y=0平面で算出）。デフォルトは原点。
	/// </summary>
	virtual void AddDynamicObject(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});

	/// <summary>
	/// シーンから動的オブジェクトを削除
	/// </summary>
	virtual void RemoveDynamicObject(const std::string& name);

	/// <summary>
	/// シーンに動的にスプライトを追加（Viewport ドロップ位置のクライアント座標を渡す）
	/// </summary>
	virtual void AddDynamicSprite(const std::string& texturePath, float clientX, float clientY);

	/// <summary>
	/// シーンから動的スプライトを削除
	/// </summary>
	virtual void RemoveDynamicSprite(const std::string& name);

	/// <summary>
	/// シーンに動的にアニメーションモデルを追加（枠組み。実装は派生で）
	/// </summary>
	virtual void AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});

	/// <summary>
	/// シーンから動的アニメーションモデルを削除
	/// </summary>
	virtual void RemoveDynamicAnimated(const std::string& name);

	/// <summary>
	/// シーンに動的にプリミティブを追加（PrimitiveInstance::PrimitiveType を int で受け取る）
	/// </summary>
	virtual void AddDynamicPrimitive(int primitiveType, const Vector3& worldPos = {});

	/// <summary>
	/// シーンから動的プリミティブを削除
	/// </summary>
	virtual void RemoveDynamicPrimitive(const std::string& name);

	/// <summary>
	/// シーンに動的にスプラインを追加。tagInt は EntityTag を int キャストしたもの
	/// （header の循環依存を避けるため int で受ける）。
	/// </summary>
	virtual void AddDynamicSpline(int tagInt, const Vector3& worldPos = {});

	/// <summary>
	/// 名前指定でスプラインを削除
	/// </summary>
	virtual void RemoveDynamicSpline(const std::string& name);

	/// <summary>
	/// プリファブ名でシーンに動的配置（PrefabManager から PrefabDef を引いて生成）。
	/// デフォルトは no-op。受け付けるシーンが override する。
	/// </summary>
	virtual void InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {});

	/// <summary>
	/// プレイヤー弾を1発スポーン。Primitive プレハブから生成し、velocity と寿命を関連付ける。
	/// 進行と寿命処理は UpdateBullets() が毎フレーム行う。
	/// colliderGrowthPerMeter > 0 のとき、弾が原点から離れるほど collider 半径を線形に拡大する。
	/// homingTarget != nullptr かつ homingStrength > 0 のとき、target の動的位置へ毎フレ補正する
	/// （軽ホーミング。ロックオンや必殺技で同じ仕組みを共有する）。
	/// </summary>
	virtual void SpawnPlayerBullet(const Vector3& pos, const Vector3& direction,
		float speed = 80.0f, float lifetime = 2.0f,
		float colliderGrowthPerMeter = 0.0f,
		IImGuiEditable* homingTarget = nullptr,
		float homingStrength = 0.0f,
		float maxTravelDistance = 0.0f, // 0 で無制限
		const std::string& prefabName = "TemporaryPlayerBullet");

	/// <summary>
	/// 動的エンティティ（Object3D / Animated / Primitive）を遅延破棄キューへ移送する。
	/// 衝突コールバック等から安全に呼べる（次フレームの ProcessAsyncLoads で実体破棄）。
	/// 該当が見つからなければ何もしない。
	/// </summary>
	void DestroyDynamicEntity(IImGuiEditable* e);

	/// <summary>
	/// 指定プレハブから敵を1体スポーンし、指定スプラインに沿って speed[t/sec] で進ませる。
	/// 進行と寿命処理は UpdateMovingEnemies() が毎フレーム行う。
	/// 戻り値はスポーン直後のエンティティポインタ（失敗時 nullptr）。
	/// </summary>
	virtual IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName,
		SplineCurveActor* spline, float speed = 0.1f, bool removeAtEnd = true);

	/// <summary>
	/// シーン内の SplineCurveActor を名前で取得（無ければ nullptr）
	/// </summary>
	SplineCurveActor* FindDynamicSplineByName(const std::string& name);

#ifdef USE_IMGUI
	/// <summary>
	/// 指定された editable がこのシーンの動的オブジェクト（Remove 可能）か
	/// HierarchyWindow から × ボタンを出すかどうかの判定に使う
	/// </summary>
	virtual bool IsDynamicObject(IImGuiEditable* editable) const;
#endif

	/// <summary>
	/// 非同期ロードキューの GPU フェーズを進める（毎フレーム SceneManager から呼ばれる）
	/// </summary>
	void ProcessAsyncLoads();

	//====================
	// シーン保存/読込（JSON）
	//====================

	/// <summary>
	/// 現在の動的オブジェクト配置を JSON ファイルに保存。デフォルトは no-op。
	/// 受け付けるシーンが override する。
	/// </summary>
	virtual bool SaveSceneToJson(const std::string& filePath);

	/// <summary>
	/// JSON ファイルから動的オブジェクト配置を復元。デフォルトは no-op。
	/// 既存の動的オブジェクトはクリアされる前提。
	/// </summary>
	virtual bool LoadSceneFromJson(const std::string& filePath);

	//====================
	// シーン共通サービス（カメラキャプチャ / QRコード）
	//====================

	/// <summary>
	/// カメラ映像プレビューを使うかどうか。
	/// シーンの Update 内で UseCameraCapture(true) を呼べば自動でスプライトの
	/// 更新・描画が走る。カメラ自体の開閉は ImGui 等から行う前提。
	/// </summary>
	void UseCameraCapture(bool enabled);

	/// <summary>
	/// QRコード読み取りを使うかどうか。
	/// 有効中はカメラのフレームを毎フレーム QRCodeReader にデコードさせる。
	/// false にしたタイミングで検出状態をリセットする。
	/// </summary>
	void UseQRCodeReader(bool enabled);

	/// <summary>
	/// シーン共通サービスの更新（SceneManager が Update 後に呼ぶ）
	/// </summary>
	void UpdateSceneServices();

	/// <summary>
	/// シーン共通サービスの描画（SceneManager が Draw 後に呼ぶ）
	/// </summary>
	void DrawSceneServices();

	//====================
	// シーンタイムライン（Debug用シークバー）
	//====================

	/// <summary>
	/// シーン開始からの経過秒（SceneManager が毎フレーム加算）
	/// </summary>
	float GetElapsedSeconds() const { return elapsedSeconds_; }

	/// <summary>
	/// 経過秒を直接書き換える
	/// </summary>
	void SetElapsedSeconds(float seconds) { elapsedSeconds_ = (seconds < 0.0f) ? 0.0f : seconds; }

	/// <summary>
	/// 経過秒に delta を加算する（SceneManager から呼ばれる）
	/// </summary>
	void TickElapsedSeconds(float delta) { elapsedSeconds_ += delta; }

	/// <summary>
	/// 指定時刻にシーン状態を移動する（派生で override 推奨）。
	/// デフォルトは経過秒の書き換えのみ。
	/// </summary>
	virtual void Seek(float seconds) { SetElapsedSeconds(seconds); }

	//====================
	// タイムスケール
	//====================

	/// <summary>
	/// グローバル × シーンローカルを合算したデルタタイムを返す。
	/// シーンの Update から各オブジェクトに渡すのはこれ。
	/// </summary>
	float GetScaledDeltaTime() const;

	/// <summary>
	/// シーン単位のタイムスケール（World グループの倍率を扱う互換API）
	/// </summary>
	float GetSceneTimeScale() const { return timeScales_[static_cast<int>(TimeGroup::World)]; }
	void SetSceneTimeScale(float scale) {
		timeScales_[static_cast<int>(TimeGroup::World)] = (scale < 0.0f) ? 0.0f : scale;
	}

	//====================
	// グループ別タイムスケール（World/Player/UI の独立制御）
	//====================
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

	/// <summary>
	/// グループに紐づくスケール済みデルタタイムを返す（dxCore のグローバル × group倍率）
	/// </summary>
	float GetScaledDeltaTime(TimeGroup g) const;

	//====================
	// セッター
	//====================

	void SetSpriteManager(SpriteManager* spriteManager) { spriteManager_ = spriteManager; }
	void SetObject3DManager(Object3DManager* object3DManager) { object3DManager_ = object3DManager; }
	void SetSkyboxManager(SkyboxManager* skyboxManager) { skyboxManager_ = skyboxManager; }
	void SetDirectXCore(DirectXCore* dxCore) { dxCore_ = dxCore; }
	void SetSRVManager(SRVManager* srvManager) { srvManager_ = srvManager; }
	void SetInputManager(InputManager* input) { input_ = input; }
	void SetSkinningComputeManager(SkinningComputeManager* manager) { skinningComputeManager_ = manager; }

	//====================
	// 全シーン共通のエフェクト系（Game が所有）— ヘルパで Update/Draw を呼ぶだけ
	//====================
	/// <summary>
	/// EffectManager にこのフレームのカメラをセットし、EffectManager と GPUParticle を Update する。
	/// シーンの Update から自分のカメラと scaled dt を渡して呼ぶ。
	/// </summary>
	void UpdateGlobalEffects(Camera* camera, float deltaTime);

	/// <summary>
	/// GPUParticle と EffectManager を Draw する。シーン側で描画順の好きな位置に挿入する。
	/// </summary>
	void DrawGlobalEffects();

	//====================
	// ID Pass（ハイライト対象を idMaskRT に書き込む）
	//====================

	/// <summary>
	/// ハイライト対象を追加。重複は無視。
	/// </summary>
	void AddHighlight(IImGuiEditable* e);
	void RemoveHighlight(IImGuiEditable* e);
	void ClearHighlights();
	const std::vector<IImGuiEditable*>& GetHighlights() const { return highlightedEntities_; }

	/// <summary>
	/// Game::Draw から呼ばれる：ハイライト対象だけを idMaskRT に書き込む。
	/// </summary>
	void RunIdPass(class ID3D12GraphicsCommandList* commandList);

protected:
	/// <summary>
	/// 動的プリミティブの更新（派生シーンの Update から呼ぶ）
	/// </summary>
	void UpdateDynamicPrimitives();

	/// <summary>
	/// 動的プリミティブの描画（派生シーンの Draw から呼ぶ）
	/// </summary>
	void DrawDynamicPrimitives();

	/// <summary>
	/// 動的 Object3D の Update / Draw（dirty な Camera 追従も内部で）。派生 Update/Draw から呼ぶ。
	/// </summary>
	void UpdateDynamicObjects();
	void DrawDynamicObjects();

	/// <summary>
	/// 動的 AnimatedObject3D の Update（dt 必須） / DispatchSkinning / Draw / SkeletonDebug
	/// </summary>
	void UpdateDynamicAnimated(float deltaTime);
	void DispatchDynamicAnimatedSkinning();
	void DrawDynamicAnimated();
	void DrawDynamicAnimatedSkeletonDebug();

	/// <summary>動的スプライトの Update / Draw</summary>
	void UpdateDynamicSprites();
	void DrawDynamicSprites();

	/// <summary>動的スプラインの DebugDraw キュー積み</summary>
	void DrawDynamicSplinesDebug();

	/// <summary>
	/// 弾の進行（velocity*dt 移動）と寿命減算、死亡弾の削除。派生 Update から呼ぶ。
	/// </summary>
	void UpdateBullets(float deltaTime);

	struct BulletRuntime {
		PrimitiveInstance* primitive = nullptr; // dynamicPrimitives_ 内の生ポインタ
		Vector3 velocity{ 0.0f, 0.0f, 0.0f };
		float speed = 0.0f;                // 弾速（ホーミング時に方向だけ変えるため保持）
		float remainingLifetime = 0.0f;
		// 進行距離に応じた collider 拡大用
		Vector3 originPos{ 0.0f, 0.0f, 0.0f };
		float baseColliderRadius = 0.0f;
		float colliderGrowthPerMeter = 0.0f;
		// ホーミング（他システムから再利用できるよう BulletRuntime にメンバとして持つ）
		IImGuiEditable* homingTarget = nullptr;
		float homingStrength = 0.0f;       // [/sec] 大きいほど早く target 方向に向く
		// 最大進行距離（origin からこれを超えたら強制消滅）。0 で無制限。
		float maxTravelDistance = 0.0f;
	};
	std::vector<BulletRuntime> bullets_;

	/// <summary>
	/// スプライン追従敵の進行（t += speed*dt）と終端処理。派生 Update から呼ぶ。
	/// </summary>
	void UpdateMovingEnemies(float deltaTime);

	struct MovingEnemy {
		IImGuiEditable* entity = nullptr; // Primitive / Object3D / Animated いずれか
		SplineCurveActor* spline = nullptr;
		float t = 0.0f;
		float speed = 0.1f;
		bool  removeAtEnd = true;
	};
	std::vector<MovingEnemy> movingEnemies_;

	// GPU がまだ使用中のリソースを即座に破棄するとコマンドリスト submit 時にエラーとなるため、
	// Remove 時は一旦ここに退避し、次フレームの ProcessAsyncLoads で破棄する
	// 型消去で Object3DInstance.h の include を BaseScene.h に持ち込まない
	std::vector<std::shared_ptr<void>> deferredDeletes_;

	// 動的プリミティブ（SceneEditorWindow からのドロップで追加される）
	std::vector<std::unique_ptr<PrimitiveInstance>> dynamicPrimitives_;

	// ハイライト対象（ID Pass の書き込み対象）
	std::vector<IImGuiEditable*> highlightedEntities_;

	// 動的 3D オブジェクト / Sprite / Animated / Spline（プレハブ含む）
	std::vector<std::unique_ptr<Object3DInstance>>          object3DInstances_;
	std::vector<std::unique_ptr<SpriteInstance>>            dynamicSprites_;
	std::vector<std::unique_ptr<AnimatedModelInstance>>     dynamicAnimatedModels_;
	std::vector<std::unique_ptr<AnimatedObject3DInstance>>  dynamicAnimated_;
	std::vector<std::unique_ptr<SplineCurveActor>>          dynamicSplines_;

	// 各マネージャーへのポインタ（Frameworkから受け取る）
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;

	// グループ別タイムスケール（DirectXCoreのグローバルとは別に乗算される）
	// [0]=World, [1]=Player, [2]=UI
	float timeScales_[static_cast<int>(TimeGroup::Count)] = { 1.0f, 1.0f, 1.0f };

	// シーン開始からの経過秒（Debug用シークバー / 仕掛け発火タイミングの基準など）
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