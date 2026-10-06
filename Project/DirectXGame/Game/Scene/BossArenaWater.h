#pragma once
#include <memory>
#include <string>

class DirectXCore;
class SRVManager;
class Object3DManager;
class Camera;
class WaterSurface;

/// <summary>
/// ボス戦の床：石畳（タイトルの水底と同じ素材）の上に薄く張った水。
/// 石畳は WaterSurface の水底としてシェーダ内で描かれるので、床のモデルは置かない。
/// 床（石畳）の高さ = groundY、水面 = groundY + 水深。
/// 今は空の映り込みと待機中のさざ波のみ。足元・ボスの波や映り込みはここに足していく。
/// </summary>
class BossArenaWater {
public:
	BossArenaWater();
	~BossArenaWater();

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, Object3DManager* object3DManager);

	/// <summary>石畳の高さ（プレイヤーの接地高さ）。水面はこの上に水深ぶん張る</summary>
	void SetGroundY(float groundY);
	/// <summary>波紋の中心（アリーナ中心）</summary>
	void SetArenaCenter(float x, float z);

	/// <summary>波の時間を進める（World 時間を渡す＝スロー中は波も遅くなる）</summary>
	void Update(float deltaTime);
	/// <summary>波のシミュレーション（コンピュート）。シーン RT をバインドする前に呼ぶ</summary>
	void DispatchSimulation();
	/// <summary>不透明物の後・半透明物の前に呼ぶ。ルートシグネチャを替えるので後続は各自設定し直すこと</summary>
	void Draw(const Camera& camera, const std::string& skyCubemapPath);

	void OnImGui();

private:
	void ApplyHeight();

	std::unique_ptr<WaterSurface> water_;
	float groundY_ = 0.0f;
	float depth_ = 0.05f; // 水深 [m]
};
