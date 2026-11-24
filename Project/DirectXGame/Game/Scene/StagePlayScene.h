#pragma once
#include "GameScene.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Wave/WaveDef.h"
#include "Effect/EffectManager.h"
#include "Transform.h"
#include "BoneSocket.h"
#include "RailStagePart.h"
#include "BossStagePart.h"
#include "BossClearSequence.h"
#include "StageStart/StageStartPart.h"
#include "StageStart/MechThrusters.h"
#include "PlayerDeath/PlayerDeathPart.h"
#include "FlightUnit/FlightUnitPart.h"
#include "Beam/PlayerBeamPart.h"
#include "Special/ISpecialMoveHost.h"
#include "Special/ShatterlinePart.h"
#include "Special/StormLockPart.h"
#include "Loot/LootPickupLog.h"
#include "StageEnvironment.h"
#include "StageStreaming.h"
#include "VerticalMenu.h"
#include "ControlGuide.h"
#include "AttractMode.h"
#include "Enemy/Boss/Special/BossGravityOrb.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <unordered_set>
#include <utility>
#include <cstdint>

class Camera;
class Skybox;
class SplineCurveActor;
class AnimatedObject3DInstance;
class Object3DInstance;
class Reticle;
class SpriteInstance;
class LightningRuntime;
class RailStagePart;
class BossStagePart;
class BossArenaWater;
class RiverWater;
class CloudTopIntro;
class PlayerWingUnit;
class BeamShieldRenderer;
class BeamBladeRenderer;

/// <summary>
/// ステージプレイシーン
/// 道中レールSTG(2-3分) → 着地遷移 → ボス戦3Dアクション(3-5分) をシームレスに繋ぐ。
/// Rail 専用ロジック（レールカメラ駆動・Wave/敵スポーン・Seek再構築）は RailStagePart に切り出し、
/// StagePlayScene は IRailStageHost 経由でのみそれを操作する（公開APIは増やさない）。
/// </summary>
class StagePlayScene : public GameScene, private IRailStageHost, private IBossStageHost, private IStageStartHost,
	private IPlayerDeathHost, private IStageStreamingHost, private IFlightUnitHost, private ISpecialMoveHost,
	private IPlayerBeamHost, private IBossGravityOrbHost {
public:
	enum class Phase {
		Rail,       // レールシューティング道中
		Landing,    // 着地遷移（カメラ補間）
		Boss,       // ボス戦3Dアクション
		StageStart, // スタート演出（16_StageStart.md。Rail の前。名前の表の都合で最後に置く）
	};

	StagePlayScene();
	~StagePlayScene() override;

	/// <summary>
	/// カメラ・空・環境・レール・ボス戦・スタート演出・UI・自機などを作り、調整値と配置を読んでステージを始める。
	/// HUB の降下の続き（③IN）から始める時は、降下に要らない物の初期化を後回しの段に積む
	/// </summary>
	void Initialize() override;
	/// <summary>画面の水滴・必殺技の GPU リソース・各パートを片付ける（GPU の完了を待ってから解放する）</summary>
	void Finalize() override;
	/// <summary>
	/// 1 フレームの更新（ポーズ → 後回しの初期化 → 段階の遷移 → カメラ・自機 → 照準・射撃・戦闘 → UI）。
	/// トランジションで覆われている間は止める（最初の 1 回だけは行列を書くために通す）
	/// </summary>
	void Update() override;
	/// <summary>水面のシミュレーション・反射 → 空 → 不透明物 → 水面・雲海 → 半透明・エフェクト → HUD・ポーズの順に描く（スタート演出の間は演出を描く）</summary>
	void Draw() override;
	/// <summary>ポストエフェクト（崩壊リビールの色反転）の後の最終 RT に Shatterline の破片を重ねて描く</summary>
	/// <param name="commandList">描画先のコマンドリスト</param>
	void DrawAfterPostEffect(ID3D12GraphicsCommandList* commandList) override;

	/// <summary>シーンタイムラインのシーク。RailCamera の progress を経過秒から再計算する（Boss/Landing 中なら Rail へ戻す）</summary>
	/// <param name="seconds">飛ぶ先のステージ経過秒</param>
	void Seek(float seconds) override;
	/// <summary>レールカメラの進行度</summary>
	/// <returns>進行度 t（0〜1。レールが無ければ -1）</returns>
	float GetCameraProgressT() const override;
	/// <summary>Rail 終了（Landing 遷移）トリガー秒。ImGuiManager の Scene Timeline パネルの Seek Max 表示に使う</summary>
	/// <returns>トリガー秒 [秒]</returns>
	float GetSeekMaxSeconds() const override;

	/// <summary>シーンのカメラを取得</summary>
	/// <returns>シーンのカメラ</returns>
	Camera* GetCamera() override;

	/// <summary>発光パス（スタート演出の照明の帯・ネオン・コクピットの灯り、羽根のディゾルブの縁、死亡演出の光）があるか</summary>
	/// <returns>発光させる物があれば true</returns>
	bool HasBloomSources() const override;
	/// <summary>発光する物をブルームのマスクへ描く</summary>
	void DrawBloomSources() override;

	/// <summary>
	/// Wave Editor 有効時、ビューポートへのプレハブドロップをウェーブエントリ追加として処理する。Debug ビルドのみ機能。
	/// </summary>
	/// <param name="prefabName">ドロップしたプレハブ名</param>
	/// <param name="relX">ビューポート内の横の位置（0〜1）</param>
	/// <param name="relY">ビューポート内の縦の位置（0〜1）</param>
	/// <returns>true でドロップを消費（通常の InstantiatePrefab を抑止）</returns>
	bool OnViewportPrefabDrop(const std::string& prefabName, float relX, float relY) override;

	/// <summary>ボスの必殺技 GravityOrb の段取りを返す（ボス戦の間だけ）</summary>
	/// <returns>段取り。ボス戦でなければ nullptr</returns>
	BossGravityOrb* GetBossGravityOrb() override;

protected:
	// シーン配置（プレハブ・スプライン等）の保存/復元。
	// 本体は GameScene 側に集約済みで、ここは StagePlay 固有の差分だけを差し込む。
	// 呼ぶのは基底なので public にはしない（シーンの公開APIを増やさない流儀）。
	/// <summary>JSON の "scene" に書く名前</summary>
	/// <returns>"StagePlayScene"</returns>
	std::string GetSceneJsonName() const override;
	/// <summary>保存から外すエンティティか（プレイヤーはプレハブから復元するので外す。弾・攻撃判定・敵・ボス・崩れた部品・落とし物・HP/必殺ゲージの UI はランタイム実体）</summary>
	/// <param name="entity">調べるエンティティ</param>
	/// <param name="tag">そのエンティティのタグ</param>
	/// <returns>true なら保存しない</returns>
	bool ShouldSkipOnSave(const IImGuiEditable* entity, EntityTag tag) const override;
	/// <summary>復元しないエントリか（過去の汚れた save 対策：一時オブジェクト・ランタイム敵が混ざっていても復元しない）</summary>
	/// <param name="desc">読んだエントリ</param>
	/// <returns>true なら復元しない</returns>
	bool ShouldSkipOnLoad(const SceneEntityDesc& desc) const override;
	/// <summary>読み込みでは作らず取っておくエントリか（ストリーミングの時刻表にあるグループだけ後で作る）</summary>
	/// <param name="desc">読んだエントリ（streamGroup 付き）</param>
	/// <returns>時刻表にあるグループなら true</returns>
	bool ShouldStreamOnLoad(const SceneEntityDesc& desc) const override;
	/// <summary>全エンティティが消えるので、それを指す参照とゲーム状態（敵のコントローラ・回避など）を落とす</summary>
	void OnBeforeSceneLoad() override;
	/// <summary>復元が終わった後に自機の貼り直し・レールの走行スプラインの取り直し・UI の建て直しをする</summary>
	void OnAfterSceneLoad() override;

private:
	// ----- 1 フレームの更新の手順（Update がこの順に呼ぶ。中身は StagePlayScene.cpp の各関数のコメント）-----
	/// <summary>Pause アクションでポーズを開閉し、ポーズ中はメニューだけを動かす</summary>
	/// <param name="actions">アクションの入力</param>
	/// <returns>ポーズを開閉したフレーム・ポーズ中は true（ゲームを進めない）</returns>
	bool HandlePauseInput(class InputActionMap* actions);
	/// <summary>後回しにした初期化：③IN の降下の間だけ 1 フレーム数 ms ずつ進め、それ以外の段階へ進んだら残りを全部済ませる</summary>
	void AdvanceDeferredInit();
#ifdef _DEBUG
	/// <summary>DEBUG 専用ショートカット（F2=リザルト / F3=HUB / F4=段階送り）</summary>
	/// <returns>シーンを切り替えたら true</returns>
	bool HandleDebugShortcuts();
#endif
	/// <summary>ステージ進行ステートマシン（Rail → Landing → Boss）。遷移の条件だけを見る（入った時の処理は OnStagePhaseChanged）</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdateStagePhase(class InputActionMap* actions);
	/// <summary>段階が変わったフレームに 1 回：前の段階の片付けと、次の段階に入る処理</summary>
	void OnStagePhaseChanged();
	/// <summary>ボス戦の毎フレーム（ボス AI・HP バー・撃破 → STAGE CLEAR の段取り）</summary>
	void UpdateBossBattle();
	/// <summary>自機を置く前の基準カメラ（精密射撃の FOV・カメラ行列・デバッグカメラ）</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdateBaseCamera(class InputActionMap* actions);
	/// <summary>自機の移動（ボス戦は地上、それ以外はカメラ空間のオフセット）と、自機を置いた後で上書きするカメラ</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdatePlayerMovement(class InputActionMap* actions);
	/// <summary>移動入力（WASD / 左スティック）を読む</summary>
	/// <returns>移動入力（x=右, y=上。-1..1）</returns>
	Vector2 ReadMoveInput() const;
	/// <summary>ボス戦：地上の移動・回避・分身カウンター → 向き → カメラ</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="moveDelta">移動入力（-1..1）</param>
	/// <param name="dt">経過時間 [秒]（Player グループ）</param>
	void UpdateBossPlayerMovement(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	/// <summary>
	/// ボス戦の自機の向き（yaw のみ）を更新する。ジャスト回避スロー中は攻撃元（ボス）方向、通常時は移動入力方向（入力が無ければ保持）。
	/// 実際の SetRotate は後段の照準（UpdateAimTarget）で適用する
	/// </summary>
	/// <param name="moveDelta">移動入力（-1..1）</param>
	/// <param name="playerPos">自機の位置</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateBossPlayerFacing(const Vector2& moveDelta, const Vector3& playerPos, float dt);
	/// <summary>ボス戦のカメラ（右スティック・マウスの look、L1 の短押し/長押し）と、その上に重ねる演出カメラ</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="playerPos">自機の位置</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateBossCamera(class InputActionMap* actions, const Vector3& playerPos, float dt);
	/// <summary>レール：慣性付きの移動 → 回避 → 画面端・床で止める → ワールドに置く → 演出カメラ</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="moveDelta">移動入力（-1..1）</param>
	/// <param name="dt">経過時間 [秒]（Player グループ）</param>
	void UpdateRailPlayerMovement(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	/// <summary>
	/// 速度で動かした候補のオフセットを、コライダーが画面端（+ マージン）と水面の床を越えない軸だけ採用する。
	/// 拒否した軸は速度もゼロに（壁にぶつかった扱い）
	/// </summary>
	/// <param name="dt">経過時間 [秒]</param>
	void MoveRailPlayerInsideScreen(float dt);
	/// <summary>
	/// 追加回避の戻り：コライダー 8 角の最大 NDC で軸ごとに判定し、通常マージンを超えている軸だけ内側へ押し戻す
	/// （ダッシュで取った別軸位置は維持）。全体が枠内に戻ったら派生を終える
	/// </summary>
	/// <param name="dt">経過時間 [秒]</param>
	void PushRailPlayerBackAfterDodge(float dt);
	/// <summary>
	/// オフセットから自機のワールド位置を出す。近接派生中は敵の手前まで詰め寄る位置で上書きし、
	/// 体の中心まわりに回ったように足元の位置をずらす
	/// </summary>
	/// <returns>自機の足元のワールド位置</returns>
	Vector3 ComputeRailPlayerWorldPos();
	/// <summary>自機を置いた後に上書きするカメラ（精密射撃・ジャスト回避・必殺技・雲の上・死亡演出）</summary>
	/// <param name="worldPos">自機のワールド位置</param>
	void ApplyRailOverrideCameras(const Vector3& worldPos);
	/// <summary>道中の環境（空・ライト・フォグのセクション、大河、画面の水滴、区間ごとのストリーミング）</summary>
	void UpdateStageEnvironment();
	/// <summary>Skybox の演出（必殺技の暗転・ボス戦の空）と更新。カメラが確定した後に呼ぶ</summary>
	void UpdateSkyboxAfterCamera();
	/// <summary>自機のスケルトンが更新された後に、骨に付ける物（武器・盾・飛行ユニット・羽根・スラスター）と死亡演出を進める</summary>
	void UpdatePlayerAttachments();
	/// <summary>レティクルの位置（ボス戦・雲の上は中央固定、それ以外はマウス/スティック）</summary>
	void UpdateReticle();
	/// <summary>照準（狙う敵・発射先・自機の向き・ロックオン表示）</summary>
	void UpdateAimTarget();
	/// <summary>自機の向き：狙う点をなめらかに追い、段階・演出ごとの決まった向きを優先する</summary>
	/// <param name="desiredTarget">狙う点（ワールド）</param>
	void UpdatePlayerFacing(const Vector3& desiredTarget);
	/// <summary>ロックオン中の見た目：敵を囲む大きさと、弱近接 1 段目が届くか（届くなら近接用の絵）</summary>
	/// <param name="hitEnemy">レティクルが敵に掛かっているか</param>
	/// <param name="distToEnemy">カメラから敵の中心までの距離 [m]</param>
	/// <param name="enemyRadius">敵のコライダーの半径 [m]</param>
	/// <param name="enemyCenter">敵の中心（ワールド）</param>
	void UpdateReticleLockDisplay(bool hitEnemy, float distToEnemy, float enemyRadius, const Vector3& enemyCenter);
	/// <summary>射撃とチャージ（必殺技・行動ロック・被弾後の射撃禁止の間は撃たない）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="gameFrozen">World が止まっているか（エディタの一時停止など）</param>
	void UpdateShooting(class InputActionMap* actions, bool gameFrozen);
	/// <summary>射撃ボタンを離している間にチャージを進める（1 段目＝外側のレティクルとエフェクト、2 段目＝差し替え）</summary>
	/// <param name="dtP">経過時間 [秒]（Player グループ）</param>
	void UpdateChargeLevel(float dtP);
	/// <summary>1 発撃つ（チャージ段階で弾を選び、メイン武器・精密射撃の補正を掛ける）。撃ったらチャージを戻す</summary>
	void FirePlayerShot();
	/// <summary>近接・弾・飛行ユニット・被弾・必殺技（射撃より後。弾はこのフレームに撃った物も進める）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="worldDt">世界の経過時間 [秒]</param>
	/// <param name="gameFrozen">World が止まっているか</param>
	void UpdateCombatAndSpecial(class InputActionMap* actions, float worldDt, bool gameFrozen);
	/// <summary>state.log：SUNDAY のハング検知・スタック判定の基準（フレーム末尾の位置・HP・シーン）を書く</summary>
	void WriteFrameStateLog();

	/// <summary>雲海の降下の縦穴をレールの降下区間から拾い直す（シーン読み込みでレールが差し替わった後に呼ぶ）</summary>
	void RebuildCloudShaft();

	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;

	// STG（Rail）専用ロジック一式（レールカメラ駆動・Wave/敵スポーン・Seek再構築）。
	// StagePlayScene は IRailStageHost 経由でのみこれを操作する。
	std::unique_ptr<RailStagePart> railStage_;

	// 道中の環境演出（セクションごとの空・平行光源・フォグ）を経過秒で駆動する。
	// Rail フェーズ中だけ Update される。セクション0件なら何もしない＝従来動作。
	std::unique_ptr<StageEnvironment> stageEnv_;

	// ----- IRailStageHost 実装（private override。外部からは呼べない。RailStagePart 経由のみ）-----
	/// <summary>指定プレハブから敵を1体スポーンし、指定スプラインに沿って進ませる（GameScene::SpawnEnemyOnSpline の委譲）</summary>
	/// <param name="prefabName">敵のプレハブ名</param>
	/// <param name="spline">沿わせるスプライン</param>
	/// <param name="speed">スプラインの進み [t/秒]</param>
	/// <param name="removeAtEnd">終端に着いたら破棄するなら true</param>
	/// <param name="initialT">スプラインの始めの位置（0〜1）</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	/// <returns>出した敵。出せなければ nullptr</returns>
	IImGuiEditable* SpawnEnemyOnSpline(const std::string& prefabName, SplineCurveActor* spline,
		float speed, bool removeAtEnd, float initialT, int waveEntryIndex) override {
		return GameScene::SpawnEnemyOnSpline(prefabName, spline, speed, removeAtEnd, initialT, waveEntryIndex);
	}
	/// <summary>指定プレハブをスプラインなしで指定座標に直接スポーンする（GameScene::SpawnEnemyAt の委譲）</summary>
	/// <param name="prefabName">プレハブ名</param>
	/// <param name="pos">置く位置（ワールド）</param>
	/// <returns>出した実体。出せなければ nullptr</returns>
	IImGuiEditable* SpawnEnemyAt(const std::string& prefabName, const Vector3& pos) override {
		return GameScene::SpawnEnemyAt(prefabName, pos);
	}
	/// <summary>
	/// 敵コントローラを登録する（GameScene::RegisterEnemyController の委譲。IBossStageHost も満たす）。
	/// unique_ptr<EnemyController> の値渡し（デストラクタ実体化）に完全型が要るため定義は .cpp
	/// </summary>
	/// <param name="ctrl">登録するコントローラ（所有権を移す）</param>
	void RegisterEnemyController(std::unique_ptr<EnemyController> ctrl) override;
	/// <summary>スプラインに沿う敵を進める（GameScene::UpdateMovingEnemies の委譲）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateMovingEnemies(float dt) override { GameScene::UpdateMovingEnemies(dt); }
	/// <summary>敵コントローラを更新する（GameScene::UpdateEnemyControllers の委譲。IBossStageHost も満たす）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	/// <param name="player">自機</param>
	/// <param name="stageTimeSec">ステージの経過時間 [秒]</param>
	void UpdateEnemyControllers(float dt, IImGuiEditable* player, float stageTimeSec) override {
		GameScene::UpdateEnemyControllers(dt, player, stageTimeSec);
	}
	/// <summary>HP が尽きた動的エンティティを死亡エフェクト付きで破棄する（GameScene::SweepDeadEntities の委譲。IBossStageHost も満たす）</summary>
	void SweepDeadEntities() override { GameScene::SweepDeadEntities(); }
	/// <summary>シーンをまたいで残るプレイの状態を取得する（GameScene の物。IRailStageHost / IBossStageHost / IFlightUnitHost を満たす）</summary>
	/// <returns>プレイの状態</returns>
	GameSession* GetGameSession() const override { return GameScene::GetGameSession(); }
	// ResetDodgeState() の override 宣言は下方の既存宣言（回避まわりのセクション）に付与済み。
	/// <summary>
	/// 自機を取得する（IRailStageHost / IBossStageHost / IFlightUnitHost / ISpecialMoveHost を 1 つで満たす）。
	/// player_ は AnimatedObject3DInstance*（ヘッダでは前方宣言のみ＝IImGuiEditable への
	/// アップキャストができない）ため、定義は .cpp（完全型あり）に置く。
	/// </summary>
	/// <returns>自機。いなければ nullptr</returns>
	IImGuiEditable* GetPlayer() const override;
	/// <summary>時間グループの進む速さの倍率を返す（IRailStageHost / IFlightUnitHost を満たす）</summary>
	/// <param name="g">対象の時間グループ</param>
	/// <returns>倍率（1 で通常）</returns>
	float GetTimeScale(TimeGroup g) const override { return Scene::GetTimeScale(g); }
	/// <summary>時間グループの進む速さの倍率を設定する（IRailStageHost / IFlightUnitHost / ISpecialMoveHost を満たす）</summary>
	/// <param name="g">対象の時間グループ</param>
	/// <param name="scale">倍率（0 で停止、1 で通常）</param>
	void SetTimeScale(TimeGroup g, float scale) override { Scene::SetTimeScale(g, scale); }
	/// <summary>movingEnemies_ の中から (entity, waveEntryIndex) を列挙する（キー記録用）</summary>
	/// <param name="fn">敵と Wave のエントリ番号を受け取る関数</param>
	void ForEachMovingEnemy(const std::function<void(IImGuiEditable*, int)>& fn) override { ForEachMovingEnemyEntry(fn); }
	/// <summary>カメラ相対（ScreenHover/Static）敵を movingEnemies_ へ登録する（spline=null/speed=0固定）</summary>
	/// <param name="entity">敵</param>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	void RegisterStationaryMovingEnemy(IImGuiEditable* entity, int waveEntryIndex) override { AddStationaryMovingEnemy(entity, waveEntryIndex); }
	/// <summary>対応する movingEnemies_ エントリへコントローラを紐付ける</summary>
	/// <param name="entity">敵</param>
	/// <param name="ctrl">その敵のコントローラ</param>
	void LinkEnemyController(IImGuiEditable* entity, EnemyController* ctrl) override { AttachControllerToMovingEnemy(entity, ctrl); }
	/// <summary>
	/// waveEntryIndex_ が一致するコントローラへ退避を指示する。
	/// EnemyController の完全型が必要なため定義は .cpp（Enemy/EnemyController.h を include 済み）
	/// </summary>
	/// <param name="waveEntryIndex">Wave のエントリ番号</param>
	void TriggerRetreatForWaveEntry(int waveEntryIndex) override;
	/// <summary>動的スプラインを名前で検索する（GameScene::FindDynamicSplineByName の委譲）</summary>
	/// <param name="name">スプラインの名前</param>
	/// <returns>見つかったスプライン。無ければ nullptr</returns>
	SplineCurveActor* FindDynamicSplineByName(const std::string& name) override {
		return GameScene::FindDynamicSplineByName(name);
	}
	/// <summary>動的スプラインの一覧（Wave Editor のスプライン選択コンボ用）</summary>
	/// <returns>スプラインの一覧（読むだけ）</returns>
	const std::vector<std::unique_ptr<SplineCurveActor>>& GetDynamicSplines() const override {
		return GetDynamicSplineList();
	}
	/// <summary>
	/// レールカメラの走行スプラインをシーンから取得する。CameraPathSpline タグのものが
	/// 無ければ defaultPoints を種にして新規作成する（シーン JSON に載り、エンジン内エディタ / Blender から編集できる）。
	/// 所有はシーン側、戻り値は参照のみ
	/// </summary>
	/// <param name="defaultPoints">無い時に作るスプラインの制御点</param>
	/// <returns>走行スプライン</returns>
	SplineCurveActor* EnsureCameraPathSpline(const std::vector<Vector3>& defaultPoints) override;
	/// <summary>Seek() 用：既存 STG エンティティ（弾/近接判定/敵/コントローラ）を一括破棄する</summary>
	void ClearWaveRuntimeState() override;
	/// <summary>谷の障害物が崩れた部品・落とし物・ボスの手足を消す（GameScene::DestroyDynamicEntity の委譲。IBossStageHost も満たす）</summary>
	/// <param name="entity">消す実体</param>
	void DestroyEntity(IImGuiEditable* entity) override { DestroyDynamicEntity(entity); }

	// ----- IBossStageHost 実装（RegisterEnemyController/UpdateEnemyControllers/SweepDeadEntities/
	//        GetPlayer は IRailStageHost と同一シグネチャのため上の override 1つで両方を満たす）-----
	/// <summary>プレハブをスプラインなしで指定座標にスポーンする（ボス本体・手足・落とし物に流用）</summary>
	/// <param name="prefabName">プレハブ名</param>
	/// <param name="pos">置く位置（ワールド）</param>
	/// <returns>出した実体。出せなければ nullptr</returns>
	IImGuiEditable* SpawnPrefabAt(const std::string& prefabName, const Vector3& pos) override {
		return GameScene::SpawnEnemyAt(prefabName, pos);
	}
	/// <summary>Enemy/Boss/Terrain タグの動的エンティティ＋敵コントローラを一括破棄（Reset/シーン再入用。定義は .cpp）</summary>
	void ClearBossRuntimeState() override;

	// ----- IStageStartHost 実装（private override。StageStartPart 経由のみ）-----
	/// <summary>ゲームの自機（player_）を隠す。演出の間は演出専用の機体（新モデル）を描くので隠しておく</summary>
	/// <param name="hidden">隠すなら true</param>
	void SetPlayerHidden(bool hidden) override;
	/// <summary>スタート演出を終えてレールへ渡す：フォグ・空・平行光源・IBL・カメラのクリップ面・自機の表示をステージの値に戻す</summary>
	void RestoreStageEnvironment() override;
	/// <summary>
	/// 雲の上の場面（S4、F6）を始める：RestoreStageEnvironment の後に呼ぶ。レールを t=0 から進めながら、
	/// ゲームの自機を右上前方から見るカメラで回り込んでレールのカメラへ合流させ、羽根を展開する（CloudTopIntro / PlayerWingUnit）
	/// </summary>
	void BeginCloudTop() override;

	// ----- IStageStreamingHost 実装（private override。StageStreaming 経由のみ）-----
	/// <summary>取っておいた group の配置を今すぐ作る（GameScene::LoadStreamGroup の委譲）</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>作った配置の数</returns>
	size_t CreateStreamGroup(const std::string& group) override { return LoadStreamGroup(group); }
	/// <summary>まだ作っていない group の配置の数（GameScene::GetPendingStreamCount の委譲）</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>まだ作っていない配置の数</returns>
	size_t GetPendingStreamEntityCount(const std::string& group) const override { return GetPendingStreamCount(group); }
	/// <summary>作った group の配置を消して「まだ作っていない」へ戻す（GameScene::UnloadStreamGroup の委譲）</summary>
	/// <param name="group">streamGroup 名</param>
	/// <returns>消した配置の数</returns>
	size_t DestroyStreamGroup(const std::string& group) override { return UnloadStreamGroup(group); }
	/// <summary>プレハブのモデルを読んでおく（GameScene::PreloadPrefabModels の委譲）</summary>
	/// <param name="prefabs">モデルを読んでおくプレハブ名の一覧</param>
	void WarmPrefabModels(const std::vector<std::string>& prefabs) override { PreloadPrefabModels(prefabs); }

	// 区間ごとのストリーミングの時刻表（11 番 L9）。streamGroup 付きの配置を雲の中で裏で読み、決めた秒に作る
	std::unique_ptr<StageStreaming> streaming_;

	// スタート演出（③IN〜雲の上。16_StageStart.md）。StagePlayScene は IStageStartHost 経由でのみこれを操作する。
	std::unique_ptr<StageStartPart> stageStart_;
	/// <summary>スタート演出を進める（演出中はレール・HUD は動かさない）</summary>
	/// <returns>演出中は true（終わってレールへ渡したフレームは false）</returns>
	bool UpdateStageStart();
	/// <summary>スタート演出の場面（縦穴・カタパルト・表示板・機体）を描く</summary>
	void DrawStageStart();
	// 雲の上の場面（S4、16_StageStart.md F6）：自機の右上前方からレールのカメラへ回り込むカメラ（この間は操作を止める）と、自機の羽根
	std::unique_ptr<CloudTopIntro> cloudTopIntro_;
	std::unique_ptr<PlayerWingUnit> playerWings_;
	// 自機のスラスター 4 基（カタパルトと同じ部品・同じポイント制。16_StageStart.md §2.7）。
	// 値で持つのは Initialize の前に LoadTuningFromJson が読むため。playerMoveInput_ はそのフレームの移動の入力（-1..1）
	MechThrusters playerThrusters_{ MechThrusters::Preset::Player };
	Vector2 playerMoveInput_{ 0.0f, 0.0f };
	/// <summary>雲の上の回り込み（CloudTopIntro）の途中か（この間は操作を止める）</summary>
	/// <returns>途中なら true</returns>
	bool IsCloudTopIntroActive() const;
	/// <summary>闘技場の水（足場の床・海）を動かして描くか。谷の出口から海が見えるので、ボス戦の前から</summary>
	/// <returns>着陸・ボス戦の間か、レールで谷の出口に近づいていれば true</returns>
	bool IsBossArenaVisible() const;

	// ----- IPlayerDeathHost 実装（private override。PlayerDeathPart 経由のみ）-----
	/// <summary>羽根（PlayerWingUnit）を開いた形のまま seconds 秒で焼け落とす（シューティング）</summary>
	/// <param name="seconds">焼け落ちるまで [秒]</param>
	void BurnPlayerWings(float seconds) override;
	/// <summary>羽根の関節のワールド位置（焼け落ちの粒子を出す所）を集める。羽根が見えていなければ空</summary>
	/// <param name="out">位置を追加する先</param>
	void CollectWingBurnPoints(std::vector<Vector3>& out) override;
	/// <summary>死亡演出を終えた：ゲームオーバー画面へ</summary>
	void FinishPlayerDeath() override;
	/// <summary>自機をいまのフェーズの姿勢（クリップ）へ戻す（プレビューの終わり・途中で打ち切ったとき。ボス戦の膝つきの後始末）</summary>
	void RestorePlayerPose() override;
	// 自機の死亡演出（19_GameOverAndDifficulty.md §1）。HP0 → 演出 → GAMEOVER。StagePlayScene は IPlayerDeathHost 経由でのみ操作される
	std::unique_ptr<PlayerDeathPart> playerDeath_;
	/// <summary>死亡演出の間か（操作を受け付けない・被弾しない・射撃しない）</summary>
	/// <returns>演出中なら true</returns>
	bool IsPlayerDying() const { return playerDeath_ && playerDeath_->IsActive(); }
	/// <summary>死亡演出を始める（被弾・回避の点滅を止め、レールなら羽根も焼く種類で）</summary>
	/// <param name="preview">true＝調整用。終わってもゲームオーバーへ行かない</param>
	void BeginPlayerDeath(bool preview);

	// ----- IFlightUnitHost 実装（private override。FlightUnitPart 経由のみ。GetPlayer/GetTimeScale/SetTimeScale は上の override で満たす）-----
	/// <summary>自機の体の中心（ファンネル・バリアの基準）を返す</summary>
	/// <returns>ワールド座標</returns>
	Vector3 GetFlightUnitAnchor() const override { return SpecialPlayerCenter(); }
	/// <summary>レティクルが重なっている敵を返す</summary>
	/// <returns>ロックオン中の生きている敵。無ければ nullptr</returns>
	IImGuiEditable* GetReticleTarget() const override { return IsSpecialTargetAlive(lockedEnemy_) ? lockedEnemy_ : nullptr; }
	/// <summary>レティクルの狙いの点を返す</summary>
	/// <returns>弾の発射先（ワールド座標）</returns>
	Vector3 GetReticleAimPoint() const override { return firingTarget_; }
	/// <summary>画面に映っている敵（Enemy / Boss。敵弾は含めない）を集める</summary>
	/// <param name="out">敵を追加する先</param>
	void CollectOnScreenEnemies(std::vector<IImGuiEditable*>& out) override;
	/// <summary>敵がまだ生きているかを返す（IFlightUnitHost / ISpecialMoveHost を満たす）</summary>
	/// <param name="e">調べる敵</param>
	/// <returns>生きていれば true</returns>
	bool IsEnemyAlive(IImGuiEditable* e) const override { return IsSpecialTargetAlive(e); }
	/// <summary>自機の通常弾と同じ弾を撃つ</summary>
	/// <param name="from">撃ち出す位置（ワールド）</param>
	/// <param name="dir">撃つ向き（ワールド）</param>
	/// <param name="homingTarget">強ホーミングで追う敵。nullptr ならまっすぐ飛ぶ</param>
	/// <param name="damageScale">自機の攻撃力に掛ける倍率</param>
	void FireSupportBullet(const Vector3& from, const Vector3& dir, IImGuiEditable* homingTarget, float damageScale) override;
	/// <summary>球の中の敵弾を消し、そのダメージの合計を返す（IFlightUnitHost / ISpecialMoveHost を満たす）</summary>
	/// <param name="center">球の中心（ワールド）</param>
	/// <param name="radius">球の半径 [m]</param>
	/// <returns>消した敵弾のダメージの合計（難易度の倍率の前）</returns>
	int AbsorbEnemyBulletsInSphere(const Vector3& center, float radius) override;
	/// <summary>ファンネル大のビームを撃ち始める</summary>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド）</param>
	/// <param name="damageScale">自機の攻撃力に掛ける倍率（ビームの 1 回のダメージは更に damageScale を掛ける）</param>
	/// <returns>ビームの番号（0 なら撃てなかった）</returns>
	uint32_t FireSupportBeam(const Vector3& from, const Vector3& dir, float damageScale) override;
	/// <summary>ファンネル大のビームの根元と向きを付け直す</summary>
	/// <param name="id">FireSupportBeam が返した番号</param>
	/// <param name="from">根元（ワールド）</param>
	/// <param name="dir">向き（ワールド）</param>
	void SetSupportBeamPose(uint32_t id, const Vector3& from, const Vector3& dir) override;
	/// <summary>ファンネル大のビームが撃っている途中かを返す</summary>
	/// <param name="id">FireSupportBeam が返した番号</param>
	/// <returns>途中なら true</returns>
	bool IsSupportBeamActive(uint32_t id) const override;

	// ----- IPlayerBeamHost 実装（private override。PlayerBeamPart 経由のみ）-----
	/// <summary>太さのある線分に重なっている敵を集める（GameScene::CollectEnemiesOnSegment）</summary>
	/// <param name="from">線分の始点（ワールド）</param>
	/// <param name="to">線分の終点（ワールド）</param>
	/// <param name="radius">線分の太さ（半径）[m]</param>
	/// <param name="out">当たった敵を追加する先</param>
	void CollectBeamHits(const Vector3& from, const Vector3& to, float radius, std::vector<PlayerBeamHit>& out) override {
		CollectEnemiesOnSegment(from, to, radius, out);
	}
	/// <summary>ビームが敵に当たった（GameScene::ApplyPlayerBeamHit）</summary>
	/// <param name="hit">当たった敵</param>
	/// <param name="damage">入れるダメージ</param>
	/// <param name="hitEffect">当たった所に出すエフェクト名</param>
	/// <param name="firstHit">このビームがこの敵に初めて当たったか</param>
	void ApplyBeamHit(const PlayerBeamHit& hit, int damage, const std::string& hitEffect, bool firstHit) override {
		ApplyPlayerBeamHit(hit, damage, hitEffect, firstHit);
	}
	// 自機のビーム（チャージ射撃・ファンネル大）。撃っている間はレティクルへ付いていき、線分の上の敵へ間隔ごとにダメージ
	std::unique_ptr<PlayerBeamPart> playerBeam_;
	// 撃っているチャージビーム（0 なら撃っていない）
	PlayerBeamPart::BeamId chargeBeamId_ = PlayerBeamPart::kInvalidBeam;
	/// <summary>自機のビームを進める（チャージビームの根元と向きをレティクルへ付け直してから）</summary>
	/// <param name="dt">自機の弾と同じ時間 [秒]</param>
	void UpdatePlayerBeams(float dt);
	/// <summary>自機のビームを全部止める（フェーズの切り替え・死亡演出・シーンの終わり）</summary>
	void StopAllPlayerBeams();
	// 飛行ユニット（サブ武器。20_Loadout.md §3）。付けている系統は LoadoutState。シューティングとボス戦の両方で RB の技を使う
	std::unique_ptr<FlightUnitPart> flightUnit_;
	// 拾ったアイテムとエーテルを右下に出す（22_ItemDrop.md。落とす・拾うのは RailStagePart の LootDrops）
	LootPickupLog lootLog_;
	/// <summary>飛行ユニットの毎フレーム（RB の入力・技・クールタイム。時間を握る演出・World 停止の間は止める）</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdateFlightUnit(class InputActionMap* actions);
	/// <summary>自機の攻撃力（メイン武器の加算・攻撃系の補正込み）。射撃・近接・支援射撃で使う</summary>
	/// <returns>攻撃力</returns>
	int ScaledPlayerAttack() const;
	/// <summary>右手のモデルを付けているメイン武器（LoadoutState）で作り直す。武器にモデルが無ければプレハブの weapon</summary>
	void RebuildMainWeaponModel();
	/// <summary>左手のモデルを付けている盾（LoadoutState）で作り直す。盾にモデルが無ければ描かない</summary>
	void RebuildShieldModel();
	/// <summary>付けている盾の移動速度の倍率</summary>
	/// <returns>倍率（盾なしは 1）</returns>
	float ShieldMoveSpeedScale() const;
	/// <summary>付けている盾がビームシールドで、いま描くか</summary>
	/// <returns>描くなら true</returns>
	bool IsShieldBeamVisible() const;
	/// <summary>メイン武器のビームの刃をいま描くか（近接の間だけ伸びる）</summary>
	/// <returns>描くなら true</returns>
	bool IsWeaponBladeVisible() const;
	/// <summary>STG の自機を照準へ向ける（上の基準はカメラの上。体の中心まわりのずらしは位置を作るときに掛ける）</summary>
	/// <param name="toAim">自機から狙う点への向き（ワールド）</param>
	void ApplyRailAimRotation(const Vector3& toAim);
	/// <summary>自機の判定の姿勢（中心＝体の中心・軸＝自機の回転。STG の移動状態は前傾）を Gameplay に入れる。敵弾・接触・デバッグ描画が同じものを使う</summary>
	void UpdatePlayerColliderPose();

	/// <summary>自機（新モデル PlayerModel）の姿勢：レールは射出口のけり出し（Launch）の最後のコマ＝飛ぶ姿勢、ボス戦（着陸で足場に着いた後も）は Stand</summary>
	void ApplyPlayerPose();

	// STG（Boss）専用ロジック一式（ボス生成・ボスAI・地上移動・ロックオン追従カメラ）。
	// StagePlayScene は IBossStageHost 経由でのみこれを操作する。
	std::unique_ptr<BossStagePart> bossStage_;
	// ボス撃破からリザルトまでの段取り（ヒットストップ→スロー→寄るカメラ→崩壊→STAGE CLEAR。23_StageClear.md §4 B）
	BossClearSequence clearSeq_;
	/// <summary>撃破の瞬間の 1 枚が撮れたら、名前を付けて登録し直してリザルトへ渡す（StageClearRecord::SetCapture）</summary>
	void UpdateStageClearCapture();
	// ボス戦の床（石畳＋薄い水）。Boss フェーズ中だけ更新・描画する
	std::unique_ptr<BossArenaWater> bossWater_;
	// ボスの必殺技 GravityOrb の岩と演出（攻撃のステートが GetBossGravityOrb で受け取って始める）
	std::unique_ptr<BossGravityOrb> gravityOrb_;
	// GravityOrb で空を暗くしているか（Skybox の着色の切り替えを 1 回だけ出す）
	bool gravityOrbSkyDark_ = false;
	// IBossGravityOrbHost
	IImGuiEditable* GetGravityOrbPlayer() const override;
	float GetGravityOrbGroundY() const override;
	void OnGravityOrbPlayerHit(float damageRatio) override;
	void ShakeGravityOrbCamera(float intensity, float duration) override;
	void SetGravityOrbSkyDark(bool dark) override;
	// 低空飛行の大河の水面。PSO はシーン初期化で作り、川のデータは雲の中で読む（13_RiverWater.md）
	std::unique_ptr<RiverWater> riverWater_;

	// プレイヤー：シーンの動的な一覧（GetDynamicAnimated）が所有、ここは参照用ポインタ
	// カメラのローカル空間で playerLocalOffset_ の位置に毎フレ配置する
	AnimatedObject3DInstance* player_ = nullptr;

	// 武器（ソケット追従）：プレイヤーの手ボーンに毎フレ追従させる。
	// シーンの動的な一覧には入れず、順序制御のため明示的に Update/Draw する。
	std::unique_ptr<Object3DInstance> weapon_;
	// 武器のランタイム追従ヘルパ。追従先ボーン/オフセット/enabled は毎フレ
	// player の WeaponParams コンポーネント（Inspector 編集・プレハブ保存）から同期する。
	// メイン武器（MainWeapon.json）がモデルを持つときは、オフセットはその武器の grip を使う。
	BoneSocket weaponSocket_;
	// 銃口のワールド座標（UpdatePlayerAttachments で武器の行列から求める。武器のモデルが無ければ hasWeaponMuzzle_ = false）
	Vector3 weaponMuzzle_{ 0.0f, 0.0f, 0.0f };
	bool hasWeaponMuzzle_ = false;
	/// <summary>弾・チャージビームを撃ち出す所（銃口。武器のモデルが無ければ自機の位置）</summary>
	/// <returns>ワールド座標</returns>
	Vector3 PlayerShotOrigin() const;
	// 盾（左手のソケット追従、20 番 §2）。構成は付けている盾（Shield.json）の model / grip
	std::unique_ptr<Object3DInstance> shield_;
	BoneSocket shieldSocket_;
	// ビームシールドの本体（発生装置のモデルは shield_。ビームの盾を初めて付けたときに作る）
	std::unique_ptr<BeamShieldRenderer> beamShield_;
	// メイン武器のビームの刃（20 番 §1.2。刃のある武器を初めて付けたときに作る）。近接の間だけ伸びる
	std::unique_ptr<BeamBladeRenderer> weaponBlade_;
	bool weaponBladePreview_ = false; // Debug：近接でなくても出して調整する

	// シーン停止中（エディタ Pause = SceneTimeScale==0）だけレティクル照準追従を止め、
	// この向きで固定する。武器ソケットの確認・編集をしやすくする。再生中は通常照準。
	// 位置（クリップ追従）には影響しない。
	Vector3 fixedPlayerFacing_{ 0.0f, 0.0f, 0.0f }; // 停止中の固定向き（rad, +Z正面）

	// ----- ImGui で編集可能な調整値（Resources/Json/Tuning/StagePlay.json に自動同期） -----
	Vector3 playerLocalOffset_{ 0.0f, -0.5f, 6.0f };  // カメラローカルの中心位置（無入力時）
	// 攻撃中（最後の射撃/近接から moveStateDelay_ 秒以内）の移動速度倍率。STG・ボス戦共通。撃ちながらだと避けにくくする。
	// それ以外は「移動状態」＝全速・STG は当たり判定のカプセルをモデルと同じ角度に前へ傾ける。チャージだけなら移動状態のまま
	float   attackMoveSpeedScale_ = 0.6f;
	float   moveStateDelay_ = 1.0f;          // 最後の攻撃から移動状態になるまで [sec]
	float   moveStateLeanDeg_ = 55.0f;       // 移動状態の当たり判定の前傾 [deg]（飛ぶ姿勢のモデルの体の角度に合わせる）
	float   timeSinceAttack_ = 1000.0f;      // 最後の攻撃からの経過（Player の時間）
	/// <summary>攻撃中か（最後に撃つ／近接を出してから moveStateDelay_ 秒以内と、近接の発生〜後隙中。チャージだけなら数えない）</summary>
	/// <param name="actions">アクションの入力</param>
	/// <returns>攻撃中なら true（移動速度を attackMoveSpeedScale_ 倍にする）</returns>
	bool    IsPlayerAttacking(class InputActionMap* actions) const;
	/// <summary>攻撃した印を付ける（最後の攻撃からの経過を 0 に戻す）</summary>
	void    MarkPlayerAttack() { timeSinceAttack_ = 0.0f; }
	int     playerBaseMaxHP_ = -1;           // 飛行ユニットの HP 上限の補正を掛ける前（プレハブの値。-1＝未取得）
	Vector2 playerMoveSpeed_{ 5.0f, 5.0f };            // 入力1秒あたりのオフセット移動量（カメラ空間X/Y）
	Vector2 playerClipMargin_{ 0.1f, 0.1f };           // クリップ空間で許す画面外マージン（X/Y）
	float   playerSmoothTime_ = 0.15f;                 // 慣性の指数減衰時定数（秒）：小さい=反応速い

	// ----- 射撃チューニング -----
	// 弾パラメータは弾プレハブの BulletParams、連射間隔はプレイヤープレハブの ChargeParams.fireRate に移行済み。
	float fireTimer_      = 0.0f;    // 次に撃てるまでの残り秒（ランタイム）

	// ----- 照準（aim）チューニング -----
	// カメラからの「狙いの面」までの距離。弾速・寿命とは独立。
	// 非ロックオン時、レティクル方向のレイがこの面と交わる点を target にする。
	// 値を増やすほど画面外の敵にもレティクルが効く（弾は当然そこまで届くとは限らない）。
	float aimPlaneDistance_ = 80.0f;
	float aimSmoothTime_   = 0.08f;   // Lerp 時定数（プレイヤー回転用、0.0=即時）
	float aimAssistPixelScale_ = 1.4f; // 見かけ半径×倍率＝スクリーン上のロックオン許容ピクセル
	float aimReticleOverlapRate_ = 0.5f; // レティクル半径×率をロックオン許容ピクセルに加算（輪が敵に掛かればロック）
	// ----- カメラ idle sway（Rail 中、止まって見えないよう微小に揺らす）-----
	bool    idleSwayEnabled_ = true;
	Vector3 idleSwayRotAmp_ = { 0.0052f, 0.0070f, 0.0122f }; // pitch/yaw/roll 振幅 [rad]（≒0.3°/0.4°/0.7°）
	float   idleSwayPosAmp_ = 0.12f;                          // カメラ上方向の上下振幅 [m]
	float   idleSwaySpeed_ = 1.0f;                            // 揺れの速さ倍率
	float   idleSwayTime_ = 0.0f;
	/// <summary>Rail 中のカメラを、軸ごとに周期と位相をずらした sin で微小に揺らす（World 停止中は揺らさない）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void ApplyCameraIdleSway(float dt);
	float aimConvergePx_ = 160.0f;        // 非ロック時、画面上この距離内の敵がいれば弾の収束点をその敵の深さに合わせる（視差で外れるのを防ぐ）
	// レティクル外側パーツの中心からのオフセット範囲（pixel）
	float reticleOuterMinPx_ = 32.0f;
	float reticleOuterMaxPx_ = 128.0f;
	// レティクル外側パーツのスプライトサイズ範囲（pixel）
	float reticleOuterSizeMinPx_ = 32.0f;
	float reticleOuterSizeMaxPx_ = 64.0f;
	// ランタイム状態
	Vector3 aimTarget_{ 0.0f, 0.0f, 0.0f };       // Lerp 済み（プレイヤー回転用）
	Vector3 firingTarget_{ 0.0f, 0.0f, 0.0f };    // 即時（弾の発射方向用）
	IImGuiEditable* lockedEnemy_ = nullptr;        // 現フレームのロックオン対象（ホーミング元・強）
	IImGuiEditable* nearestEnemy_ = nullptr;       // 画面上で最近の敵（軽ホーミング先）
	bool    aimInitialized_ = false;

	// 入力で加算するオフセット / 慣性用速度（ランタイムのみ、JSON 非保存）
	Vector2 playerInputOffset_{ 0.0f, 0.0f };
	Vector2 playerVelocity_{ 0.0f, 0.0f };

	// 移動阻止(壁押し戻し)の前フレーム状態。立ち上がりエッジで MOVE_BLOCKED を出すため軸ごとに保持。
	bool prevMoveBlockedX_ = false;
	bool prevMoveBlockedY_ = false;

	// state.log 用のフレーム番号（このシーン開始からの経過フレーム。SUNDAY のハング/スタック判定の基準）。
	uint64_t stateFrame_ = 0;

	// レティクル
	std::unique_ptr<Reticle> reticle_;

	// ----- 精密射撃モード（PrecisionAim：LT / 右クリック ホールド）-----
	// 射撃間隔・チャージ・マルチロックは通常と同一仕様（ここでは何も変えない）。
	// FOV ズーム + エイム感度低下 + 周辺ぼかし/減光（PrecisionBlurEffect）で精密射撃を演出する。
	float precisionFovY_ = 0.30f;              // ズーム後の FovY（rad）。base より小さい＝寄る
	// プレイヤー位置を基準にしたカメラのローカルオフセット（右肩越し）。R(+X)/U(+Y)/F(+Z=前方)
	Vector3 precisionCamOffset_{ 0.6f, 0.4f, -2.0f };
	float precisionFadeSpeed_ = 6.0f;          // モード ON/OFF 補間速度（/秒）
	float precisionStickScale_ = 0.4f;         // モード中のレティクル感度倍率（右スティック）
	float precisionVignette_ = 0.5f;           // 周辺減光の最大強度
	float precisionBlurIntensity_ = 1.0f;      // 周辺ぼかしの最大効き
	float precisionBlurInnerRadius_ = 0.30f;   // くっきり範囲（中央からの正規化距離）
	float precisionBlurFalloff_ = 0.40f;       // くっきり→最大ぼかしへの遷移幅
	float precisionBlurMaxPx_ = 10.0f;         // 周辺の最大ぼかし量（pixel）
	// ランタイム
	float precisionBlend_ = 0.0f;              // 0=通常, 1=精密モード全開（補間値）
	float baseFovY_ = 0.45f;                   // 通常時 FovY（Initialize でカメラから取得）

	// ----- クリップ面（JSON "camera.nearClip" / "camera.farClip"）-----
	// Camera の既定は near=0.1 / far=100 で、SetFarClip はプロジェクト内で一度も呼ばれていなかった
	// ＝100m より遠くが一切描画されていなかった。遠景・フォグ・広いレールには全く足りないので
	// StagePlay 側で明示的に設定する。near を上げているのは far を伸ばした分の深度精度確保のため。
	float cameraNearClip_ = 0.5f;
	float cameraFarClip_  = 5000.0f;
	float reticleBaseStickSpeed_ = 1200.0f;    // 通常時レティクル感度（Initialize で取得）

	/// <summary>精密射撃モード（LT / 右クリックのホールド）の補間値を進め、FOV・レティクル感度・周辺ぼかし/減光に反映する</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdatePrecisionAim(class InputActionMap* actions, float dt);
	/// <summary>
	/// プレイヤーのワールド位置を基準に、表示カメラを肩越し位置へ寄せる（向きは維持）。
	/// プレイヤー配置（基準カメラ）後に呼ぶ。precisionBlend_ で補間。
	/// </summary>
	/// <param name="playerWorldPos">自機のワールド位置</param>
	void ApplyPrecisionCamera(const Vector3& playerWorldPos);

	// ----- チャージシステム -----
	float playerChargeLevel_ = -1.0f;       // -1=チャージなし, 0.0=未達成, 1=stage1達成, 2=stage2達成
	float chargeStage1Time_ = 3.0f;         // 1段階目完了までの秒数
	float chargeStage2Time_ = 6.0f;         // 2段階目完了までの秒数（合計時間）
	float chargeTimer_ = 0.0f;              // 現在のチャージ経過時間
	EffectHandle chargeStartEffectHandle_ = kInvalidEffectHandle; // charge_start エフェクト
	EffectHandle chargeHoldEffectHandle_ = kInvalidEffectHandle;   // charge_hold エフェクト（ループ）
	bool chargeStage1Triggered_ = false;    // 1段階目発動済みフラグ
	bool chargeStage2Triggered_ = false;    // 2段階目発動済みフラグ
	// チャージアニメーション用レティクル設定
	float outerChargeStartRadius_ = 150.0f;
	float outerChargeEndRadius_ = 60.0f;
	float outerChargeEasingDuration_ = 0.3f;
	float outerRotationSpeed_ = 360.0f;

	// ----- 近接攻撃（コンボ）-----
	// 弱攻撃は4段コンボ（連打で段送り）、強攻撃は単発（弱コンボをリセット）。
	// 各段の発生/持続/後隙・倍率・受付猶予は段プレハブの MeleeParams から読む。
	// 攻撃の流れ：入力 →(startup)→ 判定発生(activeDuration) →(recovery 後隙)→ 行動可。
	int   meleeComboIndex_       = 0;     // 0=コンボ無し, 1..4=現在の弱攻撃段
	float meleeComboTimer_       = 0.0f;  // コンボ継続の残り [sec]（0で段リセット）
	float meleeStartupTimer_     = 0.0f;  // 発生までの残り [sec]（0で判定 spawn）
	float meleeActionLockTimer_  = 0.0f;  // >0 の間、射撃/回避/近接を全てロック（発生＋持続＋後隙）
	bool  meleePending_          = false; // 発生待ちの攻撃があるか
	std::string meleePendingPrefab_;      // 発生時に spawn する近接プレハブ
	static constexpr int kMeleeWeakComboMax_ = 4;
	// 近接判定の生成をプレイヤー原点（足元）から world-up にこの高さ持ち上げる（体の中心へ）。
	// 足元原点のままだと地上戦で攻撃が地面にめり込むため。StagePlay.json "melee" に保存。
	float meleeOriginHeight_ = 1.0f;
	// 近接判定＋見た目のサイズ倍率。STG は当てづらいのでプレハブ基準(=1.0)、ボス戦は当てやすすぎるので縮小。
	// SpawnPendingMelee が phase_ で使い分ける。StagePlay.json "melee" に保存。
	float meleeBossSizeScale_ = 0.5f;

	/// <summary>近接の入力（弱は 4 段コンボ・強は単発）を受け、発生・コンボ受付・行動ロックのタイマーを進める</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateMeleeCombo(class InputActionMap* actions, float dt);
	/// <summary>発生待ちの近接判定を実際に spawn する（足元から meleeOriginHeight_ 持ち上げ、ボス戦は小さく）</summary>
	void SpawnPendingMelee();
	/// <summary>aim 基底（前=自機→firingTarget、右=worldUp×前、上=前×右）を求める。近接派生中は対象敵を直接向く</summary>
	/// <param name="right">右の向き</param>
	/// <param name="up">上の向き</param>
	/// <param name="forward">前の向き</param>
	void ComputeAimBasis(Vector3& right, Vector3& up, Vector3& forward) const;

public:
	/// <summary>
	/// 近接攻撃の発生〜後隙中・回避の行動ロック・分身の選択中・必殺技中・雲の上の回り込み中で、
	/// 他の行動（射撃/回避/近接）が禁止されているか。
	/// </summary>
	/// <returns>禁止されていれば true</returns>
	bool IsActionLocked() const { return meleeActionLockTimer_ > 0.0f || dodgeActionLockTimer_ > 0.0f || jdSelecting_ || specialActive_ || IsCloudTopIntroActive(); }
private:

	// ----- ジャスト回避演出 -----
	bool  justDodgeActive_ = false;
	float justDodgeTimer_ = 0.0f;
	float justDodgeDuration_ = 1.5f;
	float justDodgeFadeIn_ = 0.15f;
	float justDodgeFadeOut_ = 0.3f;
	IImGuiEditable* justDodgeTarget_ = nullptr;

	// ----- 回避 / ジャスト回避メカニクス -----
	// 無敵の点滅色は「無敵の発生源」で出し分ける（被弾=赤/白・回避=水色・ジャスト=点滅なし）。
	bool  wasInvincible_ = false;        // 前フレーム無敵だったか（無敵終了時に通常色へ戻す用）

	// 通常回避（ダッシュ＋無敵窓）。タイマー類は実時間（UI グループ dt）で進める。
	bool  dodgeActive_ = false;          // 回避無敵が有効か
	float dodgeTimer_ = 0.0f;            // 回避開始からの経過 [sec]
	float dodgeJustWindow_ = 0.15f;      // ジャスト成立窓：入力後この秒数以内に被弾接触すると成立（StagePlay.json の dodge.justWindow。機動系の延長は判定の所で足す）
	float dodgeIFrameDuration_ = 0.4f;   // 回避無敵：入力後この秒数まで被弾無効（justWindow を内包）
	float dodgeCooldown_ = 0.6f;         // 回避クールダウン [sec]
	float dodgeCooldownTimer_ = 0.0f;    // クールダウン残り
	float dodgeActionLock_ = 0.25f;      // 回避中の行動ロック秒（IsActionLocked に合流）
	float dodgeActionLockTimer_ = 0.0f;  // 行動ロック残り
	Vector2 dodgeImpulse_{ 22.0f, 18.0f }; // ダッシュ初速（画面平面 X/Y, units/sec、playerVelocity_ に加算）

	// ジャスト回避スロー（受付期限モデル：将来の分身カウンターで延長/短縮できる構造）
	float justDodgeSlowWorld_ = 0.3f;       // 成立中の World 時間倍率（Player/UI は等速）
	float justDodgeReceiptWindow_ = 3.0f;   // 追加入力の受付期間（=基本スロー長）[sec]
	bool  justDodgeCounterActive_ = false;  // 追加入力アクション進行中フック（true の間は受付終了を保留）
	float justDodgeFadeOutTimer_ = -1.0f;   // フェードアウト経過（-1=未フェード）

	// 回復
	// デフォルトHeal(R/X) = 大回復・固定回数制（STG/ボスごとに上限）。
	// 左派生（分身カウンター）= 小回復・回数無制限（ジャスト回避できる限り何度でも）。
	int   healUsedStg_     = 0;  // STG で使った回復回数（デフォルトHealのみカウント）
	int   healUsedBoss_    = 0;  // ボスで使った回復回数（同上）
	int   healMaxStg_      = 2;  // STG 回復上限（デフォルトHeal用、小さめ）
	int   healMaxBoss_     = 2;  // ボス 回復上限
	int   healAmount_      = 20; // デフォルトHealの回復量（大）
	int   healSmallAmount_ = 5;  // 左派生の小回復量

	// 必殺技の種別（装備制：Storm Lock or Shatterline の2択、片方のみ持込）。
	// 本来は発動ボタン共通（R3/F=Ultimate）だが、デバッグでは別キーで両方試せるよう分離する。
	enum class SpecialKind { StormLock, Shatterline };
	SpecialKind equippedSpecial_ = SpecialKind::StormLock; // 現在装備中の必殺技

	// 必殺技ゲージ（UI 未実装。ゲージ本体だけ先に持つ）
	float specialGauge_         = 0.0f;   // 現在値（0..specialGaugeMax_）
	float specialGaugeMax_      = 100.0f; // 装備中種別の実効上限（種別切替でミラー更新）
	float specialGaugeMaxStormLock_   = 100.0f; // Storm Lock のゲージ上限
	float specialGaugeMaxShatterline_ = 120.0f; // Shatterline のゲージ上限
	float dodgeSpecialGaugeGain_ = 8.0f;  // 追加回避1回ぶんの加算量
	// 近接ヒットでの加算。弱コンボ（4段）の方が総量で貯まりやすいよう、1ヒットは弱>強
	float meleeWeakGaugeGain_   = 3.0f;
	float meleeStrongGaugeGain_ = 2.0f;
	float meleeLateGaugeRate_   = 0.5f;  // 持続あて（出だし以外）の倍率
	/// <summary>プレイヤー近接が敵/ボスに当たった直後に、弱・強と本あて・持続あてに応じて必殺技ゲージを足す</summary>
	/// <param name="target">当たった相手</param>
	/// <param name="prefabName">近接のプレハブ名（弱/強の見分けに使う）</param>
	/// <param name="clean">本あて（出てから cleanWindow 以内）なら true</param>
	void OnPlayerMeleeHit(IImGuiEditable* target, const std::string& prefabName, bool clean) override;

	// クールタイム（両必殺技共通）。発動終了でタイマー開始、>0 の間は再発動不可。
	float specialCooldown_      = 30.0f;  // クールタイム長（秒）
	float specialCooldownTimer_ = 0.0f;   // クールタイム残り（秒、ランタイム）

	// 必殺技中の無敵フラグ（後隙では false にして被弾可能にする）。既定 true。
	// 入力ロックは specialActive_ で別途継続するので、後隙=「入力不可だが無敵なし」を実現する。
	bool specialInvincible_ = true;

	// 必殺技中の固定の構え向き（レティクル追従を止めて正面＝+Z で構える）。両必殺技共通。
	Vector3 specialFirmFacing_{ 0.0f, 0.0f, 0.0f };

	// スコア
	int   justDodgeScore_ = 200; // ジャスト回避 1 回の加点（調整可）

	// ----- 分身カウンター（ジャスト回避派生）-----
	// 方向→アクション割当: 上=近接強 / 右=近接弱 / 下=追加回避 / 左=回復（Phase2 で各アクション実装）。
	enum class CounterDir { None, Up, Right, Down, Left };
	bool       jdSelecting_ = false;               // 分身プレビュー表示中（方向入力待ち）
	CounterDir jdChosen_    = CounterDir::None;     // 確定した派生方向（Phase2 で参照）
	IImGuiEditable* jdCounterTarget_ = nullptr;    // カウンター対象敵（ジャストした攻撃の発生元）
	std::vector<IImGuiEditable*> jdClones_;        // 分身ビジュアル（AnimatedObject3D。index=CounterDir-1）
	// 各方向の分身モデルパス（スキニングのモデル。方向ごとに持つ）。2026-10-19 自機と同じ新モデルに
	std::string jdClonePath_[4] = {
		"Resources/Models/Player/PlayerModel/PlayerModel.mesh", // Up
		"Resources/Models/Player/PlayerModel/PlayerModel.mesh", // Right
		"Resources/Models/Player/PlayerModel/PlayerModel.mesh", // Down
		"Resources/Models/Player/PlayerModel/PlayerModel.mesh", // Left
	};
	// 分身の姿勢＝自機と同じ飛ぶ姿勢（Launch の最後のコマ。ApplyPlayerPose と同じ）
	static constexpr const char* kJdClonePoseClip = "Resources/Models/Player/PlayerModel/PlayerModel_Launch.anim";
	float   jdCloneOffset_     = 4.0f;             // 分身の表示オフセット距離（画面平面 units）
	Vector4 jdCloneColor_{ 0.4f, 0.8f, 1.0f, 0.45f }; // 分身の半透明色
	float   jdSpreadTimer_     = 0.0f;            // 分裂アニメ経過（中心→各方向へ広がる）
	float   jdSpreadDuration_  = 0.15f;           // 分裂アニメ時間（秒）
	bool    jdMerging_         = false;           // 集合アニメ中（無選択で受付終了→中心へ戻る）
	float   jdMergeTimer_      = 0.0f;            // 集合アニメ経過
	float   jdMergeDuration_   = 0.2f;            // 集合アニメ時間（秒）
	float   jdSelectThreshold_ = 0.5f;            // (未使用・互換のため温存) 方向確定に必要な入力量

	// カメラ引き（ジャスト回避中。精密カメラより優先）
	float jdCamPullback_     = 8.0f;   // forward 逆方向への後退量（units、引き）
	float jdCamFovAdd_       = 0.06f;  // 引き時に加える FovY（rad、視野を広げる）
	float jdEffectIntensity_ = 0.0f;   // 演出強度(0..1)。UpdateJustDodgeEffect が更新し、カメラ引きブレンドに使う

	// ----- 分身カウンター派生（Phase2）アクション中の状態 -----
	// 派生確定後 justDodgeCounterActive_=true の間は受付期限が延長され、世界スローも継続する。
	enum class JdActionPhase { None, Approach, Active, Return };
	JdActionPhase jdActionPhase_ = JdActionPhase::None;
	Vector2 jdReturnOffset_{ 0.0f, 0.0f }; // テレポート前の playerInputOffset_。復帰時に補間で戻す。
	float   jdActionPhaseTimer_ = 0.0f;    // 現フェーズ経過秒（実時間=UI dt）

	// 近接派生（Up/Right）共通パラメータ
	float jdMeleeApproachDuration_ = 0.18f;   // 詰め寄り補間時間
	float jdMeleeReturnDuration_   = 0.25f;   // 元位置への戻り補間時間
	float jdMeleeApproachDist_     = 4.0f;    // 対象敵から手前にこの距離だけ離した位置を目標にする
	Vector2 jdMeleeApproachStart_{ 0.0f, 0.0f }; // (未使用、互換のため温存)
	Vector2 jdMeleeApproachGoal_{ 0.0f, 0.0f };  // (未使用)

	// 近接派生はワールド座標で補間する（camera-local 平面では z 差が出て melee 判定が届かないため）。
	// Approach: jdApproachWorldStart_ → jdApproachWorldGoal_（敵手前）
	// Active  : jdApproachWorldGoal_ で停滞して melee 発生
	// Return  : jdApproachWorldGoal_ → jdReturnWorldPos_（派生開始時のワールド位置）
	Vector3 jdApproachWorldStart_{ 0.0f, 0.0f, 0.0f };
	Vector3 jdApproachWorldGoal_{ 0.0f, 0.0f, 0.0f };
	Vector3 jdReturnWorldPos_{ 0.0f, 0.0f, 0.0f };

	// 弱近接派生（Right）の自動連撃ステージ（1..kMeleeWeakComboMax_）。強（Up）は常に1。
	int jdAutoComboStage_ = 0;

	// 近接派生中のカメラオフセット（プレイヤーローカル基底：right/up/forward）
	// forward = プレイヤー→対象敵。本値は「プレイヤー位置からのオフセット」。
	// 左斜め後ろ＝右-、上+、前-（後ろ）になるよう既定値を設定。
	Vector3 jdMeleeCameraOffset_{ -3.0f, 2.5f, -5.0f };
	// 注視点オフセット（プレイヤーと敵の中点を基準）
	Vector3 jdMeleeCameraLookOffset_{ 0.0f, 1.0f, 0.0f };
	bool    jdMeleeCameraActive_ = false; // 近接派生中は ApplyJustDodgeMeleeCamera で上書きする

	// ----- ボス戦：近接派生はワープ（瞬間移動）＋カメラは最短経路で回り込む -----
	// プレイヤーは即座にワープするが、カメラは古い位置から新フレーミングへ「最短ヨー」で寄せる
	// （対象を挟んで反対側へ移動するため、素の値だと yaw が反転して不自然になる。wrap 補正で短い方を通す）。
	float jdBossWarpDelay_        = 0.12f;   // トリガーからワープ実行までの待機（STGの詰め寄りダッシュ時間とは別概念）
	float jdBossCamSwingDuration_ = 0.35f;   // ワープ直後、カメラが最終フレーミングへ寄りきるまでの時間
	float jdBossCamSwingTimer_    = 0.0f;
	bool  jdBossMeleeCamActive_   = false;   // ワープ実行後～派生終了まで true（ワープ前の待機中は既存の自由/ロックオンカメラのまま）
	float jdBossCamSwingStartYaw_    = 0.0f; // ワープ実行の瞬間に採取したカメラの実位置（対象中心の球面座標）
	float jdBossCamSwingStartPitch_  = 0.0f;
	float jdBossCamSwingStartRadius_ = 0.0f;

	/// <summary>ワールド座標を対象（ボス）中心の球面座標にする（近接カメラの最短経路回り込みで使用）</summary>
	/// <param name="p">変換する点</param>
	/// <param name="pivot">球の中心（対象の中心）</param>
	/// <param name="outYaw">方位角 [rad]</param>
	/// <param name="outPitch">仰俯角 [rad]</param>
	/// <param name="outRadius">中心からの距離 [m]</param>
	void ToSphericalAroundPivot(const Vector3& p, const Vector3& pivot, float& outYaw, float& outPitch, float& outRadius) const;
	/// <summary>対象（ボス）中心の球面座標をワールド座標にする（ToSphericalAroundPivot の逆）</summary>
	/// <param name="pivot">球の中心（対象の中心）</param>
	/// <param name="yaw">方位角 [rad]</param>
	/// <param name="pitch">仰俯角 [rad]</param>
	/// <param name="radius">中心からの距離 [m]</param>
	/// <returns>ワールド座標</returns>
	Vector3 FromSphericalAroundPivot(const Vector3& pivot, float yaw, float pitch, float radius) const;

	// 追加回避（Down）派生：許容枠を一時拡張＋戻し補間＋射撃禁止
	bool    jdDodgeMarginActive_   = false;    // 拡張クリップ枠が有効か（戻し中も true）
	float   jdDodgeMarginTimer_    = 0.0f;     // 戻し補間タイマー（0..jdDodgeReturnDuration_）
	bool    jdDodgeReturning_      = false;    // 拡張→通常へ補間中
	Vector2 jdDodgeExpandedMargin_{ 0.8f, 0.8f }; // 拡張時のクリップマージン
	float   jdDodgeReturnDuration_ = 0.35f;    // 戻し時間
	Vector2 jdDodgeReturnFromOffset_{ 0.0f, 0.0f }; // 戻し開始時の playerInputOffset_（押し戻し補間用）

	// ボス戦：プレイヤーの向き。通常時は移動入力方向、ジャスト回避中は攻撃元（ボス）方向へ向く。
	// STG はレティクル追従のまま（この値は使わない）。yaw のみ（pitch=0＝地上を走る通常キャラ）。
	float bossFacingYaw_ = 0.0f;            // 現在の向き（rad）。無入力時は保持
	bool  bossFacingInit_ = false;          // 初回サンプリング済みフラグ
	float bossFacingTurnSmoothTime_ = 0.10f; // 旋回の指数減衰時定数（小さいほど機敏）

	/// <summary>ジャスト回避中のカメラ引き（精密カメラの後の eye から forward の逆へ下げ、FovY を広げる）</summary>
	/// <param name="playerWorldPos">自機のワールド位置</param>
	void ApplyJustDodgeCamera(const Vector3& playerWorldPos);
	/// <summary>近接派生中のカメラ（STG は自機の左斜め後ろ、ボス戦はワープ後に最短ヨーで回り込む）で上書きする</summary>
	/// <param name="playerWorldPos">自機のワールド位置</param>
	void ApplyJustDodgeMeleeCamera(const Vector3& playerWorldPos);
	/// <summary>分身カウンター派生のアクション（詰め寄り → 攻撃 → 戻り、追加回避の戻しなど）を進める</summary>
	/// <param name="dt">経過時間 [秒]（実時間）</param>
	void UpdateJustDodgeCounterAction(float dt);
	/// <summary>分身カウンター派生のアクションを終える（止めていたワールドの時間を元のスローへ戻す）</summary>
	void EndJustDodgeCounterAction();
	/// <summary>4 方向の分身（半透明の自機と同じモデル・飛ぶ姿勢）を出す</summary>
	void SpawnJustDodgeClones();
	/// <summary>分身の分裂・集合のアニメと位置を進め、アクションボタンで方向を確定する</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="moveDelta">移動入力（-1..1。追加回避の方向に使う）</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateJustDodgeClones(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	/// <summary>分身をすべて破棄する</summary>
	void ClearJustDodgeClones();
	/// <summary>
	/// アクションボタン (MeleeStrong=Up / MeleeWeak=Right / Dodge=Down / Heal=Left) で方向確定。
	/// 確定したら TriggerCloneCounterAction を呼ぶ。
	/// </summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="moveDelta">移動入力（-1..1。追加回避の方向に使う）</param>
	/// <returns>確定したら true</returns>
	bool TrySelectCloneByAction(class InputActionMap* actions, const Vector2& moveDelta);
	/// <summary>選ばれた方向の派生を発動する（分身はすべて破棄し、プレイヤーを分身位置へテレポート・該当アクションを発動）</summary>
	/// <param name="dir">確定した方向</param>
	/// <param name="moveDelta">移動入力（-1..1。追加回避の方向に使う）</param>
	void TriggerCloneCounterAction(CounterDir dir, const Vector2& moveDelta);

	/// <summary>通常回避（ダッシュ＋無敵窓）の入力とタイマー（クールダウン・行動ロック・無敵）を進める</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="moveDelta">移動入力（-1..1。ダッシュの向き）</param>
	/// <param name="dt">経過時間 [秒]（実時間）</param>
	void UpdateDodge(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	/// <summary>デフォルトの回復（大回復・STG/ボスごとの回数制）。ジャスト回避演出中は発動しない</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdateHeal(class InputActionMap* actions);
	/// <summary>ジャスト回避を成立させる（加点・攻撃元のハイライトとグレースケール・World のスロー・分身の表示）</summary>
	/// <param name="attacker">攻撃元</param>
	void TriggerJustDodge(IImGuiEditable* attacker);
	/// <summary>
	/// 回避・ジャスト回避のランタイム状態を即時クリア（Seek / LoadScene で呼ぶ）。
	/// スロー・グレースケールが残るのを防ぐため TimeScale と PostEffect も元に戻す。
	/// IRailStageHost::ResetDodgeState() の override（RailStagePart::Seek() から host_ 経由で呼ばれる）。
	/// </summary>
	void ResetDodgeState() override;

	/// <summary>ジャスト回避演出（グレースケールの強さ・受付期限・フェードアウト）を進め、終わったら時間とポストエフェクトを戻す</summary>
	/// <param name="dt">経過時間 [秒]（実時間）</param>
	void UpdateJustDodgeEffect(float dt);

public:
	/// <summary>
	/// ジャスト回避演出を発動する。プレイヤー + targetEnemy 以外のオブジェクトを
	/// duration 秒間グレースケール化する（最初/最後で短くフェード）。
	/// </summary>
	/// <param name="targetEnemy">色を残す敵（攻撃元）</param>
	/// <param name="duration">演出の長さ [秒]</param>
	void PlayJustDodgeEffect(IImGuiEditable* targetEnemy, float duration = 1.5f);
private:

	Phase phase_ = Phase::Rail;
	Phase prevPhase_ = Phase::Rail; // フェーズ遷移エッジ検出用（Boss 突入で Enter、離脱で Reset）

	// ボス戦カメラ操作ボタン（L1/マウス中ボタン）の短押し/長押し判定用ホールドタイマー。
	float bossCamBtnHold_ = -1.0f;      // >=0 で押下中の経過秒（-1=非押下）
	bool  bossCamBtnLongFired_ = false; // このホールドで長押し（トグル）を発火済みか
	float bossCamLongPressSec_ = 0.5f;  // 長押し閾値（秒）

	// ----- Rail → Landing 自動遷移 -----
	float seekMaxSec_ = 180.0f; // Rail終了トリガー秒。JSON "phase.seekMaxSec" に保存（3分＝3セクション×1分）
	float benchHoldSec_ = -1.0f; // 起動引数 --bench-hold の秒（>=0 でレールをその秒で止める。計測用、JSON非保存）


	// ----- Landing フェーズ（滞在時間・自動進行）-----
	float landingDuration_ = 10.0f; // Landing 滞在秒（将来カメラ演出に置換予定）。JSON "phase.landingDurationSec"
	float landingTimer_    = 0.0f;  // ランタイムのみ、JSON非保存

	// ----- ボス戦プリロード（未実装。バックグラウンドロード完了フラグだけ先に持つ）-----
	// 将来: Landing フェーズ中に別スレッドで ModelManager::PreloadCPU 等（CPU/GPU分割ロード。
	// ModelManager::FlushGPUUpload と同じパターン、Scene::ProcessAsyncLoads で毎フレームGPU反映）を実行し、
	// 完了したらこれを true にする。今はロード対象コンテンツが無いため常に true
	// （Landing の滞在時間は landingDuration_ だけで決まる）。
	bool bossPreloadReady_ = true;

	/// <summary>StagePlay.json から調整値を読み、各パート（レール・環境・ボス戦・必殺技など）にも渡す</summary>
	void LoadTuningFromJson();
	/// <summary>今の調整値と各パートの調整値を StagePlay.json へ書き出す</summary>
	void SaveTuningToJson() const;

public:
	/// <summary>ImGuiManager の "StagePlay Tuning" タブから呼ばれる中身（ImGui::Begin/End なし。開いたら後回しの初期化を済ませる）</summary>
	void OnImGuiTuning();
private:

	// ----- ポーズメニュー -----
	// ポーズ中は Update を早期 return してゲーム全体（エフェクト含む）を凍結し、メニューだけ動かす
	// 確認の画面（ConfirmStageSelect / ConfirmTitle）は「いいえ / はい」
	enum class PauseView { Main, ConfirmStageSelect, ConfirmTitle };
	bool paused_ = false;
	// トランジション中も最初の1回だけ Update を通したか（カメラ・空・物体の行列を一度書くため）
	bool firstUpdateDone_ = false;

	// ----- 初期化の後回し（16 番 F7 C'）-----
	// HUB の降下の続き（③IN）から始めるときは、降下に要らない物（ボス戦・大河・羽根・UI・配置・自機など）の初期化を
	// 降下中のフレームへ回す（差し替えのフレームで止まる時間を短くする）。それ以外の始まり方は Initialize の中で全部済ませる
	struct DeferredInitStep {
		const char* name;
		std::function<bool()> run;  // true＝この段は終わり。false なら次のフレームにもう一度呼ぶ（配置を数個ずつ作る段）
	};
	std::vector<DeferredInitStep> deferredInit_;
	size_t deferredInitNext_ = 0;
	bool   deferredInitActive_ = false;  // 降下中のフレームで進めている（ログを出す）
	int    deferredInitFrames_ = 0;
	double deferredInitWallMs_ = 0.0;
	/// <summary>後回しの初期化が残っているか</summary>
	/// <returns>残っていれば true</returns>
	bool HasDeferredInit() const { return deferredInitNext_ < deferredInit_.size(); }
	/// <summary>後回しの初期化を 1 フレームに budgetMs まで進める（最低 1 段）</summary>
	/// <param name="budgetMs">このフレームに使ってよい時間 [ms]</param>
	void UpdateDeferredInit(double budgetMs);
	/// <summary>後回しの初期化の残りを今すぐ全部済ませる（降下以外の段階へ進む・ポーズ・シーク・調整の画面の前に）</summary>
	void CompleteDeferredInit();
	/// <summary>後回しの初期化を 1 段進める（終わらない段は次にもう一度呼ぶ。読まれた物は先読みのマニフェストにも足す）</summary>
	void RunDeferredInitStep();
	bool deferredInitSkipFrame_ = false;  // 差し替えのフレーム（Initialize と同じフレーム）では段を進めない
	bool deferredSceneLoaded_ = false;    // 後回しの段で配置を読んだか（UI の建て直しの判定）
	// OnAfterSceneLoad の中身。後回しの段では別々のフレームで呼ぶ
	/// <summary>復元されたエンティティから player_ を貼り直す</summary>
	void RelinkPlayerAfterSceneLoad();
	/// <summary>作り直された動的スプラインから、レールカメラの走行スプラインを取り直す（解放済みを指したまま走らないように）</summary>
	void RebindRailAfterSceneLoad();
	/// <summary>シーンの動的スプライトが全削除されたので UI スプライト（HP バー・必殺ゲージ）を建て直す</summary>
	void RebuildUiAfterSceneLoad();
	PauseView pauseView_ = PauseView::Main;
	VerticalMenu pauseMenu_;
	ControlGuide controlGuide_;   // ポーズの「操作ガイド」（開いている間はメニューの代わりに描く）
	std::unique_ptr<SpriteInstance> pauseDimSprite_; // 背景を暗くする全画面スプライト（エディタ一覧に出さないため シーンの動的スプライトの外で所有）
	/// <summary>ポーズを開く（メイン画面から）</summary>
	void OpenPauseMenu();
	/// <summary>ポーズを閉じる</summary>
	void ClosePauseMenu();
	/// <summary>ポーズの画面（メイン・確認）を切り替え、項目を並べ直す（確認は「いいえ」を既定にする）</summary>
	/// <param name="view">次の画面</param>
	void SetPauseView(PauseView view);
	/// <summary>ポーズのメニューの入力（再開・操作ガイド・ステージセレクト・タイトル・確認）を読む</summary>
	/// <param name="actions">アクションの入力</param>
	void UpdatePauseMenu(class InputActionMap* actions);
	/// <summary>背景を暗くしてポーズのメニュー（か操作ガイド）を描く</summary>
	void DrawPauseMenu();

	// ----- デモモード（AttractMode）：始まって数秒でゲームオーバー / ポーズ →「ステージセレクトに戻る」→「はい」-----
	/// <summary>デモモードの自動進行（時間が来たら死ぬ、またはポーズを開いてメニューを自動で選ぶ）</summary>
	/// <returns>ポーズのメニューを自動で選んだら true（このフレームは人の入力を読まない）</returns>
	bool UpdateAttract();
	float attractTime_ = 0.0f;       // 始まってからの実時間（トランジション中は数えない）
	bool attractActed_ = false;      // このステージでのゲームオーバー / ポーズを済ませた
	bool attractPausing_ = false;    // デモモードが開いたポーズ（メニューを自動で選ぶ）
	AttractMenuTimer attractMenu_;

	// ----- ボス HP バー（ボス戦中のみ画面上部中央）-----
	// 下地 → 遅延ゲージ（減った分を遅れて追う白）→ 現在HP → 発狂ライン目盛り、の順に重ねる
	std::unique_ptr<SpriteInstance> bossHpBarBack_;
	std::unique_ptr<SpriteInstance> bossHpBarDelay_;
	std::unique_ptr<SpriteInstance> bossHpBarFill_;
	std::unique_ptr<SpriteInstance> bossHpBarRageMark_;
	bool  bossHpBarVisible_ = false;
	float bossHpRatio_ = 1.0f;
	float bossHpDelayRatio_ = 1.0f;
	float bossHpBarWidth_ = 800.0f;
	float bossHpBarHeight_ = 16.0f;
	float bossHpBarPosY_ = 58.0f;
	float bossHpDelaySpeed_ = 0.35f;   // 遅延ゲージが追う速さ（比率/秒）
	/// <summary>ボス HP バーのスプライト（下地・遅延ゲージ・現在 HP・発狂ライン目盛り）を作る</summary>
	void CreateBossHpBarUI();
	/// <summary>ボスの HP の比率を読み、遅延ゲージを追わせてスプライトの大きさを更新する（ボス戦中だけ見せる）</summary>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateBossHpBarUI(float dt);
	/// <summary>ボス HP バーを描く（ラベルはスコア表示と同じ Flush でまとめて描く）</summary>
	void DrawBossHpBarUI();

	// ----- プレイヤー被弾・無敵関連 -----
	float playerInvincibilityTimer_ = -2.0f;    // 被弾無敵残り時間（<=0 で通常）
	float playerInvincibilityDuration_ = 1.0f;  // 無敵継続秒数（ImGui調整可）
	float shootLockoutTimer_ = 0.0f;            // 射撃禁止タイマー
	float shootLockoutDuration_ = 0.5f;         // 被弾後の射撃禁止秒数（ImGui調整可）
	float damageBlinkFrequency_ = 12.0f;        // 点滅周波数 [Hz]
	float damageBlinkAlpha_ = 0.5f;             // 点滅時の半透明値
	// 点滅の色のコマに足す発光（色を掛けると青い新モデルでは色が出ないため。2026-10-19）
	Vector3 damageBlinkEmissive_{ 0.9f, 0.08f, 0.05f };   // 被弾：赤
	Vector3 dodgeBlinkEmissive_{ 0.15f, 0.55f, 0.9f };    // 回避無敵：水色
	Vector3 playerBlinkGlow_{ 0.0f, 0.0f, 0.0f };          // 今の点滅の発光（ApplyPlayerLook が下地に足す。羽根にも渡す）
	// 自機の照らし方（root["player"]["look"]）。2026-10-19 ユーザー：空の IBL（水平線より下も濃い青）で体の金と羽根が青くつぶれる
	// → IBL を切り、平行光源（雲の上は白）で照らす。金属度 0.976 のままだと IBL なしでは拡散が無く黒くなるので下げ、
	// 影の側が真っ黒にならないよう弱い白の下地（発光で足す）を付ける
	// → 雲の上の IBL を下が雲の白の物（stage_cloudtop_ibl）にしたので、既定は IBL 1・モデルの元の質感・下地なしに戻した。
	//   上書き（overrideMaterial）と下地は調整用に残す
	// → 2026-10-19 ユーザー：体の IBL は 0.5 くらい
	struct PlayerLook {
		float environmentCoefficient = 0.5f;
		bool  overrideMaterial = false;   // true で metallic / roughness を全部の部位に上書き
		float metallic = 0.5f;
		float roughness = 0.4f;
		float baseGlow = 0.0f;
	} playerLook_;
	/// <summary>毎フレーム：自機の IBL・金属度・粗さ・発光（下地＋点滅＋死亡演出のエーテルの光）を書く</summary>
	void ApplyPlayerLook();
	float damageCameraShakeIntensity_ = 0.12f;  // 被弾シェイクの強度
	float damageCameraShakeDuration_ = 0.25f;   // 被弾シェイクの継続秒

	// ----- スコア UI -----
	// ラベル "SCORE" と 数値を独立に調整できる。位置は画面右端基準の (X, Y) オフセット。
	float   scoreLabelScale_    = 0.6f;
	float   scoreLabelOffsetX_  = 16.0f;
	float   scoreLabelOffsetY_  = 12.0f;
	Vector4 scoreLabelColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float   scoreLabelOutlineThickness_ = 0.0f;
	Vector4 scoreLabelOutlineColor_{ 0.0f, 0.0f, 0.0f, 1.0f };

	float   scoreNumberScale_   = 1.5f;
	float   scoreNumberOffsetX_ = 16.0f;
	float   scoreNumberOffsetY_ = 40.0f;
	Vector4 scoreNumberColor_{ 1.0f, 1.0f, 1.0f, 1.0f };
	float   scoreNumberOutlineThickness_ = 0.0f;
	Vector4 scoreNumberOutlineColor_{ 0.0f, 0.0f, 0.0f, 1.0f };

	// ----- HP バーUI -----
	SpriteInstance* hpBarBackground_ = nullptr; // 赤ゲージ背景（遅延追従）
	SpriteInstance* hpBarForeground_ = nullptr; // 緑ゲージ前景（即時追従）
	float hpBarCurrentRatio_ = 1.0f;            // 緑ゲージの現在比率
	float hpBarTargetRatio_ = 1.0f;             // 赤ゲージの目標比率
	float hpBarLerpSpeed_ = 0.5f;               // 赤ゲージの線形追従速度（比率/秒）
	float hpBarMaxWidth_ = 300.0f;              // HP バー幅（pixel）
	float hpBarHeight_ = 20.0f;                 // HP バー高さ（pixel）
	float hpBarPosX_ = 60.0f;                   // HP バー左上 X（pixel、画面左上原点）
	float hpBarPosY_ = 30.0f;                   // HP バー左上 Y（pixel、画面左上原点）

	// ゲームオーバー状態
	bool gameOverTriggered_ = false;

	/// <summary>被弾の判定（ジャスト回避の成立・ノックバック）・無敵と点滅・射撃禁止・カメラの揺れ・HP 0 で死亡演出・HP バーを進める</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void UpdatePlayerDamageAndUI(float deltaTime);
	/// <summary>被弾した時の処理（HP を減らし、被弾音・無敵・射撃禁止・カメラの揺れを始める）</summary>
	/// <param name="damageAmount">受けたダメージ</param>
	void OnPlayerTakeDamage(int damageAmount);
	/// <summary>HP バーのスプライト（赤の背景・緑の前景）を作る</summary>
	void InitializeHPBarUI();
	/// <summary>HP の比率で緑ゲージは即時、赤ゲージは遅れて追わせる</summary>
	void UpdateHPBarUI();

	/// <summary>
	/// 無敵状態（ジャスト回避 / 回避無敵 / 被弾無敵 / 必殺技）を集約し、
	/// HP.enabled を切り替えて CollisionManager の自動ダメージも止める。
	/// シーン Update の終盤で呼ぶことで CollisionManager::Update より前に反映される。
	/// </summary>
	void RefreshPlayerHpInvincibility();

	// ----- LightningRuntime 動作確認用テストパネル（Phase2B、必殺技実装後に削除予定）-----
	std::unique_ptr<LightningRuntime> lightningTest_;

	// ----- ボス戦（未実装。Skybox演出などのトリガー用フラグだけ先に持つ）-----
	bool bossBattleActive_ = false;

	// ----- Skybox 演出トリガー用の前フレーム値（立ち上がり/立ち下がり検出）-----
	bool prevSpecialActive_    = false;
	bool prevBossBattleActive_ = false;

	// ----- Skybox 調整パラメータ（ImGui で編集／StagePlay.json に保存）-----
	std::string defaultSkyboxPath_ = "Resources/Cubemaps/rogland_clear_night_8k.dds"; // 平常時の空
	std::string bossSkyboxPath_    = "Resources/Cubemaps/passendorf_snow_8k.dds";      // ボス戦の空
	Vector4 skyboxTint_{ 1.0f, 1.0f, 1.0f, 1.0f };               // 常時の着色（テクスチャに乗算）
	Vector4 specialSkyboxDarkColor_{ 0.3f, 0.3f, 0.4f, 1.0f };   // 必殺技中の暗転色
	float   specialSkyboxFadeIn_     = 0.4f;                     // 暗転にかける秒
	float   specialSkyboxFadeOut_    = 0.6f;                     // 復帰にかける秒
	float   bossSkyboxBlendDuration_ = 2.0f;                     // ボス突入/離脱のクロスフェード秒

	// ----- 必殺技（両方に共通の状態。技ごとの段取り・演出・調整値は StormLockPart / ShatterlinePart）-----
	bool  specialActive_ = false;      // 発動中フラグ
	float specialTimer_  = 0.0f;       // 発動開始からの総経過秒（デバッグ表示用）
	// 発動中は自機を画面中央へ寄せる。終わったら戻す用に取っておく
	Vector2 specialPlayerInputOffsetSaved_{ 0.0f, 0.0f };
	Vector2 specialPlayerVelocitySaved_  { 0.0f, 0.0f };
	// 必殺技の演出中心：プレイヤー原点は足元なので、バリア球・電撃の中心を up 方向へ持ち上げて体の中心に合わせる。
	// 0 以下なら player の Collider.offset.y（プレハブの足元→カプセル中心）を自動採用。
	float specialPlayerCenterOffset_ = 0.0f;
	// 自機の体を囲む楕円（画面に投影したカプセル）の半軸。雷・ビームの始点はこの表面から出す。
	// 0 以下なら player の Collider.capsuleRadius / capsuleHeight から自動算出
	float specialBodyEllipseH_ = 0.0f;   // 水平（=capsuleRadius 相当）
	float specialBodyEllipseV_ = 0.0f;   // 垂直（=capsuleHeight/2 + capsuleRadius 相当）

	// ----- 必殺技 Storm Lock（範囲・多段・殲滅型）。シーンは ISpecialMoveHost 経由でだけ触られる -----
	StormLockPart stormLock_;

	// ----- 必殺技 Shatterline（線・瞬間・最高単発火力型）-----
	// 段取り・演出・調整値はすべて ShatterlinePart。シーンは ISpecialMoveHost 経由でだけ触られる
	ShatterlinePart shatterline_;
	// ----- ISpecialMoveHost 実装（private override。StormLockPart / ShatterlinePart 経由のみ。
	//       GetPlayer / SetTimeScale / IsEnemyAlive / AbsorbEnemyBulletsInSphere は上の override で満たす）-----
	/// <summary>必殺技の無敵を切り替える（Shatterline の後隙だけ false にして被弾できるようにする）</summary>
	/// <param name="invincible">true で無敵</param>
	void SetSpecialInvincible(bool invincible) override { specialInvincible_ = invincible; }
	/// <summary>必殺技の無敵が有効かを返す</summary>
	/// <returns>無敵中なら true</returns>
	bool IsSpecialInvincible() const override { return specialInvincible_; }
	/// <summary>自機の体の中心を返す</summary>
	/// <returns>体の中心のワールド座標</returns>
	Vector3 GetSpecialPlayerCenter() const override { return SpecialPlayerCenter(); }
	/// <summary>体の中心から dir へ撃つときの銃口（体を囲む楕円の表面）を返す</summary>
	/// <param name="playerCenter">自機の体の中心（ワールド）</param>
	/// <param name="dir">撃つ向き（ワールド）</param>
	/// <returns>銃口のワールド座標</returns>
	Vector3 GetSpecialMuzzle(const Vector3& playerCenter, const Vector3& dir) const override { return SpecialBoltStart(playerCenter, dir); }
	/// <summary>自機の体を囲む楕円の半軸を返す（調整値が 0 以下なら自機のコライダーのカプセルから求める）</summary>
	/// <param name="horizontal">画面の横方向の半軸の受け取り先</param>
	/// <param name="vertical">画面の縦方向の半軸の受け取り先</param>
	void GetSpecialBodyEllipse(float& horizontal, float& vertical) const override;
	/// <summary>レティクルの画面上の位置を返す</summary>
	/// <returns>レティクルの位置（UI 座標）</returns>
	Vector2 GetReticleScreenPos() const override;
	/// <summary>生きている敵弾の一覧を集める</summary>
	/// <param name="out">敵弾を追加する先</param>
	void CollectEnemyBullets(std::vector<IImGuiEditable*>& out) override;
	/// <summary>生きている敵本体（Enemy・Boss）の一覧を集める</summary>
	/// <param name="out">敵本体を追加する先</param>
	void CollectEnemies(std::vector<IImGuiEditable*>& out) override;
	/// <summary>敵弾を消す</summary>
	/// <param name="bullet">消す敵弾</param>
	void DestroyEnemyBullet(IImGuiEditable* bullet) override;
	/// <summary>HP を持たない敵をスコア・撃破数付きで消す（HP を持つ敵は HP を 0 にすれば通常の撃破の流れに乗る）</summary>
	/// <param name="e">消す敵</param>
	void DestroyEnemyWithScore(IImGuiEditable* e) override;

	/// <summary>必殺技ゲージのスプライト（暗い背景・現在値）を作る</summary>
	void InitializeSpecialGaugeUI();
	/// <summary>必殺技ゲージの位置・幅・色（MAX なら黄色）を毎フレーム反映する</summary>
	void UpdateSpecialGaugeUI();
	/// <summary>必殺技の発動入力と、発動中の段取り（終わったら EndSpecialMove でクールタイムへ）・クールタイムを進める</summary>
	/// <param name="actions">アクションの入力</param>
	/// <param name="dt">経過時間 [秒]</param>
	void UpdateSpecialMove(class InputActionMap* actions, float dt);
	/// <summary>種別ごとのゲージ上限を返す</summary>
	/// <param name="k">必殺技の種別</param>
	/// <returns>ゲージ上限</returns>
	float SpecialGaugeMaxFor(SpecialKind k) const {
		return (k == SpecialKind::Shatterline) ? specialGaugeMaxShatterline_ : specialGaugeMaxStormLock_;
	}
	/// <summary>装備切替＋specialGaugeMax_ ミラー更新＋ゲージクランプ</summary>
	/// <param name="k">装備する種別</param>
	void SetEquippedSpecial(SpecialKind k);
	/// <summary>クールタイム/ゲージ条件を満たせば発動する</summary>
	/// <param name="k">発動する種別</param>
	void TryTriggerSpecial(SpecialKind k);
	/// <summary>装備中種別を発動（無条件・ImGuiボタン/内部用）</summary>
	void TriggerSpecialMove();
	/// <summary>種別を指定して発動（種別で分岐。発動した種別を装備中に同期する）</summary>
	/// <param name="k">発動する種別</param>
	void TriggerSpecialMove(SpecialKind k);
	/// <summary>動的コンテナにポインタが生存しているか</summary>
	/// <param name="e">調べる実体</param>
	/// <returns>まだシーンにあれば true</returns>
	bool IsSpecialTargetAlive(IImGuiEditable* e) const;
	/// <summary>画面内の敵/敵弾を列挙する（飛行ユニット用）</summary>
	/// <param name="out">(実体, 敵弾なら true) を追加する先</param>
	void CollectScreenTargets(std::vector<std::pair<IImGuiEditable*, bool>>& out);
	/// <summary>自機の体を囲む楕円（画面に投影したカプセル）の表面の始点</summary>
	/// <param name="playerPos">自機の体の中心（ワールド）</param>
	/// <param name="dir">始点 → 対象の向き（正規化済み）</param>
	/// <returns>始点のワールド座標</returns>
	Vector3 SpecialBoltStart(const Vector3& playerPos, const Vector3& dir) const;
	/// <summary>必殺技演出の中心（足元 + offset up = 体の中心。up はカメラの上）</summary>
	/// <returns>体の中心のワールド座標</returns>
	Vector3 SpecialPlayerCenter() const;
	/// <summary>必殺技を終える（クールタイム開始・両方の技の段取りと演出を戻す・無敵フラグを既定へ・World の時間を戻す・自機を画面の中心へ戻す）</summary>
	void EndSpecialMove();
	/// <summary>必殺技中のカメラ（Shatterline は専用のシネマティック、Storm Lock は後ろ＋上へ引く）で上書きする</summary>
	/// <param name="playerWorldPos">自機のワールド位置</param>
	void ApplySpecialCamera(const Vector3& playerWorldPos);

	// ゲージ UI（HP バーと同じスプライト2枚パターン）
	SpriteInstance* gaugeBarBackground_ = nullptr; // 暗背景
	SpriteInstance* gaugeBarForeground_ = nullptr; // 現在値
	float gaugeBarMaxWidth_ = 300.0f;
	float gaugeBarHeight_   = 12.0f;
	float gaugeBarPosX_     = 60.0f;
	float gaugeBarPosY_     = 60.0f; // HP バー下
	Vector4 gaugeBarBgColor_   { 0.10f, 0.10f, 0.20f, 0.8f };
	Vector4 gaugeBarFgColor_   { 0.55f, 0.75f, 1.00f, 1.0f };  // ノーマル時：水色
	Vector4 gaugeBarFullColor_ { 1.00f, 0.85f, 0.30f, 1.0f };  // MAX時：黄色（発動可能サイン）
};
