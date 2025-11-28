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
   // SRV 用 DescriptorHeap（ShaderVisible = true）
   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvDescriptorHeap_;

   // SRV の現在の書き込み位置（= 次に使うスロット index）
   uint32_t currentSrvIndex_ = 0;

   IDxcUtils* dxcUtils_ = nullptr;
   IDxcCompiler3* dxcCompiler_ = nullptr;
   IDxcIncludeHandler* includeHandler_ = nullptr;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

   // 全ブレンドモード分の PSO を保持する
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

   int currentBlendMode_ = 0;

   struct TextureEntry
   {
       Microsoft::WRL::ComPtr<ID3D12Resource> resource;  // GPU上のテクスチャ
       D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle;            // SRV の GPU ハンドル
   };

   struct TextureInfo {
       Microsoft::WRL::ComPtr<ID3D12Resource> resource;
       D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
       D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
   };

   std::vector<TextureEntry> textures_;

public:  

   IDxcBlob* CompileShader(  
       const std::wstring& filePath,  
       const wchar_t* profile,   
       IDxcUtils* dxcUtils,  
       IDxcCompiler3* dxcCompiler,  
       IDxcIncludeHandler* includeHandler  
   );  

   void Initialize(  
       DirectXCore* dxCore,  
       IDxcUtils* dxcUtils,  
       IDxcCompiler3* dxcCompiler,  
       IDxcIncludeHandler* includeHandler  
   );  

   void CreateSrvHeap();

   /// <summary>
   /// Textureデータを読み込む関数
   /// </summary>
   /// <param name="filename">ファイルパス</param>
   /// <returns>ミップマップ付きのデータ</returns>
   DirectX::ScratchImage LoadTexture(const std::string& filePath);

   /// <summary>
   /// DirectX12のTetureResourceを作る関数
   /// </summary>
   /// <param name="device"></param>
   /// <param name="metadata"></param>
   /// <returns></returns>
   Microsoft::WRL::ComPtr<ID3D12Resource>CreateTextureResource(const DirectX::TexMetadata& metadata);

   /// <summary>
   /// TextureResourceにデータを転送する関数
   /// </summary>
   /// <param name="texture"></param>
   /// <param name="mipImages"></param>
   void UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages);

   [[nodiscard]]
   Microsoft::WRL::ComPtr<ID3D12Resource> UploadTextureData(Microsoft::WRL::ComPtr<ID3D12Resource> texture, const DirectX::ScratchImage& mipImages, Microsoft::WRL::ComPtr<ID3D12Device> device, ID3D12GraphicsCommandList* commandList);


   D3D12_GPU_DESCRIPTOR_HANDLE LoadTextureAndCreateSrv(const std::string& filePath);

   D3D12_GPU_DESCRIPTOR_HANDLE AllocateAndCreateSrv(
       Microsoft::WRL::ComPtr<ID3D12Resource> texture,
       const DirectX::TexMetadata& metadata);

   void CommonDrawSetting();  
   void SetBlendMode(BlendMode blendMode);

   DirectXCore* GetDxCore() const { return dxCore_; }  
   ID3D12RootSignature* GetRootSignature() const { return rootSignature_.Get(); }  
   ID3D12PipelineState* GetPipelineState() const { return pipelineState_.Get(); }  
   BlendMode GetBlendMode() const { return blendMode_; }  

   // Releaseメソッドを追加  
   void Release() {  
       rootSignature_.Reset();  
       pipelineState_.Reset();  
       for (auto& pipelineState : pipelineStates_) {  
           pipelineState.Reset();  
       }  
   }  

  

   // SRV用 DescriptorHeap
   Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
   uint32_t srvDescriptorSize_ = 0;
   uint32_t nextSrvIndex_ = 0;

   // テクスチャ読み込み
   uint32_t LoadTextureToGPU(const std::string& filePath);
   D3D12_GPU_DESCRIPTOR_HANDLE GetTextureHandle(uint32_t textureID) const;
   Microsoft::WRL::ComPtr<ID3D12Resource> CreateBufferResource(size_t sizeInBytes);

private:  
   void CreateRootSignature();  
   void CreateAllPipelineStates();  
   Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipelineState(BlendMode mode);  

};