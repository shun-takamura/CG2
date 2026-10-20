#pragma once
#include "StageStart/StageStartGeometry.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <string>

class Camera;
class Object3DManager;
class DirectXCore;
class InputManager;
struct ID3D12GraphicsCommandList;
class JsonValue;
class CatapultFacility;
class LaunchChecklist;
class LaunchControlBoard;
class LaunchCable;
class CatapultLaunch;
class LaunchTachometer;
class MechThrusters;
class CatapultLightning;
class StageStartSkip;
class CockpitModel;
class CockpitLighting;
class SkinningComputeManager;
class SRVManager;
class AnimatedModelInstance;
class AnimatedObject3DInstance;

/// <summary>
/// StageStartPart が StagePlayScene に対して必要とする操作だけを切り出したホストインターフェース。
/// StagePlayScene が private override で実装する（RailStagePart / BossStagePart と同じ方式）。
/// </summary>
class IStageStartHost {
public:
	virtual ~IStageStartHost() = default;
	// ゲームの自機（player_）を隠す。演出の間は演出専用の機体（新モデル）を描くので隠しておく
	virtual void SetPlayerHidden(bool hidden) = 0;
	// スタート演出を終えてレールへ渡す：フォグ・空・平行光源・IBL・カメラのクリップ面・自機の表示をステージの値に戻す
	virtual void RestoreStageEnvironment() = 0;
	// 雲の上の場面（S4、F6）を始める：RestoreStageEnvironment の後に呼ぶ。レールを t=0 から進めながら、
	// ゲームの自機を右上前方から見るカメラで回り込んでレールのカメラへ合流させ、羽根を展開する（CloudTopIntro / PlayerWingUnit）
	virtual void BeginCloudTop() = 0;
};

/// <summary>
/// StagePlayScene のスタート演出（16_StageStart.md、カタパルト発進）を所有するコンポーネント。
/// 段階：降下（③IN：HUB から渡された縦穴の位相・速さの続きを降り、カタパルトの階で減速して止まる）
///   → 機体確認（S1、F4）→ 発進（S2、F5）→ 白（S3、F5）→ 雲の上（S4、F6）→ 終了（レールへ渡す）。
/// 雲の上の場面はレールのフェーズで演じる（カタパルトを片付けてから IStageStartHost::BeginCloudTop。中身は CloudTopIntro）。
/// HUB を通らずに来たとき（リトライ・デバッグの直接開始）は雲の上から始める（2026-10-18 ユーザー決定）。
/// 視点：一人称は機体を描かずコクピットの内装と灯りだけ（§4.1）、三人称は自機を機体の右下前方の床近くから見上げる
/// （2026-10-18 ユーザー決定。参考はアニメのカタパルト発進。発進では機体を追って見送る＝F5）。
/// 三人称の自機は演出専用の新モデル（PlayerModel、HubMech と同じ 18m に拡大。羽根はカタパルトでは収納＝描かない）。
/// ゲームの自機（player_）は演出の間は隠す。
/// 施設は CatapultFacility、ワールドの離れた所に置く。調整値は StagePlay.json の "stageStart"、
/// コクピットの灯りは Hub.json の "cockpitLighting"（HUB と同じ値。切れ目で見え方をそろえる）。
/// </summary>
class StageStartPart {
public:
	enum class State {
		Descent,      // ③IN：縦穴の続きを降りてカタパルトで止まる
		Inspection,   // S1 機体確認（F4）
		Launch,       // S2 発進（F5）
		WhiteOut,     // S3 射出口の白（F5）
		CloudTop,     // S4 雲の上 → レールのカメラ（F6）
		Finished,     // レールへ渡した
	};
	enum class View {
		FirstPerson,
		ThirdPerson,
	};

	StageStartPart();
	~StageStartPart();

	void Initialize(IStageStartHost* host, Camera* camera, Object3DManager* object3DManager, DirectXCore* dxCore,
		SkinningComputeManager* skinningComputeManager, SRVManager* srvManager);
	/// <summary>三人称で映す機体を作る（重いので StagePlay が後回しの段で呼ぶ。2 回目以降は何もしない）</summary>
	void InitializeMech(Object3DManager* object3DManager, SkinningComputeManager* skinningComputeManager,
		DirectXCore* dxCore, SRVManager* srvManager);
	void Finalize();

	/// <summary>③IN：HUB から渡された値で降下の続きから始める</summary>
	void BeginDescent(const StageStartHandoff& handoff);
	/// <summary>HUB を通らずに来た：雲の上から始める</summary>
	void BeginFromCloudTop();
	/// <summary>降下を飛ばし、カタパルトに着いて止まった所（機体確認の頭）から始める（デバッグ）</summary>
	void BeginInspection(const StageStartHandoff& handoff);
	/// <summary>途中で打ち切ってレールへ渡す（デバッグの F4・シーク）</summary>
	void Skip() { if (active_) Finish(); }
	/// <summary>HUB を通らずに降下から流すときの値（デバッグ。目の位置は座ったパイロットの目）</summary>
	StageStartHandoff MakeDefaultHandoff() const;

	/// <summary>進行とカメラの構え（位置・向き）を決める。この後にシーンがカメラの行列を作り（揺れも足す）、LateUpdate を呼ぶ</summary>
	void Update(InputManager* input, float deltaTime, const Vector2& screenSize);
	/// <summary>
	/// カメラの行列ができた後に、施設・コクピット・機体・ケーブル・表示板の行列を作る。
	/// Object3DInstance::Update はその時のカメラで WVP を作るので、先に作ると 1 フレーム前のカメラになり、
	/// 射出で速く進むと一人称のコクピット（椅子）が前へずれて見えた（2026-10-19 実機）
	/// </summary>
	void LateUpdate(float deltaTime, const Vector2& screenSize);
	/// <summary>表示板の中身を RenderTexture へ。シーンの RT を描き始める前に呼ぶ（RTV とビューポートを切り替える）</summary>
	void RenderBoardTexture(ID3D12GraphicsCommandList* commandList);
	/// <summary>
	/// 画面の上に重ねるもの（スキップの案内・スキップの白フェード・一人称で射出口に近づくまぶしさ）。
	/// TextRenderer に積む（Flush はシーン）。スキップの白は演出が終わった後（レール）も開き切るまで描く
	/// </summary>
	void DrawScreenOverlay(float screenWidth, float screenHeight);
	/// <summary>スキップの白フェードを進める（演出の外でも毎フレーム呼ぶ）</summary>
	void UpdateOverlay(float deltaTime);
	/// <summary>不透明物（Object3D の DrawSetting・BindLights の後）。表示板（半透明。一人称は宙に浮く板・三人称は立てたパネル）も最後に描く</summary>
	void Draw(DirectXCore* dxCore);
	bool HasBloom() const;
	void DrawBloom(DirectXCore* dxCore);

	/// <summary>演出中（StagePlay はこの間レール・HUD を動かさない）</summary>
	bool IsActive() const { return active_; }
	State GetState() const { return state_; }
	View GetView() const { return view_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	/// <summary>Hub.json の "cockpitLighting"</summary>
	void LoadCockpitTuning(const JsonValue& node);
	void OnImGui();

	/// <summary>射出口の向こうに見せる雲の上の点（ワールド。StagePlay がレールの t=0 のカメラ位置を渡す）</summary>
	void SetSkyAnchor(const Vector3& anchor) { skyAnchor_ = anchor; }
	/// <summary>射出口の向こうに雲海を描くか（射出口を空に開けているときだけ）</summary>
	bool DrawsCloudsThroughExit() const;
	/// <summary>
	/// 雲海を描く仮のカメラのずらし量。今のカメラの位置にこれを足すと、射出口（床の中心）が雲の上の点に重なる。
	/// 向き・画角は同じなので、トンネルの壁の深度で雲が隠れ、射出口の穴からだけ雲海が見える
	/// </summary>
	Vector3 GetSkyViewOffset() const;

	/// <summary>デバッグ：HUB を通らずに来たとき（Debug の初期シーンなど）にどこから始めるか（StagePlay.json、Debug ビルドだけ効く）</summary>
	enum class DebugStart { CloudTop, Descent, Inspection };
	DebugStart GetDebugStart() const { return debugStart_; }

private:
	void Enter(State state);
	void Finish();
	// 機体確認（S1、F4）：項目を確認中 → 全部 CLEAR（カタパルトのネオンを点け直す）→ LAUNCH READY（SPACE でブースト）
	enum class InspectionStep { Checking, PowerUp, Ready };
	void UpdateInspection(InputManager* input, float deltaTime);
	void UpdateBoard(float deltaTime, const Vector2& screenSize);
	// 4 基のスラスター：機体確認は WASD、充電後は SPACE の出力、射出中は大。一人称では噴かさない
	void UpdateThrusters();
	// 電磁カタパルトの雷：ネオンの点け直しと一緒に伸び、点き切った後は溝で放電（SPACE で強く）、射出中は固定台の足元
	void UpdateLightning(float deltaTime);
	// 固定台の前進量（レールの終わりで止まる）
	float ClampTravel() const;
	// 三人称のパネル（表示板・タコメーター）を消しているか（射出が始まったら）
	bool ArePanelsHidden() const;
	// 射出の速さの割合 0..1（画角・ラジアルブラー）。射出口を抜けたら白が引くのと同じ時間で線形に 0 へ戻す
	float SpeedFactor() const;
	// 射出の速さに合わせたラジアルブラー（一人称。演出の外では切る）
	void ApplyRadialBlur();
	// 一人称の射出口のまぶしさ（glare_）を進める
	void UpdateGlare();
	// スキップ：白で覆い切ったら雲の上へ（演出の物を片付けて CloudTop へ）
	void JumpToCloudTop();
	float SkipWhiteAlpha() const;
	// 演出の間だけポイントライト（カタパルト・コクピット）を借りる
	void AcquireLights();
	void ReleaseLights();
	// 降下：時刻 t までに降りた距離・その時の速さ（閉じた式）
	float DescentDistanceAt(float time) const;
	void UpdateDescent(float deltaTime);
	// 三人称の機体の姿勢：Stand（直立のループ）→ Ignite（前傾へ、1 回）→ Catapult（前傾の保持のループ）
	// → 射出口でけり出す（Launch、1 回。最後のコマ＝飛ぶ姿勢で止める。Fly は使わない＝2026-10-19 ユーザー決定）
	enum class MechPose { Stand, Ignite, Catapult, Launch };
	void PlayMechPose(MechPose pose);
	void UpdateMechPose(float deltaTime);
	void ApplyCamera(float deltaTime);
	// 機体が前（+Z）へ走った距離（発進の分＋デバッグのプレビュー）
	float MechTravel() const { return mechTravel_ + previewTravel_; }
	// 三人称の注視点（機体から見た点）。前進量で始め → 真横 → 奥のキーを補間する
	Vector3 ThirdPersonTargetAt(float travel) const;
	void ApplyLighting(const Vector2& screenSize);
	// 給電ケーブル（F5）：付け根（カタパルトの後ろの壁）と機体側の端（背中のボーン＋ずらし）
	Vector3 CableAnchorWorld() const;
	Vector3 CableAttachWorld() const;
	void SetEnvironmentTexture(const std::string& path);

	IStageStartHost* host_ = nullptr;
	Camera* camera_ = nullptr;
	Object3DManager* object3DManager_ = nullptr;

	StageStartGeometry geometry_;
	std::unique_ptr<CatapultFacility> facility_;
	std::unique_ptr<CockpitModel> cockpit_;
	std::unique_ptr<CockpitLighting> cockpitLighting_;
	std::unique_ptr<LaunchChecklist> checklist_;
	std::unique_ptr<LaunchControlBoard> board_;
	std::unique_ptr<LaunchCable> cable_;
	std::unique_ptr<CatapultLaunch> launch_;
	std::unique_ptr<LaunchTachometer> tachometer_;
	std::unique_ptr<MechThrusters> thrusters_;
	std::unique_ptr<CatapultLightning> lightning_;
	std::unique_ptr<StageStartSkip> skip_;
	// 三人称で映す機体（新モデルの体。ファイルが無ければ描かない）
	std::unique_ptr<AnimatedModelInstance> mechModel_;
	std::unique_ptr<AnimatedObject3DInstance> mech_;

	bool active_ = false;
	bool lightsHeld_ = false;
	State state_ = State::Finished;
	View view_ = View::FirstPerson;
	float stateTime_ = 0.0f;
	InspectionStep inspectionStep_ = InspectionStep::Checking;
	float powerUpStartTime_ = 0.0f;   // ネオンの点け直し（＝カタパルトの充電）を始めた stateTime_（タコメーターが出る時刻）
	float inspectionStepTime_ = 0.0f;
	StageStartHandoff handoff_;
	std::string currentIblPath_;
	float lcdScroll_ = 0.0f;
	MechPose mechPose_ = MechPose::Stand;
	float mechTravel_ = 0.0f;      // 発進（F5）で機体と固定台が溝に沿って前へ走った距離 [m]
	bool holdChecking_ = false;     // ImGui：全部 CLEAR になっても機体確認から先へ進めない（スラスターの確認用。保存しない）
	float previewTravel_ = 0.0f;    // ImGui のプレビュー（保存しない）。F5 の前に見送りの画角を確かめる
	Vector3 smoothedTarget_ = { 0.0f, 0.0f, 0.0f };   // 三人称の注視点（ワールド、遅れて機体を追う）
	bool smoothedTargetValid_ = false;

	// 降下（BeginDescent で決める）
	float startLiftY_ = 0.0f;     // 始めのリフトの上面の高さ（カタパルトの原点から）
	float cruiseSpeed_ = 4.0f;    // HUB から渡された速さ
	float cruiseSeconds_ = 0.0f;  // 一定の速さで降りる秒
	float brakeSeconds_ = 0.0f;   // 減速して止まるまでの秒
	float brakeDistance_ = 0.0f;
	bool arrived_ = false;

	// 調整値（StagePlay.json の "stageStart"）
	Vector3 origin_ = { 0.0f, -3000.0f, 0.0f };   // カタパルトの原点（雲・フォグ・地形のロードの範囲から離す）
	int startUnitsAboveShaftBase_ = 2;            // 降下の始め：縦穴の付け根（天井の上）から何単位上か
	float brakeDistanceTuning_ = 14.0f;           // 減速の距離 [m]（止まる位置＝カタパルトの床の高さの手前から）
	float defaultDescentSpeed_ = 4.0f;            // HUB から速さが来なかったとき [m/s]
	float arriveShakeIntensity_ = 0.03f;          // 止まったときの揺れ
	float arriveShakeSeconds_ = 0.3f;
	float settleSeconds_ = 0.6f;                  // 止まってから機体確認へ
	float hallSunScale_ = 0.0f;                   // カタパルトの階での外の光の倍率（地下なので 0）
	float lampLevelInShaft_ = 1.0f;               // 縦穴にいる間のカタパルトの灯り
	// カタパルトの IBL（gen_catapult.py --ibl。チェック完了の緑は catapult_ibl_ready.dds＝F4 で差し替え）。無ければ縦穴の IBL のまま
	std::string catapultIblPath_ = "Resources/Cubemaps/catapult_ibl.dds";
	// 射出口までネオンが緑に点き切ったら替える（F4）。無ければ赤ピンクのまま
	std::string catapultReadyIblPath_ = "Resources/Cubemaps/catapult_ibl_ready.dds";
	float boardAppearDelay_ = 0.3f;               // 機体確認に入ってから表示板が出るまで [s]
	float attractLaunchWait_ = 1.5f;              // デモモード：充電が済んでから SPACE を押し始めるまで [s]（押し続けて射出）
	// 射出口を出てから白（一人称）／切り替え（三人称）までの見送り [s]（2026-10-19 ユーザー：出たら即遷移だと
	// 三人称でケーブルが見られず、一人称も空へ飛び出す爽快感が出ない）
	float firstPersonAfterExitSeconds_ = 1.0f;   // 白が引いて空が見えてから切り替える
	float thirdPersonAfterExitSeconds_ = 1.8f;
	float launchPanelHideDelay_ = 0.15f;
	// 射出の速さの感じ（2026-10-19 ユーザー：画角を広げる・ラジアルブラー）。速さ＝射出口での速さに対する割合
	float fovKickDegrees_ = 15.0f;        // 一人称：画角をこれだけ広げる
	float radialBlurMax_ = 0.04f;         // RadialBlurEffect::SetBlurWidth の最大
	bool radialBlurThirdPerson_ = false;  // 三人称にもかけるか
	float radialBlurMaskDistance_ = 6.0f; // 一人称：目からこの距離 [m] より手前（コクピットの内装・表示板）にはかけない
	// 一人称の射出口の光（2026-10-19 ユーザー：近づくにつれて全体が白っぽくまぶしく、真っ白にはしない。
	// 抜けたら目が慣れるように白が引いて空が見え、その後パッと雲の上へ）
	float glareDistance_ = 80.0f;         // 射出口のこの距離 [m] 手前からまぶしくなり始める
	float glareMax_ = 0.75f;              // 射出口での白の濃さ（1 で真っ白）
	float glareAdaptSeconds_ = 0.6f;      // 抜けてから白が引くまで
	float glare_ = 0.0f;                  // 今の白の濃さ（保存しない）
	float exitCrossTime_ = -1.0f;         // 目が射出口を抜けた stateTime_（負＝まだ）
	// スキップ（LSHIFT 長押し。2026-10-19 ユーザー：雲の上へ・白フェードを挟む）
	float skipFadeTime_ = -1.0f;          // 白フェードの経過（負＝していない）
	bool skipJumped_ = false;
	float skipFadeIn_ = 0.35f;            // 白くなるまで
	float skipFadeHold_ = 0.15f;          // 白のまま
	float skipFadeOut_ = 0.6f;            // 雲の上（今はレール）で開くまで
	// 射出口のけり出し（2026-10-19 ユーザー：前傾のまま空へ出ると空気椅子に見えた）。
	// Launch のクリップ（0.9 秒）は 0.45 秒にけり出すので、射出口に着く kickLeadSeconds 前に再生を始める
	float kickLeadSeconds_ = 0.45f;
	// 固定台（溝の台車）はレールの終わり＝射出口のこの距離 [m] 手前で止まり、機体だけが飛び出す
	float clampStopBeforeExit_ = 2.0f;          // 三人称：射出が始まってから表示板・タコメーターのパネルを消すまで [s]（走る機体と重ならないように）
	float nearClip_ = 0.1f;                       // コクピットの内装が近いので
	Vector4 cockpitSignColor_ = { 0.35f, 1.0f, 0.65f, 1.0f };   // HATCH LOCKED の緑（HatchSign と同じ）
	float lcdScrollSpeed_ = 4.0f;                 // 液晶の文字を流す速さ [m/s]
	// 三人称。機体の右は −X、正面は +Z。
	// 目はカタパルトに固定（リフトの上面の中心から）。床の平らな所は |x| ≤ 11、溝は x ±3 なので、その間の床の近くに置く。
	// 注視点は機体から見た点（発進で機体が前へ走ると、カメラは残ったまま向きだけ機体を追って見送る）。
	// 始め（前進 0）→ 真横（機体がカメラの z を通る）→ 奥（真横から farDistance 進んだ所）の 3 つのキーを smoothstep でつなぐ
	Vector3 thirdPersonEye_ = { -9.0f, 1.5f, 24.0f };
	Vector3 thirdPersonTarget_ = { 0.0f, 8.0f, 0.0f };       // 始め：見上げる
	Vector3 thirdPersonPassTarget_ = { 0.0f, 2.0f, 0.0f };   // 真横：足元
	Vector3 thirdPersonFarTarget_ = { 0.0f, 6.0f, 0.0f };    // 奥へ遠ざかる機体
	float thirdPersonFarDistance_ = 40.0f;                    // 真横から奥のキーまで [m]
	// 射出口の光：機体が射出口の exitDimDistance 手前から射出口までで ×1 → exitDimNear に弱める（白は S3 の白フェードで出す）
	float exitDimNear_ = 0.3f;
	float exitDimDistance_ = 60.0f;
	// 射出口の向こうの雲海（2026-10-18 ユーザー決定：一人称では射出口が小さく、空だけだと水色の板に見えるため）
	bool cloudsThroughExit_ = true;
	Vector3 skyAnchor_ = { 0.0f, 0.0f, 0.0f };         // レールの t=0 のカメラ位置（StagePlay が渡す。保存しない）
	Vector3 skyAnchorOffset_ = { 0.0f, 0.0f, 0.0f };   // そこからのずらし [m]
	float thirdPersonTargetLag_ = 0.0f;           // 注視点が機体を追う遅れ [s]（0 で遅れなし）
	float thirdPersonFovDegrees_ = 60.0f;
	float mechScale_ = 10.625f;                   // 新モデル（全高 1.694m）を HubMech と同じ 18m に。足の中心が固定台 (±3.0, 0, 1.2) に乗る
	// HUB を通らずに来たら雲の上から（F5 の作業中は機体確認の頭からにしていた。2026-10-19 F5 の終わりに戻した。
	// カタパルトを試すときは ImGui の Debug Start (no HUB) を inspection に）
	DebugStart debugStart_ = DebugStart::CloudTop;
};
