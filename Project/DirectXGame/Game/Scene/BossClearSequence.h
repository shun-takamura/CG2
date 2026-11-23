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

	/// <summary>撃破の段取りを① ヒットストップから始める（このフレームを撮る知らせも立てる）</summary>
	/// <param name="bossCenter">ボスの体の中心</param>
	/// <param name="groundY">カメラを潜らせない床の高さ [m]</param>
	/// <param name="camera">今のカメラ（寄る前の目の位置と向きを覚える）</param>
	/// <param name="clearTimeSec">STAGE CLEAR の下に出すクリアタイム [秒]</param>
	void Begin(const Vector3& bossCenter, float groundY, const Camera& camera, float clearTimeSec);
	/// <summary>段取りを止めて Idle に戻す（溜まった知らせと爆発も捨てる）</summary>
	void Reset();
	/// <summary>段を進め、スロー中の小さな爆発と崩壊の爆発を予約する</summary>
	/// <param name="uiDt">実時間（UI 時間）の経過 [秒]</param>
	void Update(float uiDt);

	/// <summary>段取りの最中か</summary>
	/// <returns>Idle 以外なら true</returns>
	bool IsActive() const { return stage_ != Stage::Idle; }
	/// <summary>今の段を取得</summary>
	/// <returns>今の段</returns>
	Stage GetStage() const { return stage_; }
	/// <summary>HUD（HP バー・レティクル・スコア・拾った物の表示・自機のゲージ）を描かないか</summary>
	/// <returns>段取りの最中なら true</returns>
	bool IsHudHidden() const { return IsActive(); }
	/// <summary>世界（TimeGroup::World）の時間の倍率</summary>
	/// <returns>ヒットストップ中 0、スロー中は slowScale_、崩壊からは 1.0 へなめらかに戻した値</returns>
	float GetWorldTimeScale() const;

	// ----- ホストへの知らせ（1 度だけ true）-----
	/// <summary>Begin のフレームの撮影の知らせを受け取る（UI を描かずにポストエフェクトの後の絵を撮る）</summary>
	/// <returns>撮るフレームなら true（1 度だけ）</returns>
	bool ConsumeCaptureRequest();
	/// <summary>③ の始まりの知らせを受け取る（ボスを崩して破棄する。死亡エフェクトも）</summary>
	/// <returns>崩壊の始まりなら true（1 度だけ）</returns>
	bool ConsumeCollapse();
	/// <summary>時刻の来た爆発を 1 つ取り出す</summary>
	/// <param name="outPos">爆発の位置</param>
	/// <param name="outEffect">出すエフェクト名（小さな爆発か大きな爆発）</param>
	/// <returns>取り出せたら true</returns>
	bool PopBurst(Vector3& outPos, std::string& outEffect);
	/// <summary>段取りの終わり（リザルトへ移る）の知らせを受け取る</summary>
	/// <returns>終わったなら true（1 度だけ）</returns>
	bool ConsumeFinished();

	/// <summary>ボスの斜め前へ寄るカメラ（揺れも込み）を上書きする。ホストのカメラの上書きの最後に呼ぶ</summary>
	/// <param name="camera">上書きするカメラ</param>
	void ApplyCamera(Camera& camera) const;
	/// <summary>白いフラッシュと STAGE CLEAR を描く。TextRenderer に積むだけ（Flush は呼び出し側）</summary>
	/// <param name="screenW">画面の幅 [px]</param>
	/// <param name="screenH">画面の高さ [px]</param>
	void DrawOverlay(float screenW, float screenH) const;

	/// <summary>段取りの時間・カメラ・揺れ・爆発の調整 UI（Debug のみ中身あり）</summary>
	void OnImGui();

private:
	struct PendingBurst { float at; Vector3 pos; bool big; };

	/// <summary>0〜1 の乱数（xorshift32）</summary>
	/// <returns>0 以上 1 以下の値</returns>
	float Rand01();
	/// <summary>ボスの中心のまわりの乱数の位置</summary>
	/// <param name="radius">水平の半径 [m]</param>
	/// <param name="height">上下に散らす幅 [m]</param>
	/// <returns>ボスの中心から水平 radius 以内・上下 ±height の位置</returns>
	Vector3 RandomAround(float radius, float height);
	/// <summary>今のカメラの揺れの大きさ（ヒットストップ中と崩壊の直後）</summary>
	/// <returns>揺れの振幅 [m]</returns>
	float ShakeAmplitude() const;
	/// <summary>寄った構図の目と見る点（寄った後はゆっくり回り、崩壊の後は引く）</summary>
	/// <param name="outEye">目の位置</param>
	/// <param name="outLook">見る点</param>
	/// <param name="t">Begin からの実時間 [秒]</param>
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
