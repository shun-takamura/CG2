#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "Vector3.h"
#include "StageClearBanner.h"

class Camera;

/// <summary>
/// ボス撃破からリザルトへ移るまでの段取り（23_StageClear.md §4 B）。時間はすべて実時間（UI 時間）で進める。
///   ① ヒットストップ（世界を止める・小さく揺れる・白く一瞬光る）。この最初のフレームを UI なしで撮る
///   ② スロー：カメラがボスの斜め前へ寄る・ボスの体のあちこちで小さな爆発
///   ③ 崩壊：手足が落ち、本体で大きな爆発＋周りに数発・大きく揺れる・スローを 1.0 へ戻す
///   ④ STAGE CLEAR の表示 → しばらく見せてリザルトへ
/// 演出の間は HUD を描かない。ゲームの状態（ボスの破棄・エフェクトの再生・世界の時間）はホストが
/// Consume / Pop の結果を見て行う（このクラスはシーンに触らない）。
/// </summary>
class BossClearSequence {
public:
	enum class Stage { Idle, HitStop, Slow, Collapse, Banner, Done };

	// bossCenter はボスの体の中心、groundY はカメラを潜らせない床の高さ
	void Begin(const Vector3& bossCenter, float groundY, const Camera& camera, float clearTimeSec);
	void Reset();
	void Update(float uiDt);

	bool IsActive() const { return stage_ != Stage::Idle; }
	Stage GetStage() const { return stage_; }
	// HUD（HP バー・レティクル・スコア・拾った物の表示・自機のゲージ）を描かない
	bool IsHudHidden() const { return IsActive(); }
	// 世界（TimeGroup::World）の時間の倍率
	float GetWorldTimeScale() const;

	// ----- ホストへの知らせ（1 度だけ true）-----
	bool ConsumeCaptureRequest();   // Begin のフレーム：UI を描かずにポストエフェクトの後の絵を撮る
	bool ConsumeCollapse();         // ③ の始まり：ボスを崩して破棄する（死亡エフェクトも）
	bool PopBurst(Vector3& outPos, std::string& outEffect); // 爆発を 1 つ出す
	bool ConsumeFinished();         // リザルトへ

	// 寄るカメラ（揺れも込み）。ホストのカメラの上書きの最後に呼ぶ
	void ApplyCamera(Camera& camera) const;
	// 白いフラッシュと STAGE CLEAR。TextRenderer に積むだけ（Flush は呼び出し側）
	void DrawOverlay(float screenW, float screenH) const;

	void OnImGui();

private:
	struct PendingBurst { float at; Vector3 pos; bool big; };

	float Rand01();
	Vector3 RandomAround(float radius, float height);
	float ShakeAmplitude() const;
	void ComputeShot(Vector3& outEye, Vector3& outLook, float t) const;

	Stage stage_ = Stage::Idle;
	float time_ = 0.0f;        // Begin からの実時間
	float stageTime_ = 0.0f;   // 今の段に入ってから
	bool  captureRequest_ = false;
	bool  collapseEvent_ = false;
	bool  finishedEvent_ = false;

	Vector3 bossCenter_{};
	float   groundY_ = 0.0f;
	Vector3 startEye_{};
	Vector3 startRot_{};       // カメラの回転（pitch, yaw, roll）
	Vector3 shotDir_{ 0.0f, 0.0f, -1.0f }; // ボスから寄った目の水平の向き
	float   collapseTime_ = -1.0f; // 崩壊した時刻（time_）
	float   clearTime_ = 0.0f;     // STAGE CLEAR の下に出すクリアタイム [s]

	float burstTimer_ = 0.0f;
	std::vector<PendingBurst> pending_;
	uint32_t rng_ = 0x1234567u;

	StageClearBanner banner_;

	// ----- 調整値 -----
	float hitStopTime_ = 0.15f;
	float slowTime_    = 1.6f;
	float slowScale_   = 0.25f;
	float restoreTime_ = 0.8f;   // 崩壊からスローを 1.0 へ戻すまで
	float bannerDelay_ = 1.0f;   // 崩壊から STAGE CLEAR まで
	float bannerHold_  = 3.0f;   // STAGE CLEAR を出してからリザルトまで
	// フラッシュ
	float flashTime_  = 0.18f;
	float flashAlpha_ = 0.55f;
	float collapseFlashAlpha_ = 0.35f;
	// カメラ
	float shotDistance_  = 9.0f;
	float shotHeight_    = 1.5f;   // ボスの中心からの目の高さ
	float shotYawOffset_ = 0.6f;   // 今のカメラの向きから回り込む角度 [rad]
	float lookUp_        = 0.3f;   // 見る点をボスの中心から上げる
	float orbitSpeed_    = 0.06f;  // 寄った後にゆっくり回る [rad/s]
	float pullBack_      = 5.0f;   // 崩壊の後に引く距離
	float pullBackTime_  = 2.0f;
	float minEyeAboveGround_ = 0.6f;
	// 揺れ
	float hitStopShake_  = 0.12f;
	float collapseShake_ = 0.6f;
	float collapseShakeTime_ = 0.8f;
	// 爆発
	std::string smallBurstEffect_ = "Death_Drone";
	std::string bigBurstEffect_   = "Explosion_00";
	float burstInterval_ = 0.22f;  // 小さな爆発の間隔（実時間）
	float burstRadius_   = 2.2f;
	float burstHeight_   = 1.8f;
	int   collapseBurstCount_ = 5;
	float collapseBurstSpan_  = 0.45f; // 周りの爆発を出し終えるまで
	float collapseBurstRadius_ = 3.2f;
};
