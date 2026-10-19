#pragma once
#include "StageStart/StageStartGeometry.h"
#include "Vector3.h"
#include "Vector4.h"
#include <memory>
#include <random>
#include <vector>

class Camera;
class JsonValue;
class PrimitiveMesh;
struct MeshVertex;

/// <summary>
/// 電磁カタパルトの雷（16_StageStart.md §3.2、F5）。2026-10-19 ユーザー決定：
///   射出許可の前、緑のネオンの点け直しと一緒に、両脚の下の溝に沿って雷が射出口へ伸びていく。
///   点き切った後は溝で細かく放電し、SPACE（スラスターの出力）で強くなる。射出中は固定台の足元に強い雷を出す。
///   2026-10-19 さらに「漏電しているくらい大げさに」「レール全体に電気が走っている感じ」：溝の底を縦に走る長い雷
///   （点いた所全体）と、溝から跳ね上がる大きな弧を足し、本数を大きく増やした。
/// 色は青白（溝の底の給電の線は光らせない＝gen_catapult.py で暗い鋼に）。雷 1 本＝折れ線（点を毎回少しずらしてチラつかせる）。カメラへ向けた帯（真ん中だけ不透明・両端は透明）にして、
/// 全部の雷を 1 つの動的メッシュ（PrimitiveMesh::UpdateVertices）にまとめて 1 回で描く。加算＋発光パス（ブルーム）。
/// 座標はカタパルトの原点から（SetOrigin でワールドへ）。調整値は StagePlay.json の "stageStart" → "lightning"。
/// </summary>
class CatapultLightning {
public:
	/// <summary>このフレームの雷の出し方</summary>
	struct Drive {
		bool sweeping = false;       // ネオンの点け直しの最中（先頭と一緒に伸びる）
		float sweepFrontZ = 0.0f;    // 点け直しの先頭の z
		bool charged = false;        // 点き切った後（溝で細かく放電）
		float thrust = 0.0f;         // スラスターの出力 0..1（放電を強くする）
		bool launching = false;      // 射出が始まった（射出口を出た後も）。機体が通り過ぎた所の雷は時間で弱まっていく
		bool clampRunning = false;   // 固定台がまだ走っている（足元の雷）
		float clampTravel = 0.0f;    // 固定台が前へ走った距離 [m]
	};

	CatapultLightning();
	~CatapultLightning();

	void Initialize(const StageStartGeometry& geometry);
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>出ている雷を全部消す（演出の始め・機体確認のやり直し）</summary>
	void Reset();
	void Update(float deltaTime, const Drive& drive);
	/// <summary>不透明物の後（PrimitivePipeline に切り替える）。帯はカメラへ向けて作る</summary>
	void Draw(Camera* camera);
	/// <summary>発光パス（Draw の後に呼ぶ。同じフレームの形を使う）</summary>
	void DrawBloom();

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

	static constexpr int kMaxBolts = 600;
	static constexpr int kSegments = 12;  // 1 本の折れ線の区間の数（溝を縦に走る長い雷があるので細かめ）

private:
	struct Bolt {
		bool alive = false;
		Vector3 a, b;              // カタパルトの原点から
		bool followClamp = false;  // 固定台について走る（射出中の足元）
		float spawnTravel = 0.0f;  // 生まれたときの固定台の前進量
		float age = 0.0f;
		float life = 0.1f;
		float width = 0.1f;
		float brightness = 1.0f;
		float jitterTimer = 0.0f;
		float offsets[kSegments + 1][2] = {};   // 折れ線の横ずれ（2 方向）
	};

	float Rand(float lo, float hi);
	void Spawn(const Vector3& a, const Vector3& b, float life, float width, float brightness, bool followClamp, float travel);
	void Jitter(Bolt& bolt);
	void SpawnSweep(float frontZ, float count);
	void SpawnIdle(float thrust, float count);
	void SpawnClamp(float clampTravel, float count, float width, float brightness);
	void SpawnTrail(float clampTravel, float count);
	// 溝の底を縦に走る長い雷（zMin〜zMax のあいだ）
	void SpawnRailRun(float zMin, float zMax, float count, float brightness);
	// 射出中、z の所の雷の強さ 0..1（固定台がまだ通っていなければ 1、通ってからは時間で弱まる）
	float LaunchLevelAt(float z) const;
	// 射出中は LaunchLevelAt の割合で間引く（true＝出さない）。brightness に強さを掛ける
	bool CullByLaunch(float z, float& brightness);
	void BuildVertices(const Vector3& cameraPos);

	StageStartGeometry geometry_;
	Vector3 origin_ = { 0.0f, 0.0f, 0.0f };
	std::vector<Bolt> bolts_;
	std::unique_ptr<PrimitiveMesh> mesh_;
	std::vector<MeshVertex> vertices_;
	std::mt19937 random_{ 20261019u };
	float clampTravel_ = 0.0f;
	bool built_ = false;
	// 1 フレームに出す本数の端数の繰り越し
	float sweepCarry_ = 0.0f;
	float idleCarry_ = 0.0f;
	float clampCarry_ = 0.0f;
	float trailCarry_ = 0.0f;
	float runCarry_ = 0.0f;
	// 射出の後、機体が通り過ぎた所から雷が収まっていく（2026-10-19 ユーザー：射出後にパッと消えていた）。
	// 溝を kPassBuckets に区切り、固定台が通った時刻（射出の始まりから）を覚える
	static constexpr int kPassBuckets = 96;
	bool launching_ = false;
	float launchTime_ = 0.0f;
	float passTime_[kPassBuckets] = {};

	// 調整値
	float sweepRate_ = 220.0f;          // 点け直しの先頭：1 秒あたりの本数（2 本の溝の合計）
	float sweepTail_ = 10.0f;           // 先頭の後ろのこの長さ [m] に出す
	float sweepWidth_ = 0.3f;
	float runRate_ = 140.0f;            // 溝の底を縦に走る長い雷（溝の全長あたり。点け直しの間は点いた所の割合だけ）
	float runThrustBoost_ = 1.5f;       // 出力 100% で runRate × (1 + これ)
	float runLengthMin_ = 4.0f;         // 長い雷の長さ [m]
	float runLengthMax_ = 14.0f;
	float runWidth_ = 0.22f;
	float idleRate_ = 160.0f;           // 点き切った後、溝から跳ね上がる弧（出力 0 のとき）
	float idleThrustBoost_ = 2.0f;      // 出力 100% で idleRate × (1 + これ)
	float idleNearBias_ = 1.3f;         // 機体の近くほど多く（z の分布の累乗。1 で一様）
	float idleWidth_ = 0.22f;
	float idleArcHeight_ = 1.5f;        // 弧が溝から跳ね上がる高さの最大 [m]
	float chargeClampRate_ = 120.0f;    // 出力 100% のとき固定台の足元に出す本数
	float launchClampRate_ = 260.0f;    // 射出中の固定台の足元の本数（2 台の合計）
	float launchWidth_ = 0.4f;
	float launchBrightness_ = 2.0f;
	float trailRate_ = 200.0f;          // 射出中、固定台の後ろの溝に残る放電
	float trailLength_ = 40.0f;         // 固定台の後ろのこの長さ [m]
	float jag_ = 0.18f;                 // 折れ線の横ずれ（長さに対する割合）
	float afterFadeSeconds_ = 0.35f;    // 機体が通り過ぎてから雷が収まるまで（指数で弱まる時定数 [s]。2026-10-19 ユーザー：1.2 は遅い）
	float jagMax_ = 0.7f;               // 横ずれの上限 [m]（長い雷が太いジグザグになりすぎないように）
	float jitterSeconds_ = 0.035f;      // 折れ線を描き直す間隔（チラつき）
	// 青白（2026-10-19 ユーザー指示。緑だとネオンに埋もれた）
	Vector4 coreColor_ = { 0.85f, 0.93f, 1.0f, 1.0f };   // 芯（ほぼ白）
	Vector4 glowColor_ = { 0.3f, 0.55f, 1.0f, 1.0f };    // 縁（青）
	float bloomIntensity_ = 2.5f;
};
