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
/// ディスラプター崩壊の破片レンダラ。
/// シーンキャプチャ（反転世界の元絵）の一部を貼った 3D キューブ群を、専用パイプラインで描く。
/// 各破片＝per-shard cbuffer（WVP＋サンプリングUV矩形＋α）でインスタンス的に1個ずつ描画。
/// PS でキャプチャをサンプルし色反転＝反転世界の殻のかけらとして見える。
/// </summary>
class DisruptorShardRenderer
{
public:
	struct Instance {
		Matrix4x4 world;            // 破片のワールド行列（scale 非一様＋回転＋移動）
		Vector2   uvMin{ 0.0f, 0.0f };   // 剥がれた画面位置（UV 左上）
		Vector2   uvSize{ 0.1f, 0.1f };  // UV サイズ
		float     alpha = 1.0f;
		float     distortAmount = 0.0f;  // サンプリングUVの歪み量（ガラスの屈折感）
		float     distortFreq = 12.0f;   // 歪みの細かさ
	};

	void Initialize(DirectXCore* dxCore, SRVManager* srvManager, uint32_t maxInstances);
	void Finalize();

	/// <summary>
	/// 現在バインドされている RTV/DSV（=シーン RT）に対し、capture をサンプルして全破片を描画する。
	/// </summary>
	void Draw(const Matrix4x4& viewProjection, uint32_t captureSrvIndex, const std::vector<Instance>& instances);

private:
	void CreateRootSignature();
	void CreatePipelineState();
	void CreateCubeMesh();
	void CreateInstanceBuffer();

	// per-shard cbuffer（シェーダの ShardCB と同レイアウト）
	struct ShardCB {
		Matrix4x4 wvp;
		float     uvMin[2];
		float     uvSize[2];
		float     alpha;
		float     distortAmount;
		float     distortFreq;
		float     _pad;
	};

	DirectXCore* dxCore_ = nullptr;
	SRVManager*  srvManager_ = nullptr;

	Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
	Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

	Microsoft::WRL::ComPtr<ID3D12Resource> vertexBuffer_;
	Microsoft::WRL::ComPtr<ID3D12Resource> indexBuffer_;
	D3D12_VERTEX_BUFFER_VIEW vbView_{};
	D3D12_INDEX_BUFFER_VIEW  ibView_{};
	uint32_t indexCount_ = 0;

	// per-shard cbuffer をまとめた1本のアップロードバッファ（256 アライン stride で区切る）
	Microsoft::WRL::ComPtr<ID3D12Resource> cbBuffer_;
	uint8_t* cbMapped_ = nullptr;
	uint32_t cbStride_ = 0;
	uint32_t maxInstances_ = 0;
};
