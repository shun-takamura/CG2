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
/// ディスラプター崩壊の破片レンダラ（セル方式）。
/// 画面を事前分割した凸セル（多角形）を三角形化し、各頂点に baked UV（剥がれた画面位置）を持たせて
/// 1本の連結頂点バッファにアップロードする（SetCells、崩壊開始に1回）。
/// 描画は「割れたセルだけ」を per-cell の WVP（重心回転＋飛散移動）で 1 ドローコールずつ描く（Draw）。
/// PS でシーンキャプチャを baked UV でサンプル→色反転＝破片が自分の場所の反転絵を持って飛ぶ。
/// PostEffect の後に描く前提（深度なし・DSVなし）なので二重反転しない。
/// </summary>
class DisruptorShardRenderer
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

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t maxCells);
	void Finalize();

	/// <summary>
	/// 全セルの三角形を連結 VB にアップロードし、セルごとの頂点レンジを記録する（崩壊開始に1回）。
	/// </summary>
	void SetCells(const std::vector<CellMesh>& cells);

	/// <summary>
	/// 現在バインドされている RTV（=PostEffect後の最終RT）に対し、capture をサンプルして割れたセルを描画する。
	/// </summary>
	void Draw(const Matrix4x4& viewProjection, uint32_t captureSrvIndex, const std::vector<DrawItem>& items);

private:
	void CreateRootSignature();
	void CreatePipelineState();
	void CreateConstantBuffer();
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
