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

	// ----- ジャスト回避演出 -----
	bool  justDodgeActive_ = false;
	float justDodgeTimer_ = 0.0f;
	float justDodgeDuration_ = 1.5f;
	float justDodgeFadeIn_ = 0.15f;
	float justDodgeFadeOut_ = 0.3f;
	IImGuiEditable* justDodgeTarget_ = nullptr;

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
