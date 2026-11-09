#pragma once
#include <memory>
#include <string>

class DirectXCore;
class SRVManager;
class Object3DManager;
class Camera;
class WaterSurface;
struct Vector3;

/// <summary>
/// ボス戦の闘技場の水：
///   - 足場の床：石畳（タイトルの水底と同じ素材）の上に薄く張った水。石畳は WaterSurface の水底として
///     シェーダ内で描く。床（石畳）の高さ = groundY、水面 = groundY + 水深。縁石の内側の円だけ描く
///     （足場の側面・縁石・柱は BossArena モデル＝gen_boss_arena.py）。
///   - 海：Y = seaY の深い水。カメラについて回り、海岸より手前（clipMinZ）は描かない。
/// 今は空の映り込みと待機中のさざ波のみ。足元・ボスの波や映り込みはここに足していく。
/// </summary>
class BossArenaWater {
public:
	BossArenaWater();
	~BossArenaWater();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager);

	/// <summary>
	/// 闘技場の配置。center = 足場の中心（XZ）、groundY = 石畳の高さ（プレイヤーの接地高さ。水面はこの上に
	/// 水深ぶん張る）、waterRadius = 床の水を描く円の半径、seaY = 海面
	/// </summary>
	void SetArena(const Vector3& center, float groundY, float waterRadius, float seaY);
	/// <summary>足場の水に波を1発（自機の着地など）</summary>
	void EmitSplash(const Vector3& position);

	/// <summary>波の時間を進める（World 時間を渡す＝スロー中は波も遅くなる）</summary>
	void Update(float deltaTime);
	/// <summary>波のシミュレーション（コンピュート）。シーン RT をバインドする前に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>不透明物の後・半透明物の前に呼ぶ。ルートシグネチャを替えるので後続は各自設定し直すこと</summary>
	void Draw(const Camera& camera, const std::string& skyCubemapPath);

	void OnImGui();

private:
	void ApplyHeight();

	std::unique_ptr<WaterSurface> water_; // 足場の床
	std::unique_ptr<WaterSurface> sea_;   // 海
	float groundY_ = 0.0f;
	float depth_ = 0.05f; // 水深 [m]
	float seaClipMinZ_ = 3600.0f; // 海を描く Z の下限（海岸線 Z ≈ 3530〜3710。陸の下に入る分は地形が隠す）
};
