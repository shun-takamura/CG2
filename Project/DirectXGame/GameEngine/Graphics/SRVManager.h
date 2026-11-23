#pragma once
#include"DirectXCore.h"
#include <wrl.h>
#include <cassert>
#include <queue>

// 前方宣言
class DirectXCore;

class SRVManager
{
	DirectXCore* dxCore_ = nullptr;

	// SRV用のディスクリプタサイズ
	uint32_t descriptorSize_;

	// SRV用のディスクリプタヒープ
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> descriptorHeap_;

	// 未使用スロットの先頭（一度も使っていない領域の先頭）
	uint32_t nextFresh_ = 0;

	// 解放済みスロットの再利用待ちキュー
	std::queue<uint32_t> freeList_;

public:

	// 最大SRV数(最大テクスチャ枚数)
	static const uint32_t kMaxSRVCount;

	/// <summary>シェーダーから見える CBV_SRV_UAV のディスクリプタヒープ（kMaxSRVCount 個）を作る</summary>
	/// <param name="dxCore">DirectXCoreへのポインタ</param>
	void Initialize(DirectXCore* dxCore);

	/// <summary>描画前処理。このヒープをコマンドリストにセットする（別のヒープをセットした後は呼び直す）</summary>
	void PreDraw();

	/// <summary>
	/// スロットを 1 つ確保する。返却済みのスロットがあればそれを再利用し、無ければ未使用の先頭を使う。
	/// 尽きたら 1 回だけログを出して assert する（Release では範囲外の番号が返る）
	/// </summary>
	/// <returns>確保したスロット番号</returns>
	uint32_t Allocate();

	/// <summary>スロットを返却する（テクスチャ破棄時に呼ぶ）。再利用待ちに入り、次の Allocate で使われる</summary>
	/// <param name="index">返却するスロット番号</param>
	void Free(uint32_t index);

	/// <summary>まだスロットを確保できるかを返す</summary>
	/// <returns>返却済みか未使用のスロットがあれば true</returns>
	bool CanAllocate() const {
		return !freeList_.empty() || nextFresh_ < kMaxSRVCount;
	}

	/// <summary>2D テクスチャ用の SRV を指定スロットに生成する</summary>
	/// <param name="srvIndex">書き込む先のスロット番号（Allocate で得た物）</param>
	/// <param name="pResource">ビューを作るリソース</param>
	/// <param name="format">ビューのフォーマット</param>
	/// <param name="MipLevels">見せるミップ数</param>
	void CreateSRVForTexture2D(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels);

	/// <summary>
	/// Cubemap用のSRVを指定スロットに生成する
	/// </summary>
	/// <param name="srvIndex">書き込む先のスロット番号（Allocate で得た物）</param>
	/// <param name="pResource">ビューを作るリソース</param>
	/// <param name="format">ビューのフォーマット</param>
	/// <param name="MipLevels">見せるミップ数</param>
	void CreateSRVForCubemap(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels);

	/// <summary>
	/// キューブでない配列テクスチャ用の SRV（Texture2DArray）を指定スロットに生成する
	/// </summary>
	/// <param name="srvIndex">書き込む先のスロット番号（Allocate で得た物）</param>
	/// <param name="pResource">ビューを作るリソース</param>
	/// <param name="format">ビューのフォーマット</param>
	/// <param name="MipLevels">見せるミップ数</param>
	/// <param name="arraySize">配列の枚数</param>
	void CreateSRVForTexture2DArray(uint32_t srvIndex, ID3D12Resource* pResource, DXGI_FORMAT format, UINT MipLevels, UINT arraySize);

	/// <summary>StructuredBuffer 用の SRV を指定スロットに生成する（先頭要素から全体）</summary>
	/// <param name="srvIndex">書き込む先のスロット番号（Allocate で得た物）</param>
	/// <param name="pResource">ビューを作るリソース</param>
	/// <param name="elementNums">要素数</param>
	/// <param name="structureByteStride">1 要素の大きさ [byte]</param>
	void CreateSRVForStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT elementNums, UINT structureByteStride);

	/// <summary>RWStructuredBuffer 用の UAV を指定スロットに生成する（カウンタなし）</summary>
	/// <param name="srvIndex">書き込む先のスロット番号（Allocate で得た物）</param>
	/// <param name="pResource">ビューを作るリソース</param>
	/// <param name="elementNums">要素数</param>
	/// <param name="structureByteStride">1 要素の大きさ [byte]</param>
	void CreateUAVForStructuredBuffer(uint32_t srvIndex, ID3D12Resource* pResource, UINT elementNums, UINT structureByteStride);

	/// <summary>指定スロットのディスクリプタをルートパラメータのディスクリプタテーブルとしてセットする（先に PreDraw 済みであること）</summary>
	/// <param name="RootParameterIndex">ルートパラメータの番号</param>
	/// <param name="srvIndex">セットするスロット番号</param>
	void SetGraphicsRootDescriptorTable(UINT RootParameterIndex, uint32_t srvIndex);

	/// <summary>指定スロットの CPU ディスクリプタハンドルを返す</summary>
	/// <param name="index">スロット番号</param>
	/// <returns>CPU ハンドル</returns>
	D3D12_CPU_DESCRIPTOR_HANDLE GetCPUDescriptorHandle(uint32_t index);
	/// <summary>指定スロットの GPU ディスクリプタハンドルを返す</summary>
	/// <param name="index">スロット番号</param>
	/// <returns>GPU ハンドル</returns>
	D3D12_GPU_DESCRIPTOR_HANDLE GetGPUDescriptorHandle(uint32_t index);

};

