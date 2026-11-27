#pragma once
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

	/// <summary>シーン開始時の初期化（オブジェクト生成・リソース読み込み）。SceneManager がマネージャ注入後に呼ぶ</summary>
	virtual void Initialize() = 0;
	/// <summary>シーン終了時の後片付け。SceneManager が次のシーンへ切り替える前に呼ぶ</summary>
	virtual void Finalize() = 0;
	/// <summary>1 フレーム分の更新</summary>
	virtual void Update() = 0;
	/// <summary>1 フレーム分の描画（PostEffect をかける前のシーン本体）</summary>
	virtual void Draw() = 0;

	/// <summary>
	/// PostEffect 適用後に最終 RT へ直接重ねるオーバーレイ描画。デフォルトは no-op。
	/// </summary>
	/// <param name="commandList">最終 RT が設定済みのコマンドリスト</param>
	virtual void DrawAfterPostEffect(struct ID3D12GraphicsCommandList* commandList) {}

	/// <summary>
	/// シーン内オブジェクト数・読み込み済みリソース数を P.E.P.P.E.R. ゲージへ報告する。
	/// 毎フレーム SceneManager::Update から呼ぶ（USE_PEPPER 未定義時は中身が空展開）。
	/// </summary>
	void ReportProfileGauges();

	/// <summary>
	/// シーンが使用しているアクティブな Camera を返す（無ければ nullptr）。
	/// </summary>
	/// <returns>アクティブなカメラ（無ければ nullptr）</returns>
	virtual Camera* GetCamera() { return nullptr; }

	/// <summary>
	/// ビューポートへのプレハブドロップをシーン側で横取りするためのフック。
	/// relX/relY はビューポート画像内の正規化座標 (0..1)。
	/// true を返すとドロップを消費したとみなし、呼び出し側は通常配置を行わない。
	/// 既定は何もせず false（＝通常の InstantiatePrefab に委ねる）。
	/// Wave Editor 等が override する。
	/// </summary>
	/// <param name="prefabName">ドロップされたプレハブ名</param>
	/// <param name="relX">ビューポート画像内の X（0..1）</param>
	/// <param name="relY">ビューポート画像内の Y（0..1）</param>
	/// <returns>ドロップを消費したら true</returns>
	virtual bool OnViewportPrefabDrop([[maybe_unused]] const std::string& prefabName, [[maybe_unused]] float relX, [[maybe_unused]] float relY) { return false; }

	//====================
	// デバッグカメラ（シーン共通機能）
	//====================
	/// <summary>デバッグカメラを返す（初回呼び出しで生成・初期化する）</summary>
	/// <returns>デバッグカメラ</returns>
	DebugCamera* GetDebugCamera();
	/// <summary>デバッグカメラを使っている最中か</summary>
	/// <returns>使用中なら true</returns>
	bool GetUseDebugCamera() const { return useDebugCamera_; }
	/// <summary>
	/// デバッグカメラの ON/OFF を切り替える。
	/// ON にする時はシーンカメラの状態を控えてポーズを引き継ぎ、OFF にする時は控えた位置・回転へ戻す
	/// </summary>
	/// <param name="enabled">ON にするなら true</param>
	void SetUseDebugCamera(bool enabled);
	/// <summary>
	/// デバッグカメラ使用中だけ、ビューポート上のマウス操作（周回・パン・ズーム）で動かし、
	/// その行列をシーンカメラへ注入する
	/// </summary>
	void UpdateDebugCameraIfActive();
	/// <summary>デバッグカメラ使用中、控えておいたシーンカメラの位置に視錐台の線を描く</summary>
	void DrawSceneCameraGizmo();

	//====================
	// 動的オブジェクト（エディタからのドロップで追加される汎用要素）
	//====================
	/// <summary>モデルを読み込んで動的 Object3D を足す</summary>
	/// <param name="dirPath">モデルのフォルダ</param>
	/// <param name="filename">モデルのファイル名</param>
	/// <param name="worldPos">置く位置</param>
	/// <returns>足した Object3D</returns>
	virtual Object3DInstance* AddDynamicObject(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});
	/// <summary>名前で動的 Object3D を探して遅延破棄する</summary>
	/// <param name="name">名前</param>
	virtual void RemoveDynamicObject(const std::string& name);

	/// <summary>テクスチャを貼った動的スプライトを足す</summary>
	/// <param name="texturePath">テクスチャのパス</param>
	/// <param name="clientX">画面上の X</param>
	/// <param name="clientY">画面上の Y</param>
	/// <returns>足したスプライト</returns>
	virtual SpriteInstance* AddDynamicSprite(const std::string& texturePath, float clientX, float clientY);
	/// <summary>名前で動的スプライトを探して遅延破棄する</summary>
	/// <param name="name">名前</param>
	virtual void RemoveDynamicSprite(const std::string& name);

	/// <summary>スキニングするモデルを読み込んで動的に足す</summary>
	/// <param name="dirPath">モデルのフォルダ</param>
	/// <param name="filename">モデルのファイル名</param>
	/// <param name="worldPos">置く位置</param>
	/// <returns>足したモデル</returns>
	virtual AnimatedObject3DInstance* AddDynamicAnimated(const std::string& dirPath, const std::string& filename,
		const Vector3& worldPos = {});
	/// <summary>名前で動的なアニメーションするモデルを探して遅延破棄する</summary>
	/// <param name="name">名前</param>
	virtual void RemoveDynamicAnimated(const std::string& name);

	/// <summary>動的プリミティブを足す（名前は種類名に連番を付ける）</summary>
	/// <param name="primitiveType">プリミティブの種類（保存用の int からは PrimitiveInstance::PrimitiveTypeFromInt で直す）</param>
	/// <param name="worldPos">置く位置</param>
	/// <returns>足したプリミティブ</returns>
	virtual PrimitiveInstance* AddDynamicPrimitive(PrimitiveInstance::PrimitiveType primitiveType, const Vector3& worldPos = {});
	/// <summary>名前で動的プリミティブを探して遅延破棄する</summary>
	/// <param name="name">名前</param>
	virtual void RemoveDynamicPrimitive(const std::string& name);

	/// <summary>
	/// スプラインを動的追加する。tagInt は EntityTag を int キャストしたもの。
	/// 実体はゲーム側（GameScene）が override する。基底は no-op。
	/// </summary>
	/// <param name="tagInt">EntityTag を int にした物</param>
	/// <param name="worldPos">置く位置</param>
	virtual void AddDynamicSpline(int tagInt, const Vector3& worldPos = {}) {}
	/// <summary>名前でスプラインを探して消す。実体はゲーム側（GameScene）が override する。基底は no-op</summary>
	/// <param name="name">名前</param>
	virtual void RemoveDynamicSpline(const std::string& name) {}

	/// <summary>
	/// プレハブ名でシーンに動的配置する。実体はゲーム側（GameScene）が override する。基底は no-op。
	/// 戻り値は生成したエンティティ（生成できなければ nullptr）。所有権はシーン側が持つ。
	/// </summary>
	/// <param name="prefabName">プレハブ名</param>
	/// <param name="worldPos">置く位置</param>
	/// <returns>生成したエンティティ（生成できなければ nullptr）</returns>
	virtual IImGuiEditable* InstantiatePrefab(const std::string& prefabName, const Vector3& worldPos = {}) { return nullptr; }

	/// <summary>
	/// 動的エンティティ（Primitive / Object3D / Animated）を遅延破棄キューへ移送する。
	/// 衝突コールバック等から安全に呼べる。GameScene はゲーム状態（弾・敵）の dangling
	/// 参照を掃除してから基底実装を呼ぶ。
	/// </summary>
	/// <param name="e">破棄する物（動的エンティティでなければ何もしない）</param>
	virtual void DestroyDynamicEntity(IImGuiEditable* e);

	/// <summary>
	/// 指定ポインタが現在の動的エンティティ群に生存しているか。
	/// </summary>
	/// <param name="e">調べるポインタ</param>
	/// <returns>生存していれば true（nullptr は false）</returns>
	bool IsDynamicEntityAlive(IImGuiEditable* e) const;

	//====================
	// ID Pass（ハイライト対象を idMaskRT に書き込む）
	//====================
	/// <summary>ID パスでハイライトする対象に足す（nullptr・登録済みは無視）</summary>
	/// <param name="e">足す物</param>
	void AddHighlight(IImGuiEditable* e);
	/// <summary>ハイライト対象から外す</summary>
	/// <param name="e">外す物</param>
	void RemoveHighlight(IImGuiEditable* e);
	/// <summary>ハイライト対象をすべて外す</summary>
	void ClearHighlights();
	/// <summary>ハイライト対象の一覧</summary>
	/// <returns>一覧</returns>
	const std::vector<IImGuiEditable*>& GetHighlights() const { return highlightedEntities_; }
	/// <summary>
	/// ハイライト対象（動的 Object3D / Animated / Primitive）と、シーンが直接持つ物を ID パスに描く。
	/// どちらも無ければ何もしない
	/// </summary>
	/// <param name="commandList">ID マスク RT が設定済みのコマンドリスト（今は使っていない）</param>
	void RunIdPass(struct ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// シーンがメンバで直接持つ物（コンテナ外。タイトルの扉など）に ID パスへ描く物があるか。
	/// true の間はハイライトが空でも ID パスが走る
	/// </summary>
	/// <returns>描く物があれば true</returns>
	virtual bool HasExtraIdPassObjects() const { return false; }
	/// <summary>シーンがメンバで直接持つ物（コンテナ外。タイトルの扉など）を ID パスに描く</summary>
	virtual void DrawExtraIdPassObjects() {}

	//====================
	// 発光パス（ブルームの発光 RT に「光らせたい物だけ」を描く。Game の発光パスから呼ぶ）
	//====================
	/// <summary>
	/// シーンが自分で光らせる物（Object3DInstance::DrawBloomPass で描く物）がある間 true。
	/// エフェクト・文字の発光（TextRenderer の発光の層）が無くても発光パスが走る
	/// </summary>
	/// <returns>光らせる物があれば true</returns>
	virtual bool HasBloomSources() const { return false; }
	/// <summary>発光 RT に描く。RTV / DSV / ビューポートは設定済み。PSO とルートシグネチャは描く側が貼る</summary>
	virtual void DrawBloomSources() {}

	/// <summary>
	/// 動的 Object3D をシャドウパスへ描画する。
	/// シーンがメンバで直接持つ物（タイトルのロゴ等）も影を落とす場合は override して基底も呼ぶ。
	/// </summary>
	virtual void DrawShadowCasters();
	/// <summary>動的 AnimatedObject3D のスキニングを Dispatch する。</summary>
	void DispatchDynamicAnimatedSkinning();

	//====================
	// 非同期ロード
	//====================
	/// <summary>
	/// 前フレームで遅延破棄キューに入れた物を破棄し、モデルの GPU アップロードを 1 件進める。
	/// 毎フレーム描画の前に呼ぶ（呼ばれた時点で前フレームの GPU 処理が終わっている前提）
	/// </summary>
	void ProcessAsyncLoads();

	//====================
	// シーン保存/読込（JSON）。デフォルトは no-op、受け付けるシーンが override する。
	//====================
	/// <summary>シーンの内容を JSON ファイルに保存する。基底は何もしない</summary>
	/// <param name="filePath">保存先のパス</param>
	/// <returns>保存できたら true（基底は常に false）</returns>
	virtual bool SaveSceneToJson(const std::string& filePath);
	/// <summary>JSON ファイルからシーンの内容を読み込む。基底は何もしない</summary>
	/// <param name="filePath">読み込むパス</param>
	/// <returns>読み込めたら true（基底は常に false）</returns>
	virtual bool LoadSceneFromJson(const std::string& filePath);

	/// <summary>現在のシーン内容を JSON 文字列にする（差分検出用）。保存非対応シーンは空文字。</summary>
	/// <returns>シーン内容の JSON 文字列（保存非対応なら空文字）</returns>
	virtual std::string SerializeSceneToString() const { return {}; }

#ifdef USE_IMGUI
	/// <summary>
	/// 指定 editable がこのシーンの動的オブジェクト（Remove 可能）か。
	/// 基底はエンジン管理コンテナを確認する。GameScene はスプラインも確認する。
	/// </summary>
	/// <param name="editable">調べる物</param>
	/// <returns>動的オブジェクトなら true</returns>
	virtual bool IsDynamicObject(IImGuiEditable* editable) const;
#endif

	//====================
	// エフェクト（EffectManager / GPUParticle）
	//====================
	/// <summary>
	/// エフェクト（EffectManager と GPU パーティクル）にカメラを渡して更新する。
	/// deltaTime は unscaled な実 delta（realDt）を渡すこと。各コンポーネント/グループの TimeGroup で
	/// EffectInstance / GPUParticleManager が内部スケールする（既定 World＝シーンのスローに従う）
	/// </summary>
	/// <param name="camera">エフェクトのビルボード等に使うカメラ</param>
	/// <param name="deltaTime">スケール前の経過時間 [秒]</param>
	void UpdateGlobalEffects(Camera* camera, float deltaTime);
	/// <summary>GPU パーティクルと EffectManager のエフェクトを描く</summary>
	void DrawGlobalEffects();

	//====================
	// シーン共通サービス（カメラキャプチャ / QRコード）
	//====================
	/// <summary>カメラキャプチャのプレビュー表示を使うか切り替える（初めて ON にした時にプレビューを作る）</summary>
	/// <param name="enabled">使うなら true</param>
	void UseCameraCapture(bool enabled);
	/// <summary>QR コードの読み取りを使うか切り替える（ON→OFF で検出状態をリセットする）</summary>
	/// <param name="enabled">使うなら true</param>
	void UseQRCodeReader(bool enabled);
	/// <summary>使う設定のシーン共通サービスを更新する（プレビュー更新・カメラ画像の QR デコード）</summary>
	void UpdateSceneServices();
	/// <summary>使う設定のカメラキャプチャのプレビューを描く</summary>
	void DrawSceneServices();

	//====================
	// シーンタイムライン
	//====================
	/// <summary>シーン開始からの経過時間</summary>
	/// <returns>経過時間 [秒]</returns>
	float GetElapsedSeconds() const { return elapsedSeconds_; }
	/// <summary>シーン開始からの経過時間を直接設定する（負の値は 0 にする）</summary>
	/// <param name="seconds">経過時間 [秒]</param>
	void SetElapsedSeconds(float seconds) { elapsedSeconds_ = (seconds < 0.0f) ? 0.0f : seconds; }
	/// <summary>経過時間を進める</summary>
	/// <param name="delta">進める時間 [秒]</param>
	void TickElapsedSeconds(float delta) { elapsedSeconds_ += delta; }
	/// <summary>タイムラインの指定時刻へ飛ぶ。基底は経過時間を書き換えるだけ</summary>
	/// <param name="seconds">飛び先の時刻 [秒]</param>
	virtual void Seek(float seconds) { SetElapsedSeconds(seconds); }
	/// <summary>カメラ（レール等）の進み具合</summary>
	/// <returns>進み具合（0..1）。未対応シーンは -1</returns>
	virtual float GetCameraProgressT() const { return -1.0f; }
	/// <summary>シークできる最大の時刻</summary>
	/// <returns>最大の時刻 [秒]。未対応シーンは -1</returns>
	virtual float GetSeekMaxSeconds() const { return -1.0f; } // -1 = 未対応シーン

	//====================
	// タイムスケール
	//====================
	/// <summary>World グループのタイムスケールをかけた経過時間</summary>
	/// <returns>このフレームの経過時間 [秒]（DirectXCore 未設定なら 0）</returns>
	float GetScaledDeltaTime() const;
	/// <summary>World グループ（シーン全体）のタイムスケール</summary>
	/// <returns>倍率</returns>
	float GetSceneTimeScale() const { return timeScales_[static_cast<int>(TimeGroup::World)]; }
	/// <summary>World グループ（シーン全体）のタイムスケールを設定する（負の値は 0 にする）</summary>
	/// <param name="scale">倍率</param>
	void SetSceneTimeScale(float scale) {
		timeScales_[static_cast<int>(TimeGroup::World)] = (scale < 0.0f) ? 0.0f : scale;
	}
	/// <summary>指定グループのタイムスケール</summary>
	/// <param name="g">グループ</param>
	/// <returns>倍率（範囲外のグループは 1）</returns>
	float GetTimeScale(TimeGroup g) const {
		int i = static_cast<int>(g);
		if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return 1.0f;
		return timeScales_[i];
	}
	/// <summary>指定グループのタイムスケールを設定する（負の値は 0、範囲外のグループは無視）</summary>
	/// <param name="g">グループ</param>
	/// <param name="scale">倍率</param>
	void SetTimeScale(TimeGroup g, float scale) {
		int i = static_cast<int>(g);
		if (i < 0 || i >= static_cast<int>(TimeGroup::Count)) return;
		timeScales_[i] = (scale < 0.0f) ? 0.0f : scale;
	}
	/// <summary>ITimeScaleProvider の実装。EngineTime 経由でエンジン側に dt を供給する。</summary>
	/// <param name="g">グループ</param>
	/// <returns>グループの倍率をかけた経過時間 [秒]（範囲外のグループは倍率なし、DirectXCore 未設定なら 0）</returns>
	float GetScaledDeltaTime(TimeGroup g) const override;

	//====================
	// セッター（Framework / SceneManager から各マネージャを注入）
	//====================
	/// <summary>スプライト描画のマネージャを受け取る</summary>
	/// <param name="spriteManager">SpriteManager</param>
	void SetSpriteManager(SpriteManager* spriteManager) { spriteManager_ = spriteManager; }
	/// <summary>3D モデル描画のマネージャを受け取る</summary>
	/// <param name="object3DManager">Object3DManager</param>
	void SetObject3DManager(Object3DManager* object3DManager) { object3DManager_ = object3DManager; }
	/// <summary>Skybox 描画のマネージャを受け取る</summary>
	/// <param name="skyboxManager">SkyboxManager</param>
	void SetSkyboxManager(SkyboxManager* skyboxManager) { skyboxManager_ = skyboxManager; }
	/// <summary>DirectX の基盤を受け取る</summary>
	/// <param name="dxCore">DirectXCore</param>
	void SetDirectXCore(DirectXCore* dxCore) { dxCore_ = dxCore; }
	/// <summary>SRV 確保のマネージャを受け取る</summary>
	/// <param name="srvManager">SRVManager</param>
	void SetSRVManager(SRVManager* srvManager) { srvManager_ = srvManager; }
	/// <summary>入力のマネージャを受け取る</summary>
	/// <param name="input">InputManager</param>
	void SetInputManager(InputManager* input) { input_ = input; }
	/// <summary>スキニングのコンピュートシェーダのマネージャを受け取る</summary>
	/// <param name="manager">SkinningComputeManager</param>
	void SetSkinningComputeManager(SkinningComputeManager* manager) { skinningComputeManager_ = manager; }

protected:
	//====================
	// 各マネージャー（Framework / SceneManager から受け取った物。派生シーンは読むだけ）
	//====================
	/// <summary>GPU リソースの作成・コマンドリスト・経過時間</summary>
	/// <returns>DirectXCore</returns>
	DirectXCore* GetDxCore() const { return dxCore_; }
	/// <summary>スプライトの描画</summary>
	/// <returns>SpriteManager</returns>
	SpriteManager* GetSpriteManager() const { return spriteManager_; }
	/// <summary>3D モデルの描画</summary>
	/// <returns>Object3DManager</returns>
	Object3DManager* GetObject3DManager() const { return object3DManager_; }
	/// <summary>Skybox の描画</summary>
	/// <returns>SkyboxManager</returns>
	SkyboxManager* GetSkyboxManager() const { return skyboxManager_; }
	/// <summary>SRV（テクスチャの記述子）の確保</summary>
	/// <returns>SRVManager</returns>
	SRVManager* GetSRVManager() const { return srvManager_; }
	/// <summary>キーボード・マウス・パッドと入力アクション</summary>
	/// <returns>InputManager</returns>
	InputManager* GetInput() const { return input_; }
	/// <summary>スキニングのコンピュートシェーダ</summary>
	/// <returns>SkinningComputeManager</returns>
	SkinningComputeManager* GetSkinningComputeManager() const { return skinningComputeManager_; }

private:
	// 各マネージャーへのポインタ（Frameworkから受け取る）
	SpriteManager* spriteManager_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;
	SkyboxManager* skyboxManager_ = nullptr;
	DirectXCore* dxCore_ = nullptr;
	SRVManager* srvManager_ = nullptr;
	InputManager* input_ = nullptr;
	SkinningComputeManager* skinningComputeManager_ = nullptr;

protected:
	//====================
	// 動的オブジェクトの Update / Draw（派生シーンの Update / Draw から呼ぶ）
	//====================
	/// <summary>動的 Object3D にシーンのカメラを渡して更新する</summary>
	void UpdateDynamicObjects();
	/// <summary>動的 Object3D を描く</summary>
	void DrawDynamicObjects();
	/// <summary>動的なアニメーションするモデルのアニメーションを進める（World 以外の TimeGroup を持つ物はその時間で）</summary>
	/// <param name="deltaTime">World の物の経過時間 [秒]</param>
	void UpdateDynamicAnimated(float deltaTime);
	/// <summary>動的なアニメーションするモデルを描く（スキニングは DispatchDynamicAnimatedSkinning で済ませておく）</summary>
	void DrawDynamicAnimated();
	/// <summary>動的なアニメーションするモデルの骨をデバッグ表示する（USE_IMGUI の時だけ）</summary>
	void DrawDynamicAnimatedSkeletonDebug();
	/// <summary>動的スプライトを更新する</summary>
	void UpdateDynamicSprites();
	/// <summary>動的スプライトを描く</summary>
	void DrawDynamicSprites();
	/// <summary>動的プリミティブにシーンのカメラを渡して更新する</summary>
	void UpdateDynamicPrimitives();
	/// <summary>動的プリミティブを描く</summary>
	void DrawDynamicPrimitives();

	//====================
	// 動的オブジェクトの一覧（派生シーンは読むだけ。足す・消すは下の関数を通す）
	//====================
	/// <summary>動的プリミティブの一覧（一覧への追加・削除はできない）</summary>
	/// <returns>一覧</returns>
	const std::vector<std::unique_ptr<PrimitiveInstance>>& GetDynamicPrimitives() const { return dynamicPrimitives_; }
	/// <summary>動的 Object3D の一覧（一覧への追加・削除はできない）</summary>
	/// <returns>一覧</returns>
	const std::vector<std::unique_ptr<Object3DInstance>>& GetDynamicObjects() const { return object3DInstances_; }
	/// <summary>動的スプライトの一覧（一覧への追加・削除はできない）</summary>
	/// <returns>一覧</returns>
	const std::vector<std::unique_ptr<SpriteInstance>>& GetDynamicSprites() const { return dynamicSprites_; }
	/// <summary>動的なアニメーションするモデルの一覧（一覧への追加・削除はできない）</summary>
	/// <returns>一覧</returns>
	const std::vector<std::unique_ptr<AnimatedObject3DInstance>>& GetDynamicAnimated() const { return dynamicAnimated_; }

	/// <summary>外で作ったスプライトをシーンの一覧へ移す（以後の Update / Draw / 破棄はシーンが行う）</summary>
	/// <param name="sprite">移すスプライト</param>
	/// <returns>移したスプライト（sprite が空なら nullptr）</returns>
	SpriteInstance* AdoptDynamicSprite(std::unique_ptr<SpriteInstance> sprite);
	/// <summary>外で作った Object3D をシーンの一覧へ移す（以後の Update / Draw / 破棄はシーンが行う）</summary>
	/// <param name="object">移す Object3D</param>
	/// <returns>移した Object3D（object が空なら nullptr）</returns>
	Object3DInstance* AdoptDynamicObject(std::unique_ptr<Object3DInstance> object);

	/// <summary>
	/// 一覧から外して遅延破棄キューへ移す（Primitive / Object3D / Animated のどれか）。
	/// ゲーム側の状態（弾・敵）の掃除はしないので、それが要る時は DestroyDynamicEntity を使う
	/// </summary>
	/// <param name="e">外す物</param>
	/// <returns>見つかって外したら true</returns>
	bool ReleaseDynamicEntity(IImGuiEditable* e);
	/// <summary>Primitive / Object3D / Animated のうち、条件に当てはまる物をすべて遅延破棄する</summary>
	/// <param name="shouldDestroy">消すなら true を返す条件</param>
	void DestroyDynamicEntitiesIf(const std::function<bool(IImGuiEditable* e)>& shouldDestroy);
	/// <summary>動的オブジェクト（Object3D / Animated とそのモデル / スプライト / Primitive）をすべて遅延破棄する</summary>
	void ClearAllDynamicEntities();

	/// <summary>
	/// 次フレームの ProcessAsyncLoads まで破棄を遅らせる（GPU がまだ使っている物を即破棄しないため）。
	/// 派生シーンが自分で持つ物（スプライン等）を消す時に使う
	/// </summary>
	/// <param name="object">破棄する物（空なら何もしない）</param>
	template <class T>
	void DeferDelete(std::unique_ptr<T> object) {
		if (object) deferredDeletes_.emplace_back(std::shared_ptr<T>(object.release()));
	}

	/// <summary>
	/// 動的に出す物の名前を一意にする（"base", "base (1)", "base (2)" …）。base ごとの次の番号から試すので
	/// 弾を何百発出しても照合は 1 回で済む（毎回 1 から数え直すと物の数の 2 乗になる）。番号は戻さない。
	/// </summary>
	/// <param name="base">名前の元</param>
	/// <param name="items">名前を照合する一覧</param>
	/// <param name="except">items に追加済みの自分自身（照合から外す）</param>
	/// <returns>一意な名前</returns>
	template <class Container>
	std::string MakeUniqueDynamicName(const std::string& base, const Container& items, const void* except = nullptr) {
		int& next = nameCounters_[base];
		for (;;) {
			std::string name = (next == 0) ? base : base + " (" + std::to_string(next) + ")";
			++next;
			bool taken = false;
			for (const auto& p : items) {
				if (static_cast<const void*>(p.get()) != except && p->GetName() == name) { taken = true; break; }
			}
			if (!taken) return name;
		}
	}

private:
	// MakeUniqueDynamicName の base ごとの次の番号
	std::unordered_map<std::string, int> nameCounters_;

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
