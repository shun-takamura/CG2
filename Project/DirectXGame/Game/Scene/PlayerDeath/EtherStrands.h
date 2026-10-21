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

	void Initialize();
	/// <summary>出ている糸を全部消す</summary>
	void Reset();
	/// <summary>糸の根元のアンカー（ワールド）。毎フレーム自機の位置を渡す</summary>
	void SetAnchor(const Vector3& anchor) { anchor_ = anchor; }
	const Vector3& GetAnchor() const { return anchor_; }
	/// <summary>ワールドの root から糸を 1 本出す。scale は体の拡大率（長さ・太さ・振れ幅に掛ける）</summary>
	void Emit(const Vector3& root, float scale);
	void Update(float deltaTime);
	bool HasAlive() const;

	/// <summary>不透明物の後（PrimitivePipeline に切り替える）。帯はカメラへ向けて作る</summary>
	void Draw(Camera* camera, const Vector3& etherColor);
	/// <summary>発光パス（Draw の後に呼ぶ。同じフレームの形を使う）</summary>
	void DrawBloom();
	bool IsBuilt() const { return built_; }

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
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

	float Rand(float lo, float hi);
	// 糸の高さ h（根元から）での横ずれ
	Vector3 SwayAt(const Strand& s, float h) const;
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
