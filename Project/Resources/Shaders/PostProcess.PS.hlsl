// PostProcess.PS.hlsl

#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PostProcessParams : register(b0)
{
    // C++とメモリのレイアウトが異なるので注意
    // C++ではfloat3は詰めるけどHLSLは詰めないで16byte単位に揃える
    // だから全部floatにして強制的に詰める
    float grayscaleIntensity; // offset: 0
    float sepiaIntensity; // offset: 4
    float sepiaColorR; // offset: 8
    float sepiaColorG; // offset: 12
    float sepiaColorB; // offset: 16
    float _padding1; // offset: 20
    float vignetteIntensity; // offset: 24
    float vignettePower; // offset: 28
    float vignetteScale; // offset: 32
    float _padding2; // offset: 36
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 元の色をサンプリング
    float4 color = gTexture.Sample(gSampler, input.texcoord);
    
    // 1. グレースケール変換 (BT.709方式)
    float grayscaleValue = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    float3 grayscale = float3(grayscaleValue, grayscaleValue, grayscaleValue);
    
    // グレースケールをブレンド
    color.rgb = lerp(color.rgb, grayscale, grayscaleIntensity);
    
    // 2. セピア調効果
    if (sepiaIntensity > 0.0f)
    {
        float3 sepiaColor = float3(sepiaColorR, sepiaColorG, sepiaColorB);
        float3 sepia = grayscaleValue * sepiaColor;
        color.rgb = lerp(color.rgb, sepia, sepiaIntensity);
    }
    
    // 3. ヴィネット効果
    if (vignetteIntensity > 0.0f)
    {
        float2 uv = input.texcoord * (1.0f - input.texcoord);
        float vignette = uv.x * uv.y * vignetteScale;
        vignette = pow(vignette, 1.0f / vignettePower);
        vignette = saturate(vignette);
        color.rgb *= lerp(1.0f, vignette, vignetteIntensity);
    }
    
    output.color = color;
    return output;
}