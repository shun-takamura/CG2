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

	/// <summary>足場の床の水（石畳の水底・控えめなさざ波）と海（カメラ追従・深い水）の 2 枚を作る</summary>
	/// <param name="dxCore">DirectX の基盤</param>
	/// <param name="srvManager">SRV の管理</param>
	/// <param name="object3DManager">3D オブジェクトの共通描画設定</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager);

	/// <summary>闘技場の配置を決める（床の水は縁石の内側の円だけ描く）</summary>
	/// <param name="center">足場の中心（XZ を使う）</param>
	/// <param name="groundY">石畳の高さ（プレイヤーの接地高さ。水面はこの上に水深ぶん張る）[m]</param>
	/// <param name="waterRadius">床の水を描く円の半径 [m]</param>
	/// <param name="seaY">海面の高さ [m]</param>
	void SetArena(const Vector3& center, float groundY, float waterRadius, float seaY);
	/// <summary>足場の水に波を1発（自機の着地など）</summary>
	/// <param name="position">波の中心（ワールド座標）</param>
	void EmitSplash(const Vector3& position);

	/// <summary>波の時間を進める（World 時間を渡す＝スロー中は波も遅くなる）</summary>
	/// <param name="deltaTime">経過時間 [秒]</param>
	void Update(float deltaTime);
	/// <summary>波のシミュレーション（コンピュート）。シーン RT をバインドする前に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>海→床の水の順に描く。不透明物の後・半透明物の前に呼ぶ。ルートシグネチャを替えるので後続は各自設定し直すこと</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="skyCubemapPath">水面に映す空の cubemap のパス</param>
	void Draw(const Camera& camera, const std::string& skyCubemapPath);

	/// <summary>水深・床の水・海の調整 UI（Debug のみ中身あり）</summary>
	void OnImGui();

private:
	/// <summary>水深を床の水に反映し、水面を石畳の高さ＋水深に置く</summary>
	void ApplyHeight();

	std::unique_ptr<WaterSurface> water_; // 足場の床
	std::unique_ptr<WaterSurface> sea_;   // 海
	float groundY_ = 0.0f;
	float depth_ = 0.05f; // 水深 [m]
	float seaClipMinZ_ = 3600.0f; // 海を描く Z の下限（海岸線 Z ≈ 3530〜3710。陸の下に入る分は地形が隠す）
};
