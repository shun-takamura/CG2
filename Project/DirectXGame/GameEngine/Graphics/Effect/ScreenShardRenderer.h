#pragma once
#include <wrl.h>
#include <d3d12.h>
#include <cstdint>
#include <vector>
#include "Vector2.h"
#include "Matrix4x4.h"

class DirectXCore;
class SRVManager;

/// <summary>
/// 画面を割る演出の破片レンダラ（セル方式）。
/// 画面を事前分割した凸セル（多角形）を三角形化し、各頂点に baked UV（剥がれた画面位置）を持たせて
/// 1本の連結頂点バッファにアップロードする（SetCells、崩壊開始に1回）。
/// 描画は「割れたセルだけ」を per-cell の WVP（重心回転＋飛散移動）で 1 ドローコールずつ描く（Draw）。
/// PS でシーンキャプチャを baked UV でサンプル→色反転＝破片が自分の場所の反転絵を持って飛ぶ。
/// PostEffect の後に描く前提（深度なし・DSVなし）なので二重反転しない。
/// </summary>
class ScreenShardRenderer
{
public:
	// 連結 VB の1頂点（pos=重心ローカル座標[world単位], uv=baked スクリーンUV）
	struct Vertex {
		float pos[3];
		float uv[2];
	};
	// 1セル分の三角形リスト（3頂点×三角数。ファン化済みを想定）
	struct CellMesh {
		std::vector<Vertex> verts;
	};
	// 描画する1セル（割れたセル）
	struct DrawItem {
		uint32_t  cellIndex = 0;   // SetCells で登録したセルの index
		Matrix4x4 world;           // 重心ワールド配置（回転×移動）
		float     alpha = 1.0f;
		float     satBoost = 1.0f; // 反転色の彩度ブースト
	};

	/// <summary>ルートシグネチャ・PSO・セルごとの CB（256 バイト境界で maxCells 個）を作る</summary>
	/// <param name="dxCore">デバイスとコマンドリストの取得元</param>
	/// <param name="srvManager">キャプチャ SRV をバインドするのに使う</param>
	/// <param name="maxCells">1 回の Draw で描けるセル数の上限（最低 1）</param>
	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t maxCells);
	/// <summary>CB と VB をアンマップして、すべての GPU リソースを解放する</summary>
	void Finalize();

	/// <summary>
	/// 全セルの三角形を連結 VB にアップロードし、セルごとの頂点レンジを記録する（崩壊開始に1回）。
	/// </summary>
	/// <param name="cells">セルごとの三角形リスト（index が DrawItem::cellIndex になる）</param>
	void SetCells(const std::vector<CellMesh>& cells);

	/// <summary>
	/// 現在バインドされている RTV（=PostEffect後の最終RT）に対し、capture をサンプルして割れたセルを描画する。
	/// </summary>
	/// <param name="viewProjection">カメラのビュープロジェクション行列</param>
	/// <param name="captureSrvIndex">画面キャプチャの SRV 番号（ルート [1] の t0 にバインド）</param>
	/// <param name="items">描くセル（maxCells を超えた分は描かない）</param>
	void Draw(const Matrix4x4& viewProjection, uint32_t captureSrvIndex, const std::vector<DrawItem>& items);

private:
	/// <summary>ルートシグネチャを作る（[0]=b0 の ShardCB・全ステージ、[1]=PS t0 のキャプチャ SRV テーブル、s0=リニア・クランプ）</summary>
	void CreateRootSignature();
	/// <summary>PSO を作る（両面・α ブレンド・深度なし・DSV なし・sRGB の RT）</summary>
	void CreatePipelineState();
	/// <summary>セルごとの ShardCB を 256 バイト境界で maxCells 個並べたアップロードバッファを作ってマップする</summary>
	void CreateConstantBuffer();
	/// <summary>足りなければ VB を 1.5 倍以上の容量で作り直してマップする</summary>
	/// <param name="vertexCount">必要な頂点数</param>
	void EnsureVertexCapacity(uint32_t vertexCount);

	// per-cell cbuffer（シェーダの ShardCB と同レイアウト）
	struct ShardCB {
		Matrix4x4 wvp;
		float     alpha;
		float     satBoost;
		float     pad0;
		float     pad1;
	};

	struct CellRange {
		uint32_t start = 0; // 連結 VB 内の開始頂点
		uint32_t count = 0; // 頂点数
	};

	DirectXCore* dxCore_ = nullptr;
	SRVManager*  srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	// 全セル三角形を連結した動的アップロード VB（SetCells で書き込む）
	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	uint8_t* vbMapped_ = nullptr;
	uint32_t vbCapacityVerts_ = 0;
	D3D12_VERTEX_BUFFER_VIEW vbView_{};

	// セルごとの頂点レンジ
	std::vector<CellRange> cellRanges_;

	// per-cell cbuffer をまとめた1本のアップロードバッファ（256 アライン stride で区切る）
	Microsoft::WRL::ComPtr<ID3D12Resource> cbBuffer_;
	uint8_t* cbMapped_ = nullptr;
	uint32_t cbStride_ = 0;
	uint32_t maxCells_ = 0;
};
