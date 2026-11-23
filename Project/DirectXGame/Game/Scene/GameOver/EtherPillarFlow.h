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

	/// <summary>道の輪郭（無ければ既定）から長さを測り、全部の線の帯を入れる動的メッシュを作る</summary>
	void Initialize();
	/// <summary>器の原点（土台の脚の下）を設定する</summary>
	/// <param name="origin">ワールド位置</param>
	void SetOrigin(const Vector3& origin) { origin_ = origin; }
	/// <summary>明るい光を上から次々に下ろし始める（止めるまで）</summary>
	void BeginDescend();
	/// <summary>光を下ろすのをやめる（出ている光は流れ切る）</summary>
	void StopDescend() { descending_ = false; }
	/// <summary>明るい光を下から次々に昇らせ始める（止めるまで）</summary>
	void BeginAscend();
	/// <summary>光を昇らせるのをやめる（出ている光は流れ切る）</summary>
	void StopAscend() { ascending_ = false; }
	/// <summary>光を昇らせている途中かを返す</summary>
	/// <returns>途中なら true</returns>
	bool IsAscending() const { return ascending_; }
	/// <summary>光を下ろしている途中かを返す</summary>
	/// <returns>途中なら true</returns>
	bool IsDescending() const { return descending_; }
	/// <summary>この Update の間に道の上の端（球）へ着いた明るい光の数を返す（液面に波紋を立てるのに使う）</summary>
	/// <returns>光の数</returns>
	int GetTopArrivals() const { return topArrivals_; }
	/// <summary>いつもの淡い光の倍率を設定する</summary>
	/// <param name="scale">倍率（0 で消える）</param>
	void SetIdleScale(float scale) { idleScale_ = scale; }
	/// <summary>この Update の間に道の下の端へ着いた光の数を返す（泉に波紋を立てるのに使う）</summary>
	/// <returns>光の数</returns>
	int GetArrivals() const { return arrivals_; }
	/// <summary>道の下の端を返す</summary>
	/// <returns>ワールド位置（輪郭が無ければ器の原点）</returns>
	Vector3 GetBottom() const;

	/// <summary>光を出して道に沿って進め、端に着いた光を数える</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);
	/// <summary>カメラへ向けた帯を作り直して描く。不透明物の後に呼ぶ（PrimitivePipeline に切り替える）</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="etherColor">エーテルの色（RGB 0〜1）</param>
	void Draw(Camera* camera, const Vector3& etherColor);
	/// <summary>発光パスで帯を描く（Draw で作った後だけ）</summary>
	void DrawBloom();
	/// <summary>帯を作り終えているかを返す</summary>
	/// <returns>Draw で作った後なら true</returns>
	bool IsBuilt() const { return built_; }

	/// <summary>調整値（GameOver.json の "pillarFlow"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
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

	/// <summary>一様乱数を返す</summary>
	/// <param name="lo">下限</param>
	/// <param name="hi">上限</param>
	/// <returns>lo〜hi の値</returns>
	float Rand(float lo, float hi);
	/// <summary>道の輪郭の折れ線の、各点までの長さと全長を測り直す</summary>
	void RebuildProfile();
	/// <summary>道の上の端から距離 s の点を求める</summary>
	/// <param name="s">上の端からの道に沿った距離</param>
	/// <param name="angle">中心の軸まわりの向き [ラジアン]</param>
	/// <returns>ワールド位置（器の原点から）</returns>
	Vector3 PointAt(float s, float angle) const;
	/// <summary>今の光の位置から、カメラへ向けた帯の頂点を作る</summary>
	/// <param name="cameraPos">カメラの位置（ワールド）</param>
	/// <param name="etherColor">エーテルの色（RGB 0〜1）</param>
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
