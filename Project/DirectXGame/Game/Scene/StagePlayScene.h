#pragma once
#include "BaseScene.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Wave/WaveDef.h"
#include "Effect/EffectManager.h"
#include <memory>
#include <vector>
#include <unordered_set>
#include <utility>
#include <cstdint>

class Camera;
class Skybox;
class SplineCurveActor;
class RailCameraController;
class AnimatedObject3DInstance;
class Reticle;
class LightningRuntime;

/// <summary>
/// ステージプレイシーン
/// 道中レールSTG(2-3分) → ボス戦3Dアクション(3-5分) をシームレスに繋ぐ
/// 内部に Phase（Rail / Landing / Boss）を持つ予定
/// </summary>
class StagePlayScene : public BaseScene {
public:
	enum class Phase {
		Rail,       // レールシューティング道中
		Landing,    // 着地遷移（カメラ補間）
		Boss,       // ボス戦3Dアクション
	};

	StagePlayScene();
	~StagePlayScene() override;

	void Initialize() override;
	void Finalize() override;
	void Update() override;
	void Draw() override;
	void DrawAfterPostEffect(ID3D12GraphicsCommandList* commandList) override;

	// シーンタイムラインのシーク。RailCamera の progress を経過秒から再計算する。
	void Seek(float seconds) override;
	float GetCameraProgressT() const override;

	Camera* GetCamera() override;

	// シーン配置（プレハブ・スプライン等）の保存/復元
	bool SaveSceneToJson(const std::string& filePath) override;
	bool LoadSceneFromJson(const std::string& filePath) override;

private:
	std::unique_ptr<Camera> camera_;
	std::unique_ptr<Skybox> skybox_;

	// レールカメラ用の 2 本のスプライン（cameraPath=位置 / lookAtPath=注視点）
	std::unique_ptr<SplineCurveActor> cameraPath_;
	std::unique_ptr<SplineCurveActor> lookAtPath_;
	std::unique_ptr<RailCameraController> railCamera_;

	// プレイヤー：dynamicAnimated_ が所有、ここは参照用ポインタ
	// カメラのローカル空間で playerLocalOffset_ の位置に毎フレ配置する
	AnimatedObject3DInstance* player_ = nullptr;

	// ----- ImGui で編集可能な調整値（Resources/Json/Tuning/StagePlay.json に自動同期） -----
	Vector3 playerLocalOffset_{ 0.0f, -0.5f, 6.0f };  // カメラローカルの中心位置（無入力時）
	float   railCameraSpeed_ = 1.0f / 120.0f;          // RailCamera の進行速度（t/秒）。120秒で踏破
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

	// ----- スポーン -----
	WaveDef currentWave_;
	std::vector<bool>  spawnFired_;    // entries と同じサイズ。スポーン済みフラグ
	std::vector<bool>  retreatFired_;  // entries と同じサイズ。退避発令済みフラグ
	// entries と同じサイズ。そのエントリから生まれた敵が倒された時の t 値。
	// 負数 = まだ倒されていない。Seek で「killAtT_ > currentT」なら未死亡扱いに戻す。
	std::vector<float> killAtT_;

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
	float reticleBaseStickSpeed_ = 1200.0f;    // 通常時レティクル感度（Initialize で取得）

	void UpdatePrecisionAim(class InputActionMap* actions, float dt);
	// プレイヤーのワールド位置を基準に、表示カメラを肩越し位置へ寄せる（向きは維持）。
	// プレイヤー配置（基準カメラ）後に呼ぶ。precisionBlend_ で補間。
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

	void UpdateMeleeCombo(class InputActionMap* actions, float dt);
	void SpawnPendingMelee();             // 発生待ちの近接判定を実際に spawn する
	// aim 基底（前=自機→firingTarget、右=worldUp×前、上=前×右）を返す
	void ComputeAimBasis(Vector3& right, Vector3& up, Vector3& forward) const;

public:
	/// <summary>
	/// 近接攻撃の発生〜後隙中で、他の行動（射撃/回避/近接）が禁止されているか。
	/// 将来の回避実装などからも参照する。
	/// </summary>
	bool IsActionLocked() const { return meleeActionLockTimer_ > 0.0f || dodgeActionLockTimer_ > 0.0f || jdSelecting_ || specialActive_; }
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
	float dodgeJustWindow_ = 0.2f;       // ジャスト成立窓：入力後この秒数以内に被弾接触すると成立
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

	// 必殺技の種別（装備制：傲慢サンダー or ディスラプターの2択、片方のみ持込）。
	// 本来は発動ボタン共通（R3/F=Ultimate）だが、デバッグでは別キーで両方試せるよう分離する。
	enum class SpecialKind { GoumanThunder, Disruptor };
	SpecialKind equippedSpecial_ = SpecialKind::GoumanThunder; // 現在装備中の必殺技

	// 必殺技ゲージ（UI 未実装。ゲージ本体だけ先に持つ）
	float specialGauge_         = 0.0f;   // 現在値（0..specialGaugeMax_）
	float specialGaugeMax_      = 100.0f; // 装備中種別の実効上限（種別切替でミラー更新）
	float specialGaugeMaxGouman_    = 100.0f; // 傲慢サンダーのゲージ上限
	float specialGaugeMaxDisruptor_ = 120.0f; // ディスラプターのゲージ上限
	float dodgeSpecialGaugeGain_ = 8.0f;  // 追加回避1回ぶんの加算量

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
	std::vector<IImGuiEditable*> jdClones_;        // 分身ビジュアル（Object3D。index=CounterDir-1）
	// 各方向の分身モデルパス（あとで派生モーション初期ポーズの非アニメ.meshに差し替えやすいよう方向ごとに持つ）
	std::string jdClonePath_[4] = {
		"Resources/Models/Animated/Walk/walk.mesh", // Up
		"Resources/Models/Animated/Walk/walk.mesh", // Right
		"Resources/Models/Animated/Walk/walk.mesh", // Down
		"Resources/Models/Animated/Walk/walk.mesh", // Left
	};
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

	// 追加回避（Down）派生：許容枠を一時拡張＋戻し補間＋射撃禁止
	bool    jdDodgeMarginActive_   = false;    // 拡張クリップ枠が有効か（戻し中も true）
	float   jdDodgeMarginTimer_    = 0.0f;     // 戻し補間タイマー（0..jdDodgeReturnDuration_）
	bool    jdDodgeReturning_      = false;    // 拡張→通常へ補間中
	Vector2 jdDodgeExpandedMargin_{ 0.8f, 0.8f }; // 拡張時のクリップマージン
	float   jdDodgeReturnDuration_ = 0.35f;    // 戻し時間
	Vector2 jdDodgeReturnFromOffset_{ 0.0f, 0.0f }; // 戻し開始時の playerInputOffset_（押し戻し補間用）

	void ApplyJustDodgeCamera(const Vector3& playerWorldPos);
	void ApplyJustDodgeMeleeCamera(const Vector3& playerWorldPos);
	void UpdateJustDodgeCounterAction(float dt);
	void EndJustDodgeCounterAction();
	void SpawnJustDodgeClones();
	void UpdateJustDodgeClones(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	void ClearJustDodgeClones();
	// アクションボタン (MeleeStrong=Up / MeleeWeak=Right / Dodge=Down / Heal=Left) で方向確定。
	// 確定したら TriggerCloneCounterAction を呼んで true を返す。
	bool TrySelectCloneByAction(class InputActionMap* actions, const Vector2& moveDelta);
	// 選ばれた分身を実体化（α=1）・残り3体を破棄・プレイヤーを分身位置へテレポート・該当アクションを発動。
	void TriggerCloneCounterAction(CounterDir dir, const Vector2& moveDelta);

	void UpdateDodge(class InputActionMap* actions, const Vector2& moveDelta, float dt);
	void UpdateHeal(class InputActionMap* actions);
	void TriggerJustDodge(IImGuiEditable* attacker);
	// 回避・ジャスト回避のランタイム状態を即時クリア（Seek / LoadScene で呼ぶ）。
	// スロー・グレースケールが残るのを防ぐため TimeScale と PostEffect も元に戻す。
	void ResetDodgeState();

	void UpdateJustDodgeEffect(float dt);

public:
	/// <summary>
	/// ジャスト回避演出を発動する。プレイヤー + targetEnemy 以外のオブジェクトを
	/// duration 秒間グレースケール化する（最初/最後で短くフェード）。
	/// </summary>
	void PlayJustDodgeEffect(IImGuiEditable* targetEnemy, float duration = 1.5f);
private:

	Phase phase_ = Phase::Rail;

	void LoadTuningFromJson();
	void SaveTuningToJson() const;

public:
	// ImGuiManager の "StagePlay Tuning" タブから呼ばれる中身（ImGui::Begin/End なし）
	void OnImGuiTuning();
private:

	// ポーズ状態（後でメニュー実装）
	bool paused_ = false;

	// ----- プレイヤー被弾・無敵関連 -----
	float playerInvincibilityTimer_ = -2.0f;    // 被弾無敵残り時間（<=0 で通常）
	float playerInvincibilityDuration_ = 1.0f;  // 無敵継続秒数（ImGui調整可）
	float shootLockoutTimer_ = 0.0f;            // 射撃禁止タイマー
	float shootLockoutDuration_ = 0.5f;         // 被弾後の射撃禁止秒数（ImGui調整可）
	float damageBlinkFrequency_ = 12.0f;        // 点滅周波数 [Hz]
	float damageBlinkAlpha_ = 0.5f;             // 点滅時の半透明値
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

	void UpdatePlayerDamageAndUI(float deltaTime);
	void OnPlayerTakeDamage(int damageAmount);
	void InitializeHPBarUI();
	void UpdateHPBarUI();

	// 無敵状態（ジャスト回避 / 回避無敵 / 被弾無敵 / 必殺技）を集約し、
	// HP.enabled を切り替えて CollisionManager の自動ダメージも止める。
	// シーン Update の終盤で呼ぶことで CollisionManager::Update より前に反映される。
	void RefreshPlayerHpInvincibility();

	// ----- LightningRuntime 動作確認用テストパネル（Phase2B、必殺技実装後に削除予定）-----
	std::unique_ptr<LightningRuntime> lightningTest_;

	// ----- 必殺技「傲慢サンダー」 -----
	// Phase 1 (Barrier): バリア展開・プレイヤー強制位置・カメラ引き・無敵・バリア当たり判定
	// Phase 2-5 は Step B-2 以降で追加
	enum class SpecialPhase {
		Idle,
		Barrier,   // バリア展開（通常時間進行、バリア当たり判定）
		Lockon,    // Step B-2: ワールド時間停止 + 順次ロックオン + チャージ
		Fire,      // Step B-3: 時間停止解除 + サンダー発射
		End,       // Step B-4: 終了猶予（バリア停止までの間）
	};
	SpecialPhase specialPhase_ = SpecialPhase::Idle;
	float specialPhaseTimer_ = 0.0f;     // 現フェーズ内の経過秒
	bool  specialActive_   = false;      // 発動中フラグ（Idle 以外で true）
	float specialTimer_    = 0.0f;       // 発動開始からの総経過秒（デバッグ表示用）
	float specialDuration_ = 5.0f;       // [互換] 旧 Step A の総時間（ImGui で残す、未使用化）

	// Phase 1（バリア）
	float specialBarrierRadius_   = 2.0f;   // バリア球半径
	float specialBarrierDuration_ = 3.0f;   // Phase 1 の長さ（秒）
	Vector4 specialBarrierColor_{ 1.0f, 0.8f, 0.3f, 0.10f }; // 薄い金（内膜）
	bool  specialBarrierFillOn_ = false;    // 塗りつぶし半透明球を表示するか（既定 OFF、ワイヤ球のみ）

	// プレイヤー強制位置・カメラ引き
	Vector2 specialPlayerInputOffsetSaved_{ 0.0f, 0.0f }; // 復帰用に保存
	Vector2 specialPlayerVelocitySaved_  { 0.0f, 0.0f };
	float   specialCamPullback_ = 8.0f;   // forward 逆方向（ジャスト回避と同じ既定）
	float   specialCamFovAdd_   = 0.06f;  // FovY 加算
	float   specialCamUpAdd_    = 1.5f;   // up 方向へ持ち上げ（プレイヤーが画面下寄りに）

	// バリア可視化（半透明球プリミティブ）
	std::unique_ptr<class EffectPrimitiveRenderer> specialBarrierVis_;

	// 遅延削除キュー：GPU が前フレームの commandList を実行中に resource を Release すると
	// D3D12 OBJECT_DELETED_WHILE_STILL_IN_USE になるため、数フレーム保持してから捨てる
	struct SpecialTrashEntry {
		std::unique_ptr<class EffectPrimitiveRenderer> renderer;
		std::unique_ptr<class LightningRuntime> lightning;
		int framesLeft = 4;
	};
	std::vector<SpecialTrashEntry> specialTrash_;

	// ----- Phase 2（ロックオン + チャージ）-----
	struct SpecialLockonTarget {
		IImGuiEditable* entity = nullptr;        // 敵本体 or 突進敵
		int   bulletIndex = -1;                   // 弾の場合の bullets_ インデックス（-1=敵）
		int   patternIndex = 0;                   // 0=A(真上/左下/右下), 1=B(真下/右上/左上)
		float startTime = 0.0f;                   // Phase 2 経過秒（このターゲットのロック開始時刻）
		float radius = 0.5f;                      // 敵 collider 半径
		bool  visualsCreated = false;             // 線・リング・キューブ生成済みか
		std::unique_ptr<class EffectPrimitiveRenderer> lines[3];
		std::unique_ptr<class EffectPrimitiveRenderer> ring;
		std::unique_ptr<class EffectPrimitiveRenderer> cube;
	};
	std::vector<SpecialLockonTarget> specialLockonTargets_;
	float specialLockonInterval_         = 0.075f;  // ロックオン開始間隔
	float specialLockonDuration_         = 0.5f;    // 1体あたりロックオン完了までの秒
	float specialLockonLineTravelTime_   = 0.2f;    // 線が敵中心へ集まる時間
	float specialLockonLineHoldTime_     = 0.1f;    // 集まった後の停止時間（線消滅まで）
	float specialLockonRingAppearTime_   = 0.2f;    // リング出現時刻（ロック開始から）
	float specialLockonRingFadeoutTime_  = 0.1f;    // ロック完了後リングが消えるまでの時間
	float specialChargeDuration_         = 1.5f;    // 全ロック後のチャージ秒
	Vector4 specialLockonColor_{ 0.3f, 0.7f, 1.0f, 1.0f }; // 線・リングの青
	Vector4 specialCubeColor_  { 1.0f, 0.5f, 0.0f, 0.5f }; // キューブのオレンジ
	float specialLockonLineWidth_  = 0.04f;
	float specialLockonRingThickness_ = 0.05f;
	float specialPhase2Timer_ = 0.0f; // Phase 2 経過秒（ロック演出時刻基準）
	bool  specialAllLockonComplete_ = false;
	float specialChargeStartTime_   = 0.0f; // Phase2 内での全ロック完了時刻

	// チャージ電撃（複数本がプレイヤー周囲のカプセル表面で短い弧を描く）
	struct SpecialChargeBolt {
		std::unique_ptr<class LightningRuntime> rt;
		float nextRegenTime = 0.0f;  // 次に角度を再ランダム化する時刻（Phase 2 経過秒）
		float angleBase = 0.0f;       // 現在の基準角度（ラジアン）
	};
	std::vector<SpecialChargeBolt> specialChargeBolts_;
	int   specialChargeBoltCount_     = 4;       // 同時本数
	float specialChargeRegenInterval_ = 0.1f;    // 1本ごとの再生成間隔
	// 始点・終点の半径制御（プレイヤーカプセル形状を考慮し、スクリーン投影楕円で計算）
	// 0 以下なら player の Collider.capsuleRadius / capsuleHeight から自動算出
	float specialChargeStartRadiusH_  = 0.0f;    // 水平半径（=capsuleRadius 相当）
	float specialChargeStartRadiusV_  = 0.0f;    // 垂直半径（=capsuleHeight/2 + capsuleRadius 相当）
	float specialChargeMinLength_     = 0.6f;    // 終点 - 始点 の最小長

	// ----- Phase 3（Fire = サンダー発射）-----
	// ロック済み + 新規侵入のターゲットを順次サンダーで拘束し、ダメージティックで削る。
	struct SpecialFireBolt {
		std::unique_ptr<class LightningRuntime> rt;
		IImGuiEditable* entity = nullptr;      // 対象（敵本体 or 弾の primitive ポインタ）
		bool   isBullet = false;               // HP を持たない対象（敵弾）か
		float  elapsed = 0.0f;                 // この対象に当て始めてからの経過秒
		float  nextTickTime = 0.0f;            // 次のダメージティック時刻（elapsed 基準）
		Vector3 lastTargetPos{ 0.0f, 0.0f, 0.0f }; // 最後に確認した対象位置（終点用）
		bool   done = false;                   // 終了フラグ（trash へ回収予定）
	};
	std::vector<SpecialFireBolt> specialFireBolts_;
	struct SpecialFireQueueItem { IImGuiEditable* entity = nullptr; bool isBullet = false; };
	std::vector<SpecialFireQueueItem> specialFireQueue_;        // 処理待ちターゲット
	std::unordered_set<IImGuiEditable*> specialFireProcessed_;  // 既に発射枠へ回した対象（再ロック防止）
	float specialFireLaunchTimer_ = 0.0f;        // 次の発射までの残り秒
	// Fire パラメータ（ImGui 調整 / JSON 保存）
	int   specialFireSimultaneous_   = 3;        // 同時サンダー本数
	float specialFireMinHold_        = 0.5f;     // 最低拘束時間（HP=0 でもこの秒数は当てる）
	float specialFireMaxHold_        = 2.0f;     // 最高拘束時間（生存でも強制終了）
	float specialFireTickInterval_   = 0.1f;     // ダメージティック間隔
	int   specialFireTickDamage_     = 5;        // 1ティックダメージ
	float specialFireLaunchInterval_ = 0.1f;     // 次の発射開始間隔
	Vector4 specialFireColor_{ 0.5f, 0.8f, 1.0f, 1.0f }; // サンダーの色
	// サンダーの見た目（敵へ飛んでいく感 vs 放電感のバランス調整）
	float specialFireGrowTime_       = 0.08f;    // 始点→ターゲットへ伸びる時間（0で即全長）
	float specialFireMaxOffsetRatio_ = 0.10f;    // ジグザグの暴れ幅（小=直線的＝飛んでる感）
	float specialFireBranchProb_     = 0.30f;    // 枝分かれ確率（放電のパチパチ感）
	float specialFireStartWidth_     = 0.10f;    // 始点(プレイヤー側)の太さ
	float specialFireEndWidth_       = 0.05f;    // 終点(敵側)の太さ
	float specialFireBoltLifetime_   = 0.08f;    // 1メッシュの寿命（小=高速にパチパチ再生成）

	// ----- 光の翼（X字方向に「小さいパーティクル」を外向き噴出。アーム1本=GPUパーティクル群1つ）-----
	// 将来プレイヤー常時装備へ流用予定。描画は GPUParticleManager が自動で行う。
	bool    specialWingOn_          = true;
	int     specialWingArmCount_    = 4;          // アーム本数（X字=4）
	float   specialWingAngleOffset_ = 0.7853982f; // 配置の基準角(rad)。π/4 で X 字
	float   specialWingSpeed_       = 6.0f;       // 外向き初速（実質長さ ≒ speed×life）
	float   specialWingLife_        = 0.4f;       // 粒子寿命（秒）
	int     specialWingBurstCount_  = 6;          // 1アーム・1フレームの粒子数（量）
	float   specialWingJitter_      = 1.0f;       // 速度ゆらぎ
	float   specialWingEmitRadius_  = 0.1f;       // 発生位置の散らばり（中心付近）
	Vector2 specialWingScaleMin_{ 0.04f, 0.04f }; // めっちゃ小さい
	Vector2 specialWingScaleMax_{ 0.10f, 0.10f };
	Vector4 specialWingColorInner_{ 1.0f, 0.85f, 0.20f, 1.0f }; // 金（発生時）
	Vector4 specialWingColorOuter_{ 1.0f, 0.40f, 0.85f, 0.0f };  // ピンク（寿命末でフェード）

	// ----- Phase 4（End = 終了猶予）-----
	float specialEndDuration_ = 1.0f;            // Fire 終了からバリア消失までの猶予秒

	// 必殺技の演出中心：プレイヤー原点は足元なので、バリア球・電撃の中心を up 方向へ持ち上げて体の中心に合わせる。
	// 0 以下なら player の Collider.offset.y（プレハブの足元→カプセル中心）を自動採用。
	float specialPlayerCenterOffset_ = 0.0f;

	// バリア本体エフェクト（エフェクトエディタ製）。名前が空でなく登録済みなら、これを再生して追従。
	// 再生中はワイヤーフレーム球は描画しない（空 or 未登録ならワイヤー球にフォールバック）。
	std::string  specialBarrierEffectName_   = "Barrier";
	EffectHandle specialBarrierEffectHandle_ = kInvalidEffectHandle;

	// バリアの回転ワイヤーフレーム球（LineRenderer で経線・緯線を描画）。当たり判定の正確な範囲を可視化。
	bool    specialBarrierWireframeOn_     = true;
	Vector3 specialBarrierWireSpin_{ 0.0f, 0.0f, 0.0f };           // 各軸の累積回転角（rad、内部状態）
	Vector3 specialBarrierWireSpinSpeed_{ 0.5f, 1.2f, 0.3f };      // 各軸の回転速度（rad/s、実時間）
	int     specialBarrierWireMeridians_   = 8;      // 経線本数
	int     specialBarrierWireParallels_   = 5;      // 緯線本数
	int     specialBarrierWireSegments_    = 24;     // 1円の分割数
	Vector4 specialBarrierWireColorGold_{ 1.0f, 0.84f, 0.15f, 0.9f }; // 経線=金
	Vector4 specialBarrierWireColorPink_{ 1.0f, 0.35f, 0.80f, 0.9f }; // 緯線=ピンク

	// バリアのパーティクル演出（GPU パーティクルを球状に連続バースト）。当たり判定は球プリミティブ側のまま。
	// 球状ランダムは虫っぽくなるため既定 OFF。将来は外周リング沿い等に作り直す。
	bool    specialBarrierParticleOn_   = false;
	float   specialBarrierEmitInterval_ = 0.02f;   // バースト間隔（秒、実時間）
	int     specialBarrierEmitCount_    = 10;      // 1バーストの粒子数
	float   specialBarrierParticleLife_ = 0.5f;    // 粒子寿命（秒）
	float   specialBarrierParticleRadiusScale_ = 1.0f; // 発生半径 = barrierRadius × これ
	Vector2 specialBarrierParticleScaleMin_{ 0.06f, 0.06f };
	Vector2 specialBarrierParticleScaleMax_{ 0.16f, 0.16f };
	Vector4 specialBarrierParticleColor0_{ 0.5f, 0.85f, 1.0f, 1.0f }; // 発生時の色
	Vector4 specialBarrierParticleColor1_{ 0.2f, 0.5f,  1.0f, 0.0f }; // 寿命末の色（フェード）
	float   specialBarrierEmitAccum_  = 0.0f;      // 内部バーストタイマ
	bool    specialBarrierGroupReady_ = false;     // GPU パーティクルグループ生成済み

	// ----- 必殺技「ディスラプター」（線・瞬間・最高単発火力型）-----
	// 傲慢サンダー（SpecialPhase）とは別のフェーズ機で進む。Step 2 はタイマーのみ（ビジュアルなし）。
	enum class DisruptorPhase {
		Idle,
		Charge,    // Phase1 チャージ(収束)：World スロー＋無敵＋プレイヤー中央＋カメラ引き
		Slash,     // Phase2 一閃：World 停止
		Collapse,  // Phase3 崩壊：World 停止
		Recover,   // Phase4 復帰＋後隙：World 再開・無敵なし・入力不可
	};
	DisruptorPhase disruptorPhase_      = DisruptorPhase::Idle;
	float          disruptorPhaseTimer_ = 0.0f;  // 現フェーズ経過秒（実時間）
	// フェーズ長（秒、実時間）
	float disruptorChargeDuration_   = 2.5f;  // Phase1 チャージ（狙う猶予。ImGui/JSONで調整可）
	float disruptorSlashDuration_    = 1.5f;  // Phase2 一閃＝発射ショットの表示尺（カメラ到達は門番で別途保証）。確認しやすいよう長め
	float disruptorCollapseDuration_ = 2.5f;  // Phase3 崩壊（リビールの剥がれ時間も兼ねる。ゆっくり戻す）
	float disruptorRecoverDuration_  = 2.0f;  // Phase4 後隙（無敵なし・入力不可）
	float disruptorChargeTimeScale_  = 0.0f;  // チャージ中の World 時間倍率（0=停止＝敵が固定で狙いやすい。>0でスロー）
	// Slash 中の発射タイミング：カメラが発射ショット位置へ回り込み切ってからビーム発射し、
	// 発射から少し遅れて色反転を入れる（回り込み→発射→一瞬遅れて反転→崩壊 の順を保証）。
	bool  disruptorFired_       = false;  // この一閃でビーム発射済みか（カメラ到達後に true）
	float disruptorFireElapsed_ = 0.0f;   // 発射からの経過秒（色反転ディレイ／表示尺に使用）
	float disruptorInvertDelay_ = 0.1f;   // 発射から色反転 ON までの遅れ（秒）
	// Collapse：カメラが「狙うアングル」へ戻り切ってから断裂線走り込み＆崩壊を進める用のタイマー。
	// カメラ復帰中（到達前）は進めない＝崩壊が発射ショットの後方から始まるのを防ぐ。
	float disruptorCollapseRevealTimer_ = 0.0f;

	// 照準（中央支点・1点置き・角度自由）。スクリーン空間、深度無視。
	// 線は画面中央 (kClientWidth/2, kClientHeight/2) を支点に角度 θ だけで決まる。0=横一閃(水平)。
	float disruptorAimAngle_     = 0.0f;   // 切断線の角度(rad)。チャージ中はレティクルへライブ追従
	bool  disruptorAimConfirmed_ = false;  // 射撃ボタンで角度を確定したか
	float disruptorLineWidthPx_  = 40.0f;  // (Step4) スクリーン空間の線への距離しきい値(px, 半幅)。可視化太さにも流用
	int   disruptorBossDamage_   = 1000;   // (Step4) ボスへの固定ダメージ（+将来スタンゲージ削り）
	Vector4 disruptorAimPreviewColor_{ 1.0f, 0.85f, 0.2f, 0.6f }; // 予測線（調整中）＝半透明の金
	Vector4 disruptorAimConfirmColor_{ 0.6f, 0.4f, 1.0f, 1.0f };  // 確定線＝紫
	float   disruptorPivotRadiusPx_  = 14.0f;                     // 中央支点マーカー（リング）の半径(px)
	Vector4 disruptorPivotColor_{ 1.0f, 1.0f, 1.0f, 0.9f };       // 支点マーカー色（白）

	// ディスラプターのカメラ演出（フェーズ別の目標姿勢を補間。詳細は下の Step5-A 群と ApplyDisruptorCamera）。
	// 切断線は一閃でワールド空間へ焼き付くので、アングルを変えても線と敵がズレない。傲慢の specialCam* とは別管理。
	float disruptorCamPullback_ = 8.0f;   // forward 逆方向への後退量（Charge 時）
	float disruptorCamUpAdd_    = 1.5f;   // up 方向の持ち上げ（Charge 時）
	float disruptorCamFovAdd_   = 0.06f;  // FovY 加算（Charge 時）
	float disruptorCamEaseTime_ = 0.25f;  // 1フェーズ分のカメラ移動時間（秒・決定的smoothstep。これが終わるまで次フェーズへ進まない）
	float disruptorCamBlend_    = 0.0f;   // 旧ランタイム（現在カメラ駆動には未使用。互換のため残置）

	// Step5-A: フェーズ別の目標カメラ姿勢を補間する演出。
	// Charge=後方引き(狙う) → Slash=左横やや前・あおり(発射ショット) → Collapse=後方復帰(断裂線が走り込む) → Recover=通常へ。
	bool    disruptorCamActive_ = false;   // 演出中フラグ（EnterDisruptor/EndSpecialMove で切替）
	bool    disruptorCamInit_   = false;   // 補間状態を実カメラで初期化済みか
	Vector3 disruptorCamEyeCur_{ 0.0f, 0.0f, 0.0f };  // 補間中のカメラ位置
	Vector3 disruptorCamLookCur_{ 0.0f, 0.0f, 0.0f }; // 補間中の注視点
	float   disruptorCamFovCur_ = 0.0f;    // 補間中の FovY
	// 決定的なフェーズ間移動（前の移動が完了してから次フェーズへ＝手続き形式）。秒数設定を変えても破綻しない。
	DisruptorPhase disruptorCamMovePhase_ = DisruptorPhase::Idle; // 現在移動中の対象フェーズ（変化検出用）
	Vector3 disruptorCamFromEye_{ 0.0f, 0.0f, 0.0f };  // 移動開始時のカメラ位置
	Vector3 disruptorCamFromLook_{ 0.0f, 0.0f, 0.0f }; // 移動開始時の注視点
	float   disruptorCamFromFov_   = 0.0f; // 移動開始時の FovY
	float   disruptorCamMoveTimer_ = 0.0f; // 現フェーズ移動の経過秒
	bool    disruptorCamArrived_   = false;// 現フェーズの目標姿勢へ到達したか（フェーズ遷移の門番）
	// 発射ショット（Slash）：プレイヤー基準オフセット（左横やや前・あおり）
	float   disruptorCamFireSide_   = 6.0f; // プレイヤー左方向の距離
	float   disruptorCamFireFront_  = 5.0f; // 前方（画面奥）方向の距離
	float   disruptorCamFireDown_   = 3.0f; // 下げ量（あおり）
	float   disruptorCamFireLookUp_ = 0.5f; // 注視点の持ち上げ
	float   disruptorCamFireFovAdd_ = 0.0f; // 発射ショットの FovY 加算
	// Collapse：後方引き（Charge とは別値で管理）
	float   disruptorCamCollapsePullback_ = 8.0f;
	float   disruptorCamCollapseUpAdd_    = 1.5f;

	// 一閃の瞬間に確定するワールド空間の切断線（カメラを引いても敵と一緒に世界に残る＝描いた通り）。
	bool    disruptorCutWorldValid_ = false;
	Vector3 disruptorCutWorldP1_{ 0.0f, 0.0f, 0.0f };
	Vector3 disruptorCutWorldP2_{ 0.0f, 0.0f, 0.0f };
	float   disruptorCutDepth_ = 40.0f;   // ヒット敵が無いときの焼き付け奥行き（敵があれば平均で上書き）

	// ----- Step5-B: 発射ビーム / 衝撃波 / 断裂線ビジュアル（A案・FREEDOM風） -----
	// 共通方式：ローカル空間でメッシュを1回生成し、SetRotate で発射方向へ向け、SetScale.z で伸長（再生成なし）。
	// 発射ビーム（固定方向の一閃。Slash 中に筒先→全長へ伸びる transient）
	std::unique_ptr<class EffectPrimitiveRenderer> disruptorBeam_;
	Vector3 disruptorBeamDir_{ 0.0f, 0.15f, 1.0f };  // 固定発射方向（ワールド。既定=前方やや上＝戦場へ）。狙い角度とは無関係
	float   disruptorBeamLength_  = 80.0f;           // ビーム全長
	float   disruptorBeamWidth_   = 0.8f;            // ビーム幅（startWidth）
	Vector4 disruptorBeamColor_{ 0.7f, 0.5f, 1.0f, 1.0f }; // 紫
	float   disruptorBeamRunTime_ = 0.12f;           // 筒先→全長へ伸びる時間（Slash 内・決定的）
	float   disruptorBeamTimer_   = 0.0f;            // ランタイム：伸長経過秒
	Vector3 disruptorBeamFireDir_{ 0.0f, 0.0f, 1.0f };// ランタイム：確定発射方向
	Vector3 disruptorBeamMuzzle_{ 0.0f, 0.0f, 0.0f }; // ランタイム：筒先位置
	// 衝撃波（筒先の加算リング。Slash 中に拡大＋αフェード）
	std::unique_ptr<class EffectPrimitiveRenderer> disruptorShockwave_;
	float   disruptorShockRadius_    = 6.0f;         // 最大外半径
	float   disruptorShockThickness_ = 0.4f;         // リング太さ（0..1、outer に対する内側の食い込み）
	Vector4 disruptorShockColor_{ 0.8f, 0.6f, 1.0f, 1.0f };
	float   disruptorShockTime_   = 0.25f;           // 拡大時間
	float   disruptorShockTimer_  = 0.0f;            // ランタイム
	// 断裂線ビジュアル（細い Beam・仮。Collapse 中に P1[右]→P2[左] へ走り込む）
	std::unique_ptr<class EffectPrimitiveRenderer> disruptorRift_;
	// 案1：見た目を「判定帯（disruptorLineWidthPx_ 半幅px）」に一致させる。焼き付け深度で px→world 換算した
	// 半幅(disruptorRiftBakedHalfWidth_)をビーム全幅に使い、scale で微調整（1.0=判定帯ぴったり）。
	float   disruptorRiftWidthScale_   = 1.0f;       // 判定帯に対する太さ倍率
	float   disruptorRiftBakedHalfWidth_ = 0.0f;     // ランタイム：焼き付け時に確定した world 半幅
	Vector4 disruptorRiftColor_{ 0.6f, 0.4f, 1.0f, 1.0f };
	float   disruptorRiftRevealTime_ = 0.45f;        // 右→左へ走り込む時間（Collapse 内・決定的）
	float   disruptorRiftTimer_   = 0.0f;            // ランタイム
	// 遅延キル：判定は Slash で収集し、断裂線が引き切った瞬間に一括キル（線＝判定の対応を見せる）
	std::vector<IImGuiEditable*> disruptorPendingEnemies_;     // 収集した敵本体（Enemy/Boss）
	std::vector<IImGuiEditable*> disruptorPendingBulletPrims_; // 収集した敵弾の primitive ポインタ
	bool disruptorKillsDone_ = false;                          // この一閃のキルを実行済みか

	// ----- Step6: 崩壊リビール＋色反転（PostEffect の DisruptorReveal を駆動）-----
	// Collapse 中は World 停止＝ライブ／キャプチャ同一フレームなので、現フレームを
	// 断裂線の領域マスクで反転（殻）／通常（剥がれた下の世界）に出し分けるだけで2層リビールが成立する。
	// Slash=全画面反転(revealT=0) → Collapse で線から上下へ通常色が伝播(revealT 0→1) → Recover で OFF。
	float disruptorRevealIntensity_    = 1.0f;   // 反転の強さ（1=完全反転）
	// Collapse 入りからこの秒数は revealT=0（全画面反転＝断裂線が走り切るまで殻のまま）。
	// 以後 [delay, disruptorCollapseDuration_] 区間で revealT 0→1（ゆっくり剥がれて下の通常色が戻る）。
	float disruptorRevealStartDelay_   = 0.45f;
	// 画面がこの割合まで通常色に戻ったら崩壊終了＝後隙(World再開)へ。破片は生かしたまま。
	float disruptorCollapseEndAt_      = 0.9f;
	float DisruptorCollapseRevealT() const; // Collapse の割れ進捗 0..1（smoothstep 済み・セルの割れ駆動に使用）
	void  UpdateDisruptorReveal();               // 反転シェーダの ON/OFF（Slash 全反転＋Collapse ブリッジ全反転のみ）

	// ----- 破片レンダラ（反転世界の殻のかけら）-----
	// セル方式：未割れセル＝静止した反転シャードで“殻”、割れたセルは飛散（F3）。描画は DrawDisruptorFragments。
	std::unique_ptr<class DisruptorShardRenderer> disruptorShards_; // セル描画（キャプチャ反転）
	bool  disruptorCaptureDone_ = false;       // この崩壊でシーンキャプチャ済みか
	// 見た目チューニング（殻・破片共通）
	float   disruptorFragSpin_      = 6.0f;    // 破片の回転速度の上限(rad/s)
	float   disruptorFragMinScale_  = 0.06f;   // 破片サイズ(1=発生時)がこの比まで縮んだら完全に消す
	float   disruptorFragAlpha_     = 1.0f;    // 殻・破片の不透明度（1=反転スクリーンをそのまま貼る）
	float   disruptorFragSatBoost_  = 1.6f;    // 反転色の彩度ブースト（1=そのまま, >1で色を強調）
	static constexpr uint32_t kDisruptorShardCap_ = 2048; // 破片レンダラの per-cell 描画上限（Cell Count 上限以上に取る）
	void DrawDisruptorFragments();                // 最終 RT へ描画（PostEffect 後＝二重反転回避。未割れ＝殻／割れ＝飛散）

	// ----- F1: 事前分割セル（手続き Voronoi）＋割れ順プレビュー -----
	// Slash→Collapse でスクリーン空間に種点を撒き、半平面クリップで各セルの凸多角形を作る（事前分割）。
	// 種点はスクリーン空間固定＝エイム角θに非依存。同じ seed で同じ割れ方が再現される。
	// 割れ順は「重心の切断線からの垂直距離」で決まり、リビール境界が来た片から順に割れる（飛散駆動はF3）。
	struct DisruptorCell {
		std::vector<Vector2> polyUV;          // セル凸多角形の頂点（スクリーンUV [0,1]）
		Vector2 centroidUV{ 0.5f, 0.5f };     // 重心（スクリーンUV）
	};
	std::vector<DisruptorCell> disruptorCells_;
	uint32_t disruptorFractureSeed_     = 1u;     // 割り方のシード（同値＝同じ割れ方）
	bool     disruptorFractureSeedLock_ = false;  // true: 発動ごとにこの seed を使う / false: 毎回ランダム（使った値は seed に残す）
	int      disruptorCellCount_        = 100;    // セル数（種点数）
	// 割れ順プレビュー（デバッグ）
	bool     disruptorCellDebugDraw_      = false; // セル境界線を LineRenderer で表示
	float    disruptorCellPreviewRevealT_ = 0.0f;  // 手動スクラブ：この revealT まで割れたとみなして色分け
	void BuildDisruptorCells();             // seed から種点スキャッタ→半平面クリップ→重心。disruptorCells_ を作る
	void DrawDisruptorCellBordersDebug();   // セル境界線を LineRenderer へ（割れ順で色分け＋スクラブ）

	// ----- F2: セルのワールド形状アップロード＋静止描画（baked UV 検証）-----
	std::vector<Vector3> disruptorCellCentroidWorld_; // 各セル重心のワールド配置（rest）。F3 の飛散基準にも使う
	bool disruptorCellMeshUploaded_ = false;          // この崩壊で SetCells 済みか
	bool disruptorCellStaticDraw_   = false;          // F2: 割らずに全セルを静止描画（マッピング確認用）
	void BuildDisruptorCellMeshesAndUpload();         // disruptorCells_ → ワールド三角形 → renderer SetCells（崩壊中1回）

	// ----- F3: 割れ駆動（リビール境界が来たセルから順に飛散）-----
	struct DisruptorCellRuntime {
		bool    broken    = false;       // 既に割れて飛んだか
		float   age       = 0.0f;        // broken からの経過秒
		float   lifeDur   = 0.7f;        // 寿命（秒）
		float   breakDist = 0.0f;        // 切断線からの垂直距離（割れ順の閾値、build 時に確定）
		Vector3 vel{ 0.0f, 0.0f, 0.0f }; // 飛散速度（world）
		Vector3 spin{ 0.0f, 0.0f, 0.0f };// 回転速度（rad/s）
		Vector3 rot{ 0.0f, 0.0f, 0.0f }; // 現在回転
		Vector3 offset{ 0.0f, 0.0f, 0.0f }; // 重心からの累積変位（world）
	};
	std::vector<DisruptorCellRuntime> disruptorCellRuntime_;
	float disruptorCellMaxBreakDist_ = 1.0f; // 割れ閾値の正規化用（全画面を覆う最大垂直距離・build 時に確定）
	// チューニング（飛散）。spin 上限/alpha/satBoost は既存 disruptorFrag* を流用。
	float disruptorBreakFreeze_   = 0.12f; // 割れた直後この秒数は静止（バリン）
	float disruptorBreakPopSpeed_ = 6.0f;  // 視点方向へ飛び出す速さ
	float disruptorBreakSpread_   = 4.0f;  // ランダム拡散の速さ
	float disruptorBreakGravity_  = 6.0f;  // 下方向（-Y）の重力加速
	float disruptorBreakLifeMin_  = 0.5f;
	float disruptorBreakLifeMax_  = 1.0f;
	void UpdateDisruptorCells(float realDt); // 境界進行でセルを順に割り、飛散・回転・フェードを進める

	void EnterDisruptor();                       // TriggerSpecialMove(Disruptor) から：Charge へ
	void UpdateDisruptorMove(class InputActionMap* actions, float realDt); // 専用フェーズ機の更新
	void EnterDisruptorPhase(DisruptorPhase p);  // フェーズ遷移＋World時間/無敵の切替
	float DisruptorAngleFromReticle() const;     // 画面中央→レティクルの角度(rad)
	void  DrawDisruptorAimLine();                // 切断線を LineRenderer(3D) へ積む（スクリーン端まで）
	void  ExecuteDisruptorSlash();               // 一閃：線上の敵/敵弾を「収集」＋切断線焼き付け＋発射ビーム生成（キルは遅延）
	void  ExecuteDisruptorKills();               // 収集した対象を一括キル（断裂線が引き切った瞬間に呼ぶ）
	void  ApplyDisruptorCamera();                // Step5-A：フェーズ別の目標カメラ姿勢を補間して適用
	void  BuildDisruptorFireVisuals();           // Step5-B：Slash 入りで発射ビーム＋衝撃波を生成
	void  BuildDisruptorRift();                  // Step5-B：Collapse 入りで断裂線ビームを生成
	void  UpdateDisruptorVisuals(float realDt);  // Step5-B：ビーム伸長／衝撃波／断裂走り込みのアニメ＋Update
	void  TrashDisruptorVisual(std::unique_ptr<class EffectPrimitiveRenderer>& r); // 遅延削除キューへ移す

	void InitializeSpecialGaugeUI();
	void UpdateSpecialGaugeUI();
	void UpdateSpecialMove(class InputActionMap* actions, float dt);
	// 種別ごとのゲージ上限を返す
	float SpecialGaugeMaxFor(SpecialKind k) const {
		return (k == SpecialKind::Disruptor) ? specialGaugeMaxDisruptor_ : specialGaugeMaxGouman_;
	}
	void SetEquippedSpecial(SpecialKind k); // 装備切替＋specialGaugeMax_ ミラー更新＋ゲージクランプ
	void TryTriggerSpecial(SpecialKind k);  // クールタイム/ゲージ条件を満たせば発動
	void TriggerSpecialMove();              // 装備中種別を発動（無条件・ImGuiボタン/内部用）
	void TriggerSpecialMove(SpecialKind k); // 種別を指定して発動（種別で分岐）
	void EnterSpecialPhaseBarrier();
	void UpdateSpecialPhaseBarrier(float worldDt);
	void UpdateSpecialBarrier();   // バリア球のプレイヤー追従＋球内の敵弾消去（全フェーズ共通）
	void DrawSpecialBarrierWireframe(const Vector3& center); // 回転ワイヤーフレーム球を LineRenderer へ積む
	void UpdateSpecialWings();     // 光の翼：X字方向に小パーティクルを外向き噴出
	void EnterSpecialPhaseLockon();
	void UpdateSpecialPhaseLockon(float realDt);
	void EnumerateLockonTargets();   // 画面内敵+敵弾を列挙してプレイヤー近い順にソート
	void SpawnLockonVisuals(SpecialLockonTarget& t);
	void UpdateLockonTargetVisuals(SpecialLockonTarget& t, float localTime);
	void EnterSpecialPhaseFire();
	void UpdateSpecialPhaseFire(float dt);
	void EnterSpecialPhaseEnd();
	void UpdateSpecialPhaseEnd(float dt);
	bool IsSpecialTargetAlive(IImGuiEditable* e) const;          // 動的コンテナにポインタが生存しているか
	void CollectScreenTargets(std::vector<std::pair<IImGuiEditable*, bool>>& out); // 画面内の敵/敵弾を列挙(entity,isBullet)
	Vector3 SpecialBoltStart(const Vector3& playerPos, const Vector3& dir) const;  // プレイヤーカプセル表面の始点（楕円射影）
	Vector3 SpecialPlayerCenter() const;  // 必殺技演出の中心（足元 + offset up = 体の中心）
	void EndSpecialMove();
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
