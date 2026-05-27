#pragma once
#include "BaseScene.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Vector4.h"
#include "Wave/WaveDef.h"
#include "Effect/EffectManager.h"
#include <memory>
#include <vector>

class Camera;
class Skybox;
class SplineCurveActor;
class RailCameraController;
class AnimatedObject3DInstance;
class Reticle;

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
	bool IsActionLocked() const { return meleeActionLockTimer_ > 0.0f || dodgeActionLockTimer_ > 0.0f || jdSelecting_; }
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

	// 必殺技ゲージ（UI 未実装。ゲージ本体だけ先に持つ）
	float specialGauge_         = 0.0f;   // 現在値（0..specialGaugeMax_）
	float specialGaugeMax_      = 100.0f; // 上限
	float dodgeSpecialGaugeGain_ = 8.0f;  // 追加回避1回ぶんの加算量

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
};
