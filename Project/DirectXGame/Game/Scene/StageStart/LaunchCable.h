#pragma once
#include "Vector3.h"
#include "Vector4.h"
#include <array>
#include <memory>
#include <string>
#include <vector>

class Camera;
class JsonValue;
class PrimitiveMesh;
struct MeshVertex;

/// <summary>
/// カタパルトの給電ケーブル（16_StageStart.md §3.2、F5）。後ろの壁の高い所の付け根から機体の背中の丸い部品へ太い赤の 1 本。
/// 点を並べたひも（Verlet）で動かし、点をつないだ筒のメッシュを毎フレーム作り直す（PrimitiveMesh::UpdateVertices）。
/// 2026-10-19 ユーザー決定：射出の途中（7 割）までつないだまま、伸びるゴムひものように機体についていき、
/// そこでパージすると、たわみながら付け根の方へ縮んで戻る。
/// PrimitiveMesh はライティングを使わないので、陰影（空と床の明るさ・上からの光・つや）は頂点色に焼く。
/// 調整値は StagePlay.json の "stageStart" → "cable"。
/// </summary>
class LaunchCable {
public:
	LaunchCable();
	~LaunchCable();

	void Initialize();

	/// <summary>
	/// 付け根と機体側の端をつなぎ直し、2 点のあいだに垂れた形で置く（演出の始め）。
	/// restAttach＝機体が前へ出ていないとき（前進 0）の端。長さはこちらの距離で決める
	/// </summary>
	void Reset(const Vector3& anchor, const Vector3& attach, const Vector3& restAttach);
	/// <summary>つなぎ直す。形は次の Update で、そのときの付け根と端から作る（機体のボーンがまだ動いていないときに使う）</summary>
	void Reconnect() { attached_ = true; valid_ = false; }
	/// <summary>
	/// anchor＝付け根（ワールド）、attach＝機体側の端（つないでいる間だけ使う）、restAttach＝前進 0 のときの端（つなぎ直すときの長さ）、
	/// floorY＝カタパルトの床の高さ（ワールド）
	/// </summary>
	void Update(float deltaTime, const Vector3& anchor, const Vector3& attach, const Vector3& restAttach, float floorY);
	/// <summary>機体側の端を離す（パージ）。この後は付け根へ縮んでいく</summary>
	void Purge();
	bool IsAttached() const { return attached_; }
	/// <summary>外れた後にケーブルを横切らせたい点（三人称のカメラの目。ワールド）</summary>
	void SetSweepTarget(const Vector3& target) { sweepTarget_ = target; }

	/// <summary>不透明物の後（PrimitivePipeline に切り替える）。陰影はカメラの位置で焼く</summary>
	void Draw(Camera* camera);

	void LoadTuning(const JsonValue& node);
	void SaveTuning(JsonValue& node) const;
	void OnImGui();

	// 機体側の端（StageStartPart が機体のボーンから出す）
	const std::string& GetAttachBone() const { return attachBone_; }
	const Vector3& GetAttachOffset() const { return attachOffset_; }
	int GetAnchorIndex() const { return anchorIndex_; }

	static constexpr int kPointCount = 32;   // ひもの点の数
	static constexpr int kSides = 12;        // 筒の断面の角の数

private:
	void Simulate(float dt, const Vector3& anchor, const Vector3& attach, float floorY);
	void BuildVertices(const Vector3& cameraPos);
	// 点 i でケーブルの向きに直交する面の中の、角度 angle の向き（螺旋のうねり）
	Vector3 SwirlDirection(int i, float angle) const;

	std::unique_ptr<PrimitiveMesh> mesh_;
	std::vector<MeshVertex> vertices_;

	std::array<Vector3, kPointCount> points_{};
	std::array<Vector3, kPointCount> previous_{};
	bool attached_ = true;
	bool valid_ = false;
	float baseLength_ = 0.0f;     // つないだときの長さ（2 点の距離 × slack）
	float purgeLength_ = 0.0f;    // パージした瞬間の長さ
	float purgeTime_ = 0.0f;      // パージしてからの秒
	float length_ = 0.0f;         // 今の長さ（伸び縮みする）

	// 調整値
	int anchorIndex_ = 0;                                   // StageStartGeometry の cableAnchors のどちら（2026-10-19 ユーザー：今の箱のまま）
	std::string attachBone_ = "mixamorig:Spine2";           // 背中の丸い部品
	Vector3 attachOffset_ = { 0.0f, 1.6f, -1.4f };          // ボーンの位置からのずらし（ワールドの向き・m。機体は +Z を向いて回さない）。背中の金の円（2026-10-19 実機で 0.4 は低かった）
	float radius_ = 0.34f;                                  // 太さ（半径 m。2026-10-19 ユーザー指示で 1.2 倍）
	float slack_ = 1.12f;                                   // つないだときの長さ＝2 点の距離 × これ（たるみ）
	float stretchSlack_ = 1.02f;                            // 機体が離れて伸びるときの長さ＝距離 × これ（ほぼ張る）
	float thinMin_ = 0.55f;                                 // 伸びると細くなる（半径 × sqrt(元の長さ / 今の長さ)、この倍率まで）
	float gravity_ = 14.0f;                                 // 重さ [m/s²]（太いので少し強め）
	float damping_ = 0.985f;                                // 1 ステップごとの速さの残り
	float stiffness_ = 0.6f;                                // 長さの拘束の強さ（1 未満でゴムのように伸び縮み）
	int iterations_ = 24;
	int substeps_ = 2;
	float maxSpeed_ = 140.0f;                               // 点の速さの上限 [m/s]（縮むときに暴れないように）
	float retractLength_ = 7.0f;                            // パージした後に縮む先の長さ [m]
	float retractSeconds_ = 2.2f;                           // 縮み切るまで
	float retractDelay_ = 0.3f;                             // 外れてから縮み始めるまで（先にうねらせる）
	Vector3 purgeKick_ = { 0.0f, 8.0f, -10.0f };            // パージの瞬間に離れた端へ足す速さ [m/s]（跳ね上がって後ろへ）
	float purgeDamping_ = 0.995f;                           // 外れた後の速さの残り（大きく揺れ続けるよう弱めに）
	// 曲げの硬さ（1 つ飛ばしの点の距離が 2·区間·bendRest より縮まないように。太いホースが丸い弧を描く。2026-10-19 参考画像）
	float bendStiffness_ = 0.25f;
	float bendRest_ = 0.97f;
	// 外れた後のうねり（2026-10-19 ユーザー：もっとうねりながらパージ）
	// 2026-10-19 さらに「もっと螺旋のように」→ 横だけの波をやめ、ケーブルの軸の周りを回る螺旋に
	float waveKick_ = 20.0f;                                // パージの瞬間の螺旋の速さ [m/s]（端ほど大きい）
	float waveKickHalfWaves_ = 4.0f;                        // その螺旋の巻き（半回転の数）
	float waveForce_ = 90.0f;                               // 先端から付け根へ伝わる螺旋の波の強さ [m/s²]
	float waveFrequency_ = 1.6f;                            // 回る速さ [Hz]
	float waveCount_ = 2.5f;                                // ケーブル全体に乗る巻きの数
	float waveUpRatio_ = 1.0f;                              // 螺旋の断面（1 で円、小さいほど横につぶれた楕円）
	float waveFadeSeconds_ = 2.2f;                          // 波が消えるまで
	// 外れた後、ケーブルの中ほどを三人称のカメラの側へ引き寄せ、目の前をうねりながら横切らせる（2026-10-19 ユーザー）
	Vector3 sweepTarget_ = { 0.0f, 0.0f, 0.0f };            // 保存しない（StageStartPart が毎フレーム渡す）
	float sweepForce_ = 70.0f;                              // 引き寄せる強さ [m/s²]
	float sweepDelay_ = 0.1f;                               // 外れてから引き寄せ始めるまで [s]
	float sweepSeconds_ = 1.6f;                             // 強まって弱まるまで [s]（山の形）
	float sweepCenter_ = 0.55f;                             // 一番強く引く所（付け根 0 〜 端 1）
	float sweepWidth_ = 0.35f;                              // 引く範囲の広さ（u の幅）
	float sweepKeepOut_ = 4.0f;                             // カメラからこの距離 [m] より近づいたら引かない
	float floorFriction_ = 0.6f;                            // 床に着いた点の横の速さの残り
	int plugPoints_ = 2;                                    // 機体側の端の差し込み口（太く暗い）の点の数
	float plugScale_ = 1.45f;
	Vector4 color_ = { 0.62f, 0.05f, 0.05f, 1.0f };         // 赤のゴム
	Vector4 plugColor_ = { 0.16f, 0.16f, 0.18f, 1.0f };
	Vector3 skyLight_ = { 0.55f, 0.6f, 0.7f };              // 上を向いた面の明るさ（天井の灯り）
	Vector3 groundLight_ = { 0.12f, 0.12f, 0.14f };         // 下を向いた面
	Vector3 keyDirection_ = { 0.3f, 1.0f, 0.4f };           // 主な光の来る向き（ワールド）
	float keyIntensity_ = 0.7f;
	float specularIntensity_ = 0.45f;
	float specularPower_ = 24.0f;
};
