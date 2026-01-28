// Vignette.PS.hlsl
// ヴィネット効果専用のピクセルシェーダー（周辺減光）

#include "PostProcess.hlsli"

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// PostProcessと同じ定数バッファ構造を使用
cbuffer PostProcessParams : register(b0)
{
    float grayscaleIntensity; // offset: 0
    float sepiaIntensity; // offset: 4
    float3 sepiaColor; // offset: 8-19 (float3は12バイト)
    float _padding1; // offset: 20 (float3の後のパディング)
    float vignetteIntensity; // offset: 24
    float vignettePower; // offset: 28
    float vignetteScale; // offset: 32
    float _padding2; // offset: 36
};

PixelShaderOutput main(VertexShaderOutput input)
{
    
    PixelShaderOutput output;
    
    // 元の色をサンプリング
    output.color = gTexture.Sample(gSampler, input.texcoord);
    
    // 中心からの距離を計算
    float2 correct = input.texcoord * (1.0f - input.texcoord.yx);
        
    // 中心が明るく、周辺が暗くなるグラデーション
    float vignette = correct.x * correct.y * 16.0f;
        
    // カーブ調整
    vignette = pow(vignette, 0.8f);
        
    // 0〜1にクランプ
    vignette = saturate(vignette);
        
    output.color.rgb *= vignette;
    
    return output;
}