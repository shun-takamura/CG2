#pragma once
#include "BaseScene.h"
#include "Vector2.h"
#include "Vector3.h"
#include "Wave/WaveDef.h"
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
	float   railCameraSpeed_ = 0.05f;                  // RailCamera の進行速度（t/秒）
	Vector2 playerMoveSpeed_{ 5.0f, 5.0f };            // 入力1秒あたりのオフセット移動量（カメラ空間X/Y）
	Vector2 playerClipMargin_{ 0.1f, 0.1f };           // クリップ空間で許す画面外マージン（X/Y）
	float   playerSmoothTime_ = 0.15f;                 // 慣性の指数減衰時定数（秒）：小さい=反応速い

	// ----- 射撃チューニング -----
	float bulletSpeed_    = 80.0f;   // 弾速 [units/sec]
	float bulletLifetime_ = 2.0f;    // 寿命 [sec]
	float fireRate_       = 0.12f;   // 連射間隔 [sec]
	float fireTimer_      = 0.0f;    // 次に撃てるまでの残り秒（ランタイム）
	float bulletColliderGrowth_ = 0.02f; // 進行 1m あたりの collider 半径拡大量
	float bulletHomingStrength_ = 1.5f;   // 軽ホーミング（target 方向への指数収束 [/sec]）
	float bulletHomingLockOnBoost_ = 4.0f; // ロックオン中の弾は強めに引き寄せる（合計値）

	// ----- 照準（aim）チューニング -----
	// カメラからの「狙いの面」までの距離。弾速・寿命とは独立。
	// 非ロックオン時、レティクル方向のレイがこの面と交わる点を target にする。
	// 値を増やすほど画面外の敵にもレティクルが効く（弾は当然そこまで届くとは限らない）。
	float aimPlaneDistance_ = 80.0f;
	float aimSmoothTime_   = 0.08f;   // Lerp 時定数（プレイヤー回転用、0.0=即時）
	float aimAssistPixelScale_ = 1.4f; // 見かけ半径×倍率＝スクリーン上のロックオン許容ピクセル
	// ランタイム状態
	Vector3 aimTarget_{ 0.0f, 0.0f, 0.0f };       // Lerp 済み（プレイヤー回転用）
	Vector3 firingTarget_{ 0.0f, 0.0f, 0.0f };    // 即時（弾の発射方向用）
	IImGuiEditable* lockedEnemy_ = nullptr;        // 現フレームのロックオン対象（ホーミング元・強）
	IImGuiEditable* nearestEnemy_ = nullptr;       // 画面上で最近の敵（軽ホーミング先）
	bool    aimInitialized_ = false;

	// ----- ウェーブ -----
	WaveDef currentWave_;
	std::vector<bool>  waveFired_;     // entries と同じサイズ。発火済みフラグ
	// entries と同じサイズ。そのエントリから生まれた敵が倒された時刻（秒）。
	// 負数 = まだ倒されていない。Seek で「kill 時刻 > T」なら未死亡扱いに戻る。
	std::vector<float> waveKillTime_;

	// 入力で加算するオフセット / 慣性用速度（ランタイムのみ、JSON 非保存）
	Vector2 playerInputOffset_{ 0.0f, 0.0f };
	Vector2 playerVelocity_{ 0.0f, 0.0f };

	// レティクル
	std::unique_ptr<Reticle> reticle_;

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
};
