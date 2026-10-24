#pragma once
#include "Vector3.h"
#include <memory>
#include <string>

class DirectXCore;
class SRVManager;
class Camera;
class LiquidGlassSphere;

/// <summary>
/// エーテルの器のガラスの球と中のエーテル（19_GameOverAndDifficulty.md §4・G5）。
/// 描画はエンジンの LiquidGlassSphere。ここは器としての値の受け渡しと調整値（JSON・ImGui）を持つ。
///   - 球の中心と半径は G4 の EtherVesselGeometry.json（gen_ether_vessel.py が書く）の "sphere"
///   - 量（SetFill）＝賭け分。体積の割合なので、球の曲面で高さと量は比例しない（§4.4）
///   - 傾き（SetTilt）＝難易度の針と同じ。球は中心でエンジンの Z 軸まわりに回る（Swing と同じ）
///   - 光の強さ（SetDifficulty）＝難易度。高いほど明るい
/// 置き場所は SetOrigin（器の土台の脚の下）。
/// </summary>
class EtherVesselGlass {
public:
	EtherVesselGlass();
	~EtherVesselGlass();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
	void Update(float deltaTime);
	/// <summary>波のシミュレーションを進める。シーンの Draw の最初に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>不透明物（器のモデル）を描いた後に呼ぶ。ルートシグネチャを替える</summary>
	void Draw(const Camera& camera, const std::string& environmentCubemapPath);
	/// <summary>シーンの DrawBloomSources から</summary>
	void DrawBloom(const Camera& camera, const std::string& environmentCubemapPath);

	/// <summary>器の原点（土台の脚の下）のワールド位置</summary>
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>量（0..1）。immediate=false なら少し遅れて寄る</summary>
	void SetFill(float amount, bool immediate = false);
	/// <summary>球の傾き [rad]。+ で I の側＝+X へ針が倒れる</summary>
	void SetTilt(float radians) { tilt_ = radians; }
	float GetTilt() const { return tilt_; }
	/// <summary>
	/// Swing（C の字＋針）に渡す Z 軸まわりの回転 [rad]。エンジンの Z 回転は + で上が −X へ倒れるので、傾きの符号を逆にする
	/// </summary>
	float GetRotationZ() const { return -tilt_; }
	/// <summary>難易度 0..1 → 光の強さ（glowAtMin〜glowAtMax）</summary>
	void SetDifficulty(float difficulty01);
	/// <summary>光の強さに掛ける倍率（1＝難易度のまま。もう一度を選んだときに光を強める）</summary>
	void SetGlowBoost(float boost) { glowBoost_ = boost; ApplyGlow(); }
	/// <summary>液面に波を立てる（中心の真上）</summary>
	void Splash(float strength);

	/// <summary>球の中心（ワールド）</summary>
	Vector3 GetSphereCenter() const;
	float GetSphereRadius() const { return sphereRadius_; }
	LiquidGlassSphere* GetSphere() const { return sphere_.get(); }

	bool LoadTuning();
	void SaveTuning() const;
	void OnImGui();

private:
	void LoadGeometry();
	void ApplyGlow();

	std::unique_ptr<LiquidGlassSphere> sphere_;
	Vector3 origin_{};
	// G4 の EtherVesselGeometry.json の値（読めなければこの既定値）
	Vector3 sphereCenter_{ 0.0f, 2.8591f, 0.0f };
	float sphereRadius_ = 0.28f;

	float tilt_ = 0.0f;
	float difficulty_ = 0.5f;
	float glowAtMin_ = 0.15f;   // 難易度 0 の光の強さ
	float glowAtMax_ = 0.45f;   // 難易度 1 の光の強さ（難易度 0.5 で 0.3＝2026-10-24 ユーザーが決めた値）
	float splashRadius_ = 0.03f;
	float glowBoost_ = 1.0f;
};
