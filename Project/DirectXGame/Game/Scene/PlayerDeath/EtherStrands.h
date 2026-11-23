#pragma once
#include "Vector3.h"
#include <memory>
#include <random>
#include <vector>

class Camera;
class JsonValue;
class PrimitiveMesh;
struct MeshVertex;

/// <summary>
/// 死亡演出のエーテル化で、体からほどけて天へ昇る光の糸（19_GameOverAndDifficulty.md §1、G1）。
/// 参考は BotW のワープの逆（2026-10-21 ユーザーの参考画像：細い光の糸が横にくねりながら上へ伸び、昇るほど広がる）。
/// 糸 1 本＝根元から上へ伸びる曲線（頭が加速しながら昇り、伸び切ると尾も離れて昇る。高さに沿ってくねり、昇るほど振れ幅と広がりが増す）。
/// カメラへ向けた帯（真ん中だけ不透明・両端は透明）にして、全部の糸を 1 つの動的メッシュ（PrimitiveMesh::UpdateVertices）に
/// まとめて 1 回で描く。加算＋発光パス（ブルーム）。作りは CatapultLightning と同じ。
/// 根元は「アンカー（自機の位置）からのずらし」で持つ＝レールで自機ごと進んでも糸が置いていかれない。
/// どこから出すか（ディゾルブの面の高さの体の上）は呼ぶ側（PlayerDeathPart）が決めて Emit する。
/// 色はエーテルの色（呼ぶ側が渡す。ハードコードしない）。調整値は StagePlay.json の "playerDeath" → "strands"。
/// </summary>
class EtherStrands {
public:
	EtherStrands();
	~EtherStrands();

	/// <summary>糸の枠と、全部の糸の帯を入れる動的メッシュを作る</summary>
	void Initialize();
	/// <summary>出ている糸を全部消す</summary>
	void Reset();
	/// <summary>糸の根元のアンカーを設定する。毎フレーム自機の位置を渡す</summary>
	/// <param name="anchor">ワールド位置</param>
	void SetAnchor(const Vector3& anchor) { anchor_ = anchor; }
	/// <summary>糸の根元のアンカーを返す</summary>
	/// <returns>ワールド位置</returns>
	const Vector3& GetAnchor() const { return anchor_; }
	/// <summary>ワールドの root から糸を 1 本出す（空きが無ければ出さない）</summary>
	/// <param name="root">根元のワールド位置</param>
	/// <param name="scale">体の拡大率（長さ・太さ・振れ幅に掛ける）</param>
	void Emit(const Vector3& root, float scale);
	/// <summary>糸の年齢を進め、寿命が来た糸を消す</summary>
	/// <param name="deltaTime">経過 [秒]</param>
	void Update(float deltaTime);
	/// <summary>出ている糸があるかを返す</summary>
	/// <returns>1 本でもあれば true</returns>
	bool HasAlive() const;

	/// <summary>カメラへ向けた帯を作って描く。不透明物の後に呼ぶ（PrimitivePipeline に切り替える）</summary>
	/// <param name="camera">描画に使うカメラ</param>
	/// <param name="etherColor">エーテルの色（RGB 0〜1）</param>
	void Draw(Camera* camera, const Vector3& etherColor);
	/// <summary>発光パスで描く（Draw の後に呼ぶ。同じフレームの形を使う）</summary>
	void DrawBloom();
	/// <summary>このフレームの帯を作り終えているかを返す</summary>
	/// <returns>Draw で作った後なら true</returns>
	bool IsBuilt() const { return built_; }

	/// <summary>調整値（StagePlay.json の "playerDeath" → "strands"）を読む</summary>
	/// <param name="node">読み込み元の JSON ノード</param>
	void LoadTuning(const JsonValue& node);
	/// <summary>調整値を JSON へ書く</summary>
	/// <param name="node">書き込み先の JSON ノード</param>
	void SaveTuning(JsonValue& node) const;
	/// <summary>調整値の ImGui を表示する</summary>
	void OnImGui();

	static constexpr int kMaxStrands = 400;
	static constexpr int kSegments = 15;   // 1 本の曲線の区間の数

private:
	struct Strand {
		bool alive = false;
		Vector3 root;          // アンカーからのずらし
		float scale = 1.0f;
		float age = 0.0f;
		float life = 2.0f;
		float speed = 1.0f;    // 頭の初速 [m/s]
		float length = 1.0f;   // 伸び切った長さ [m]
		float width = 0.02f;
		float brightness = 1.0f;
		float amplitude = 0.1f;   // くねりの振れ幅（根元）[m]
		float freqA = 3.0f;       // くねりの細かさ（高さあたり）[rad/m]
		float freqB = 4.5f;
		float phaseA = 0.0f;
		float phaseB = 0.0f;
		Vector3 flareDir;         // 昇るほど広がる向き（水平）
	};

	/// <summary>一様乱数を返す</summary>
	/// <param name="lo">下限</param>
	/// <param name="hi">上限</param>
	/// <returns>lo〜hi の値</returns>
	float Rand(float lo, float hi);
	/// <summary>糸の高さ h（根元から）での横ずれを求める（昇るほど振れ幅が増す）</summary>
	/// <param name="s">糸</param>
	/// <param name="h">根元からの高さ</param>
	/// <returns>横ずれ（ワールド）</returns>
	Vector3 SwayAt(const Strand& s, float h) const;
	/// <summary>出ている糸から、カメラへ向けた帯の頂点を作る</summary>
	/// <param name="cameraPos">カメラの位置（ワールド）</param>
	/// <param name="etherColor">エーテルの色（RGB 0〜1）</param>
	void BuildVertices(const Vector3& cameraPos, const Vector3& etherColor);

	Vector3 anchor_ = { 0.0f, 0.0f, 0.0f };
	std::vector<Strand> strands_;
	int nextSlot_ = 0;
	std::unique_ptr<PrimitiveMesh> mesh_;
	std::vector<MeshVertex> vertices_;
	std::mt19937 random_{ 20261021u };
	float time_ = 0.0f;
	bool built_ = false;

	// 調整値（体の大きさ 1 のとき。Emit の scale を掛ける）
	float lifeMin_ = 1.6f;
	float lifeMax_ = 2.6f;
	float speedMin_ = 0.6f;          // 頭の初速 [m/s]
	float speedMax_ = 1.6f;
	float acceleration_ = 2.5f;      // 頭の加速 [m/s²]（昇るほど速く＝天へ吸い上げられる）
	float lengthMin_ = 0.8f;         // 伸び切った長さ [m]
	float lengthMax_ = 2.2f;
	float widthMin_ = 0.008f;        // 帯の太さ [m]
	float widthMax_ = 0.022f;
	float amplitude_ = 0.06f;        // くねりの振れ幅（根元）[m]
	float amplitudeGrow_ = 0.12f;    // 高さ 1m あたりに増える振れ幅 [m]
	float frequencyMin_ = 2.0f;      // くねりの細かさ [rad/m]
	float frequencyMax_ = 6.0f;
	float swaySpeed_ = 2.5f;         // くねりが流れる速さ [rad/s]
	float flare_ = 0.18f;            // 高さ 1m あたりに外へ広がる量 [m]
	float coreWhite_ = 0.55f;        // 芯を白へ寄せる割合（0＝エーテルの色のまま）
	float brightness_ = 1.6f;        // 明るさ（加算。1 を超えてよい）
	float fadeInSeconds_ = 0.15f;    // 出てから明るくなるまで
	float bloomIntensity_ = 2.0f;
};
