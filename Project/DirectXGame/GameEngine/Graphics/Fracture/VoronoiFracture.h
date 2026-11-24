#pragma once
#include "Vector3.h"
#include <cstdint>
#include <string>
#include <vector>

/// <summary>
/// 閉じた三角形メッシュ（穴の無い岩など）を 3D ボロノイで割り、断面のメッシュも作る（CPU だけ。読み込み時に 1 回）。
/// 種点ごとに、元のメッシュを「その種点と他の種点の垂直二等分面」で順に切り、切り口の輪を耳刈り法で三角形にして塞ぐ。
/// 出力の頂点は「割る前の岩の空間」の座標のままで、破片の番号を持つ（動かすのは描画側の破片ごとの行列）。
/// </summary>
class VoronoiFracture {
public:
	/// <summary>描画へ渡す 1 頂点（32 バイト。シェーダの入力と同じ並び）</summary>
	struct Vertex {
		Vector3 position;       // 割る前の岩の空間の位置
		Vector3 normal;         // 割る前の岩の空間の法線（断面は切った面の向き）
		float interior = 0.0f;  // 1 = 断面、0 = 外側
		uint32_t fragment = 0;  // 破片の番号（割る前の岩の頂点は 0）
	};

	/// <summary>割る元のメッシュ（三角形リスト）</summary>
	struct SourceMesh {
		std::vector<Vector3> positions;
		std::vector<Vector3> normals;
		std::vector<uint32_t> indices;
	};

	/// <summary>割り方の設定</summary>
	struct Settings {
		uint32_t seedCount = 20;       // 種点の数（＝破片の数の目安。外れた種点の分だけ減る）
		uint32_t randomSeed = 1;       // 種点の並びを決める乱数の種
		float minSeedSpacing = 0.55f;  // 種点どうしの最小間隔（平均の間隔に対する比。大きいほど破片の大きさがそろう）
	};

	/// <summary>1 個の破片</summary>
	struct Fragment {
		Vector3 center{ 0.0f, 0.0f, 0.0f };  // 体積の重心（割る前の岩の空間）。回転の中心
		float volume = 0.0f;                 // 体積（岩の空間）
		float radius = 0.0f;                 // 重心から最も遠い頂点までの距離
		uint32_t firstIndex = 0;             // Result::indices の中の開始位置
		uint32_t indexCount = 0;
	};

	/// <summary>割った結果。頂点・インデックスは「割る前の岩」→「破片 0, 1, ...」の順に連結している</summary>
	struct Result {
		std::vector<Vertex> vertices;
		std::vector<uint32_t> indices;
		uint32_t intactFirstIndex = 0;  // 割る前の岩のインデックスの範囲
		uint32_t intactIndexCount = 0;
		std::vector<Fragment> fragments;
		std::vector<Vector3> seeds;     // 使った種点（デバッグ表示用）
		uint32_t capFailures = 0;       // 輪に閉じなかった切り口の数（0 が正常。元のメッシュに穴があると増える）
		float buildMilliseconds = 0.0f; // 割るのにかかった時間

		/// <summary>描ける結果か</summary>
		/// <returns>割る前の岩と破片が 1 個以上あれば true</returns>
		bool IsValid() const { return intactIndexCount > 0 && !fragments.empty(); }
	};

	/// <summary>モデルファイル（glb など assimp で読める形式）の全メッシュを 1 つの三角形リストとして読む</summary>
	/// <param name="filePath">モデルファイルのパス</param>
	/// <param name="out">読み込み先</param>
	/// <returns>読めたら true</returns>
	static bool LoadSourceMesh(const std::string& filePath, SourceMesh& out);

	/// <summary>メッシュをボロノイで割る</summary>
	/// <param name="mesh">割る元の閉じたメッシュ</param>
	/// <param name="settings">割り方</param>
	/// <returns>割った結果（失敗したら IsValid() が false）</returns>
	static Result Build(const SourceMesh& mesh, const Settings& settings);
};
