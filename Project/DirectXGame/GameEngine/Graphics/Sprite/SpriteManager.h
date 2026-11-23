#pragma once  
#include "DirectXCore.h"  
#include <wrl.h>  
#include <d3d12.h>  
#include "DirectXTex.h"
#include <dxcapi.h>  
#include <array>
#include <utility>
#include <vector>
#include"SRVManager.h"

class SpriteManager {  

public:

   // ブレンドモード  
   enum BlendMode {  
       // ブレンド無し  
       kBlendModeNone,  

       // 通常αブレンド。デフォルト。Src*SrcA+Dest*(1-SrcA)  
       kBlendModeNormal,  

       // 加算 Src*SrcA+Dest*1  
       kBlendModeAdd,  

       // 減算 Dest*1-Src*SrcA  
       kBlendModeSubtract,  

       // 乗算 Src*0+Dest*Src
       kBlendModeMultily,  

       // スクリーン Src*(1-Dest)+Dest*1  
       kBlendModeScreen,  

       // 利用してはいけない
       kCountOfBlendMode
   };

   // ルートシグネチャのルートパラメータの番号（ルートシグネチャを作る SpriteManager と、描画で積む SpriteInstance で同じ番号を使う）
   enum RootParam : UINT {
       kRootMaterial = 0,        // PS b0：マテリアル
       kRootTransformation = 1,  // VS b0：座標変換行列
       kRootTexture = 2,         // PS t0：テクスチャ（テーブル）
       kRootCount = 3,
   };

   BlendMode blendMode_ = kBlendModeNormal;

private:  

   DirectXCore* dxCore_ = nullptr;
   SRVManager* srvManager_ = nullptr;
   
   Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

   // 全ブレンドモード分の PSO を保持する
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

   int currentBlendMode_ = 0;

   // Texture データ保持
   struct TextureInfo {
       Microsoft::WRL::ComPtr<ID3D12Resource> resource;
       D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
   };

public:  

   /// <summary>ルートシグネチャと PSO を作る</summary>
   /// <param name="dxCore">DirectXCoreへのポインタ</param>
   /// <param name="srvManager">SRVManagerへのポインタ</param>
   void Initialize( DirectXCore* dxCore, SRVManager* srvManager);
   /// <summary>スプライトを描く前の共通設定（三角形リスト・ルートシグネチャ・PSO）をコマンドリストに積む</summary>
   void DrawSetting();  
   /// <summary>
   /// ブレンドモードを切り替え、pipelineStates_ から対応する PSO を選ぶ（今と同じなら何もしない）。
   /// pipelineStates_ はどこからも作られていないので、切り替えると PSO が空になる点に注意
   /// </summary>
   /// <param name="blendMode">ブレンドモード</param>
   void SetBlendMode(BlendMode blendMode);

   // 中間バッファは DirectXCore に集約（テクスチャ/バッファ共通の fence pairing 機構）

   /// <summary>DirectXCore を返す</summary>
   /// <returns>Initialize で受け取った DirectXCore</returns>
   DirectXCore* GetDxCore() const { return dxCore_; }
   /// <summary>今のブレンドモードを返す</summary>
   /// <returns>ブレンドモード</returns>
   BlendMode GetBlendMode() const { return blendMode_; }  

   /// <summary>ルートシグネチャと全 PSO を解放するデストラクタ</summary>
   ~SpriteManager();

   /// <summary>ルートシグネチャと全 PSO を解放する</summary>
   void Release() {  
       rootSignature_.Reset();  
       pipelineState_.Reset();  
       for (auto& pipelineState : pipelineStates_) {  
           pipelineState.Reset();  
       }  
   }  

   /// <summary>
   /// DirectX12のTextureResourceを作る関数（DEFAULT ヒープの 2D テクスチャ、初期状態 COPY_DEST）
   /// </summary>
   /// <param name="device">デバイス</param>
   /// <param name="metadata">テクスチャの大きさ・ミップ数・配列数・フォーマット</param>
   /// <returns>作ったテクスチャリソース</returns>
   Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
       Microsoft::WRL::ComPtr<ID3D12Device> device, 
       const DirectX::TexMetadata& metadata
   );

   /// <summary>
   /// 全ミップを中間バッファ経由でテクスチャへ転送するコマンドを積み、COPY_DEST → GENERIC_READ に遷移する。
   /// 中間バッファは DirectXCore に預け、GPU の完了後に解放される
   /// </summary>
   /// <param name="texture">転送先のテクスチャ（COPY_DEST 状態）</param>
   /// <param name="mipImages">転送するミップ付きの画像</param>
   /// <param name="device">デバイス</param>
   /// <param name="commandList">コピーを積むコマンドリスト</param>
   /// <returns>使った中間バッファ</returns>
   Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
       Microsoft::WRL::ComPtr<ID3D12Resource> texture,
       const DirectX::ScratchImage& mipImages,
       Microsoft::WRL::ComPtr<ID3D12Device> device, 
       ID3D12GraphicsCommandList* commandList
   );

private:  
   /// <summary>
   /// ルートシグネチャを作る。[0]=PS b0（マテリアル）、[1]=VS b0（座標変換行列）、[2]=t0（テクスチャ）、s0=linear wrap
   /// </summary>
   void CreateRootSignature();  
   /// <summary>
   /// Sprite.VS/PS の PSO を作って pipelineState_ に入れる（入力は POSITION・TEXCOORD、カリングなし、深度書き込みあり LESS_EQUAL、
   /// RTV は sRGB、DSV は D24S8）。ブレンドは引数ではなく今の blendMode_ で決まる
   /// </summary>
   /// <param name="mode">未使用（ブレンドは blendMode_ を見る）</param>
   void CreateGraphicsPipelineState(BlendMode mode);
   
   /// <summary>
   /// テクスチャを WIC で sRGB として読み込み、ミップマップを作る関数
   /// </summary>
   /// <param name="filePath">画像ファイルのパス</param>
   /// <returns>ミップ付きの画像</returns>
   DirectX::ScratchImage LoadTexture(const std::string& filePath);

};