#pragma once
#include "Vector3.h"

class Camera;
class JsonValue;

/// <summary>
/// 雲の上の場面（16_StageStart.md S4、F6）：雲の上でゲームの自機を右上前方から見るカメラ → 自機の右側を回り込んで
/// レールのカメラ（TPS）へ合流する。2026-10-19 ユーザーのスケッチ（§3.3 の「左上」から変更）。
/// レールはこの間も t=0 から進む（§1 の既定）。毎フレーム、レールのカメラ（位置・向き・画角）が決まり、自機をその前に置いた後で
/// Apply を呼ぶと、カメラをレールのカメラの向きの空間で回り込ませて上書きする。終わりの姿勢はレールのカメラと同じなので切れ目は出ない。
/// 操作を受け付けるのは回り込みが終わってから（IsActive の間はシーンが入力を止める）。
/// 調整値は StagePlay.json の "cloudTopIntro"。
/// </summary>
class CloudTopIntro {
public:
	void Begin();
	void Cancel() { active_ = false; }
	bool IsActive() const { return active_; }
	float GetElapsed() const { return elapsed_; }
	float GetTotalSeconds() const;

	/// <summary>
	/// 時間を進め、camera（今はレールのカメラ）を回り込みのカメラで上書きして camera->Update() する。
	/// focus＝自機の位置（ワールド）。終わったフレームは上書きしない（そのままレールのカメラ）
	/// </summary>
	void Apply(Camera* camera, const Vector3& playerWorldPos, float deltaTime);
	/// <summary>Apply の前のレールのカメラの姿勢へ戻す／回り込みのカメラへ戻す（レールのカメラで決める物＝敵の出現位置の間だけ）</summary>
	void UseRailPose(Camera* camera) const;
	void UseIntroPose(Camera* camera) const;
	/// <summary>レールのカメラの向き（自機をレールの前へ向ける）</summary>
	const Vector3& GetRailRotate() const { return railRotate_; }
	/// <summary>雲の上に出てから羽根の展開を始めるまで [s]</summary>
	float GetWingDeployDelay() const { return wingDeployDelay_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

private:
	bool active_ = false;
	bool applied_ = false;   // このフレームに上書きしたか（UseRailPose / UseIntroPose）
	float elapsed_ = 0.0f;
	// このフレームのレールのカメラと回り込みのカメラ
	Vector3 railEye_ = { 0.0f, 0.0f, 0.0f };
	Vector3 railRotate_ = { 0.0f, 0.0f, 0.0f };
	float railFovY_ = 0.45f;
	Vector3 introEye_ = { 0.0f, 0.0f, 0.0f };
	Vector3 introRotate_ = { 0.0f, 0.0f, 0.0f };
	float introFovY_ = 0.45f;

	// 調整値。方位・仰角はレールのカメラの向きの空間で、自機（注視点）から見た角度（方位 0＝前、正＝右、180＝後ろ）
	float holdSeconds_ = 0.6f;          // 始めの構えのまま（羽根が開き始めるのを見せる）
	float orbitSeconds_ = 3.0f;         // 回り込んでレールのカメラへ合流するまで
	float startAzimuthDeg_ = 35.0f;     // 右上前方
	float startElevationDeg_ = 20.0f;
	float startDistance_ = 6.0f;        // 注視点からの距離 [m]
	float startFovDeg_ = 50.0f;
	bool orbitRight_ = true;            // 自機の右側を回る（false で左側）
	float focusHeight_ = 0.9f;          // 注視点＝自機の原点からレールのカメラの上方向へ [m]（胴の高さ）
	float lookBlendStart_ = 0.55f;      // 回り込みのこの割合から、向きを注視点からレールのカメラの向きへ移す
	float wingDeployDelay_ = 0.2f;      // 雲の上に出てから羽根を開き始めるまで
};
