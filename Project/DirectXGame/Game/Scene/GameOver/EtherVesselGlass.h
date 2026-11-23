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

	/// <summary>LiquidGlassSphere を作り、球の形（EtherVesselGeometry.json）と調整値を読む</summary>
	/// <param name="dxCore">DirectX の中核</param>
	/// <param name="srvManager">SRV の管理</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager);
	/// <summary>球の中心・半径・傾きを反映して球を進める</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);
	/// <summary>波のシミュレーションを進める。シーンの Draw の最初に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>ガラスの球と中のエーテルを描く。不透明物（器のモデル）を描いた後に呼ぶ（ルートシグネチャを替える）</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="environmentCubemapPath">映り込みに使う環境マップのパス</param>
	void Draw(const Camera& camera, const std::string& environmentCubemapPath);
	/// <summary>発光パスで描く（シーンの DrawBloomSources から）</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="environmentCubemapPath">映り込みに使う環境マップのパス</param>
	void DrawBloom(const Camera& camera, const std::string& environmentCubemapPath);

	/// <summary>器の原点（土台の脚の下）を設定する</summary>
	/// <param name="origin">ワールド位置</param>
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>量を設定する</summary>
	/// <param name="amount">体積の割合 0〜1</param>
	/// <param name="immediate">false なら少し遅れて寄る</param>
	void SetFill(float amount, bool immediate = false);
	/// <summary>球の傾きを設定する</summary>
	/// <param name="radians">傾き [ラジアン]。+ で I の側＝+X へ針が倒れる</param>
	void SetTilt(float radians) { tilt_ = radians; }
	/// <summary>球の傾きを返す</summary>
	/// <returns>傾き [ラジアン]</returns>
	float GetTilt() const { return tilt_; }
	/// <summary>Swing（C の字＋針）に渡す Z 軸まわりの回転を返す（エンジンの Z 回転は + で上が −X へ倒れるので、傾きの符号を逆にする）</summary>
	/// <returns>回転 [ラジアン]</returns>
	float GetRotationZ() const { return -tilt_; }
	/// <summary>難易度を光の強さ（glowAtMin〜glowAtMax）に変える</summary>
	/// <param name="difficulty01">難易度 0〜1</param>
	void SetDifficulty(float difficulty01);
	/// <summary>光の強さに掛ける倍率を設定する（もう一度を選んだときに光を強める）</summary>
	/// <param name="boost">倍率（1＝難易度のまま）</param>
	void SetGlowBoost(float boost) { glowBoost_ = boost; ApplyGlow(); }
	/// <summary>液面に波を立てる（中心の真上）</summary>
	/// <param name="strength">波の強さ</param>
	void Splash(float strength);

	/// <summary>球の中心を返す</summary>
	/// <returns>ワールド位置</returns>
	Vector3 GetSphereCenter() const;
	/// <summary>球の半径を返す</summary>
	/// <returns>半径 [m]</returns>
	float GetSphereRadius() const { return sphereRadius_; }
	/// <summary>描画を受け持つ LiquidGlassSphere を返す</summary>
	/// <returns>球。Initialize 前は nullptr</returns>
	LiquidGlassSphere* GetSphere() const { return sphere_.get(); }

	/// <summary>調整値を EtherVesselGlass.json から読む</summary>
	/// <returns>読めたら true。ファイルが無い・読めなければ false</returns>
	bool LoadTuning();
	/// <summary>調整値を EtherVesselGlass.json へ書く</summary>
	void SaveTuning() const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

private:
	/// <summary>EtherVesselGeometry.json の "sphere" から球の中心と半径を読む（読めなければ既定値のまま）</summary>
	void LoadGeometry();
	/// <summary>難易度と倍率から光の強さを求めて球へ渡す</summary>
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
