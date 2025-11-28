#pragma once  
#include "DirectXCore.h"  
#include <wrl.h>  
#include <d3d12.h>  
#include "DirectXTex.h"
#include <dxcapi.h>  
#include <array>

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

   BlendMode blendMode_ = kBlendModeNormal;

private:  

   DirectXCore* dxCore_ = nullptr;
   
   Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_ = nullptr;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_ = nullptr;

   // 全ブレンドモード分の PSO を保持する
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

   int currentBlendMode_ = 0;

   // SRV用ヒープ
   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;

   // 次に使うSRVのインデックス
   uint32_t nextSrvIndex_ = 0;

   // Texture データ保持
   struct TextureInfo {
       Microsoft::WRL::ComPtr<ID3D12Resource> resource;
       D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;
   };
   std::vector<TextureInfo> textures_;

public:  

   void Initialize( DirectXCore* dxCore);  
   void DrawSetting();  
   void SetBlendMode(BlendMode blendMode);

   std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> intermediateResources_;
   void ClearIntermediateResources();

   /// <summary>
   /// テクスチャをロードしてGPUに転送する関数
   /// </summary>
   /// <param name="filePath"></param>
   /// <returns></returns>
   D3D12_GPU_DESCRIPTOR_HANDLE LoadTextureToGPU(const std::string& filePath);

   DirectXCore* GetDxCore() const { return dxCore_; }   
   BlendMode GetBlendMode() const { return blendMode_; }  

   // Releaseメソッドを追加  
   void Release() {  
       rootSignature_.Reset();  
       pipelineState_.Reset();  
       for (auto& pipelineState : pipelineStates_) {  
           pipelineState.Reset();  
       }  
   }  

private:  
   void CreateRootSignature();  
   void CreateGraphicsPipelineState(BlendMode mode);
   void CreateDescriptorHeap();
   
   /// <summary>
   /// テクスチャをロードする関数
   /// </summary>
   /// <param name="filePath"></param>
   /// <returns></returns>
   DirectX::ScratchImage LoadTexture(const std::string& filePath);

   /// <summary>
   /// DirectX12のTetureResourceを作る関数
   /// </summary>
   /// <param name="device"></param>
   /// <param name="metadata"></param>
   /// <returns></returns>
   Microsoft::WRL::ComPtr<ID3D12Resource> CreateTextureResource(
       Microsoft::WRL::ComPtr<ID3D12Device> device, 
       const DirectX::TexMetadata& metadata
   );

   Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(
       Microsoft::WRL::ComPtr<ID3D12Resource> texture,
       const DirectX::ScratchImage& mipImages,
       Microsoft::WRL::ComPtr<ID3D12Device> device, 
       ID3D12GraphicsCommandList* commandList
   );

};