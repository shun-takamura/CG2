#pragma once
#include "Vector2.h"
#include "Vector3.h"
#include <memory>
#include <random>
#include <vector>

class Camera;
class JsonValue;
class PrimitiveMesh;
struct MeshVertex;

/// <summary>
/// 器の支柱の溝を走るエーテルの光の線（19_GameOverAndDifficulty.md §4.5・§4.6、G6）。
/// 線は球の下から支柱を伝って土台へ下りる道（断面の輪郭＝高さと半径の折れ線）に沿って、まわりに何本か並べる。
///   - いつも：うっすら通っていて、ときどき下から弱い光が昇る（下に泉がある気配）
///   - 泉へ下りる（BeginDescend）：明るい光が上から次々に下りていく（引き上げる・賭け分を下げる＝手元へ戻る）
///   - 泉から昇る（BeginAscend）：明るい光が下から次々に昇っていく（賭ける＝手元から器へ。§4.7）
/// 作りは EtherStrands と同じ（カメラへ向けた帯を 1 つの動的 PrimitiveMesh にまとめて 1 回で描く。加算＋発光パス）。
/// 道の形・本数・明るさは GameOver.json の "pillarFlow"。輪郭は器の原点（土台の脚の下）からの高さと、中心の軸からの半径。
/// </summary>
class EtherPillarFlow {
public:
	EtherPillarFlow();
	~EtherPillarFlow();

	void Initialize();
	/// <summary>器の原点（土台の脚の下）のワールド位置</summary>
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>光を上から次々に下ろす（止めるまで）</summary>
	void BeginDescend();
	void StopDescend() { descending_ = false; }
	/// <summary>光を下から次々に昇らせる（止めるまで）</summary>
	void BeginAscend();
	void StopAscend() { ascending_ = false; }
	bool IsAscending() const { return ascending_; }
	bool IsDescending() const { return descending_; }
	/// <summary>この Update の間に道の上の端（球）へ着いた明るい光の数（液面に波紋を立てるのに使う）</summary>
	int GetTopArrivals() const { return topArrivals_; }
	/// <summary>いつもの淡い光の倍率（0 で消える）</summary>
	void SetIdleScale(float scale) { idleScale_ = scale; }
	/// <summary>この Update の間に道の下の端へ着いた光の数（泉に波紋を立てるのに使う）</summary>
	int GetArrivals() const { return arrivals_; }
	/// <summary>道の下の端（ワールド）</summary>
	Vector3 GetBottom() const;

	void Update(float deltaTime);
	/// <summary>不透明物の後（PrimitivePipeline に切り替える）</summary>
	void Draw(Camera* camera, const Vector3& etherColor);
	void DrawBloom();
	bool IsBuilt() const { return built_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

	static constexpr int kMaxLines = 16;
	static constexpr int kSamples = 48;          // 1 本の帯の点の数
	static constexpr int kMaxProfilePoints = 8;

private:
	struct Pulse {
		int line = 0;
		float position = 0.0f;   // 道の上の端からの距離 [m]
		float speed = 0.0f;      // + で下へ [m/s]
		float intensity = 1.0f;
		float length = 0.1f;     // 光の長さ [m]
	};

	float Rand(float lo, float hi);
	void RebuildProfile();
	// 道の上の端からの距離 s の点（器の原点から、角度 angle の向き）
	Vector3 PointAt(float s, float angle) const;
	void BuildVertices(const Vector3& cameraPos, const Vector3& etherColor);

	Vector3 origin_{};
	std::vector<Vector2> profile_;          // (高さ, 半径)。上から下へ
	std::vector<float> cumulative_;         // 各点までの道のり [m]
	float totalLength_ = 0.0f;
	std::vector<Pulse> pulses_;
	std::unique_ptr<PrimitiveMesh> mesh_;
	std::vector<MeshVertex> vertices_;
	std::mt19937 random_{ 20261024u };
	float time_ = 0.0f;
	float idleTimer_ = 0.0f;
	float descendTimer_ = 0.0f;
	bool descending_ = false;
	float ascendTimer_ = 0.0f;
	bool ascending_ = false;
	int topArrivals_ = 0;
	float idleScale_ = 1.0f;
	int arrivals_ = 0;
	bool built_ = false;

	// 調整値
	int lineCount_ = 6;
	float angleOffsetDeg_ = 0.0f;
	float surfaceLift_ = 0.004f;        // 表面から浮かせる量 [m]（深度で埋もれないように）
	float width_ = 0.006f;              // 帯の太さ [m]
	float idleIntensity_ = 0.25f;       // いつもの淡い光
	float idleBreathSpeed_ = 1.2f;      // 淡い光がゆっくり明滅する速さ [rad/s]
	float idlePulseInterval_ = 1.4f;    // 下から弱い光が昇る平均間隔 [s]（どれかの線）
	float idlePulseSpeed_ = 0.35f;      // [m/s]
	float idlePulseIntensity_ = 0.8f;
	float descendInterval_ = 0.08f;     // 下りる光を出す平均間隔 [s]（どれかの線）
	float descendSpeed_ = 1.4f;         // [m/s]
	float descendIntensity_ = 3.0f;
	float pulseLength_ = 0.12f;         // 光の長さ [m]
	float coreWhite_ = 0.5f;
	float bloomIntensity_ = 2.0f;
};
