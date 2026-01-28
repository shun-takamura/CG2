// Sepia.PS.hlsl
// セピア調専用のピクセルシェーダー

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
    float grayscaleIntensity;    // offset: 0
    float sepiaIntensity;        // offset: 4
    float3 sepiaColor;           // offset: 8-19
    float _padding1;             // offset: 20
    float vignetteIntensity;     // offset: 24
    float vignettePower;         // offset: 28
    float vignetteScale;         // offset: 32
    float _padding2;             // offset: 36
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 元の色をサンプリング
    float4 color = gTexture.Sample(gSampler, input.texcoord);
    
    // グレースケール値を計算（BT.709）
    float grayscaleValue = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    
    // セピア色を適用
    // 赤成分が255(1.0)最大になるようにスケール調整
    float3 sepia = grayscaleValue * sepiaColor;
    
    // 元の色とセピア色をブレンド
    output.color.rgb = lerp(color.rgb, sepia, sepiaIntensity);
    output.color.a = color.a;
    
    return output;
}
