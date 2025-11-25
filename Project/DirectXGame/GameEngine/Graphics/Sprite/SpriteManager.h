#pragma once  
#include "DirectXCore.h"  
#include <wrl.h>  
#include <d3d12.h>  
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

   IDxcUtils* dxcUtils_ = nullptr;
   IDxcCompiler3* dxcCompiler_ = nullptr;
   IDxcIncludeHandler* includeHandler_ = nullptr;

   Microsoft::WRL::ComPtr<ID3D12RootSignature> rootSignature_;
   Microsoft::WRL::ComPtr<ID3D12PipelineState> pipelineState_;

   // 全ブレンドモード分の PSO を保持する
   std::array<Microsoft::WRL::ComPtr<ID3D12PipelineState>, kCountOfBlendMode> pipelineStates_;

   int currentBlendMode_ = 0;

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

private:  
   void CreateRootSignature();  
   void CreateAllPipelineStates();  
   Microsoft::WRL::ComPtr<ID3D12PipelineState> CreateGraphicsPipelineState(BlendMode mode);  

};