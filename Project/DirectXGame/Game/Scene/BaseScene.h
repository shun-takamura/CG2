#pragma once
#include <memory>
#include <string>
#include <vector>

// PrimitiveInstance はシーン基底が直接 std::unique_ptr で保持するため、完全型が必要
#include "Primitive/PrimitiveInstance.h"

// 前方宣言
class SpriteManager;
class Object3DManager;
class SkyboxManager;
class DirectXCore;
class SRVManager;
class InputManager;
class SkinningComputeManager;
class Camera;
class IImGuiEditable;
class CameraPreviewSprite;

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
	/// シーン単位のタイムスケール（0で停止、1で等速、0.5でスローなど）
	/// </summary>
	float GetSceneTimeScale() const { return sceneTimeScale_; }
	void SetSceneTimeScale(float scale) { sceneTimeScale_ = (scale < 0.0f) ? 0.0f : scale; }

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
protected:
	/// <summary>
	/// 動的プリミティブの更新（派生シーンの Update から呼ぶ）
	/// </summary>
	void UpdateDynamicPrimitives();

	/// <summary>
	/// 動的プリミティブの描画（派生シーンの Draw から呼ぶ）
	/// </summary>
	void DrawDynamicPrimitives();

	// GPU がまだ使用中のリソースを即座に破棄するとコマンドリスト submit 時にエラーとなるため、
	// Remove 時は一旦ここに退避し、次フレームの ProcessAsyncLoads で破棄する
	// 型消去で Object3DInstance.h の include を BaseScene.h に持ち込まない
	std::vector<std::shared_ptr<void>> deferredDeletes_;

	// 動的プリミティブ（SceneEditorWindow からのドロップで追加される）
	std::vector<std::unique_ptr<PrimitiveInstance>> dynamicPrimitives_;

	// 各マネージャーへのポインタ（Frameworkから受け取る）
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;

	// シーンローカルのタイムスケール（DirectXCoreのグローバルとは別に乗算される）
	float sceneTimeScale_ = 1.0f;

	// シーン開始からの経過秒（Debug用シークバー / 仕掛け発火タイミングの基準など）
	float elapsedSeconds_ = 0.0f;

	// シーン共通サービス
	std::unique_ptr<CameraPreviewSprite> cameraPreview_;
	bool useCameraCapture_ = false;
	bool useQRCodeReader_ = false;
};