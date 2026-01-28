// PostProcess.PS.hlsl
// ポストプロセスエフェクト：グレースケール、セピア調、ヴィネット

#include "PostProcess.hlsli"

// ピクセルシェーダー出力構造体
struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// テクスチャとサンプラー
Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// 定数バッファ
cbuffer PostProcessParams : register(b0)
{
    // グレースケール
    float grayscaleIntensity;      // offset: 0
    
    // セピア調
    float sepiaIntensity;          // offset: 4
    float3 sepiaColor;             // offset: 8-19 (12バイト)
    
    float _padding1;               // offset: 20 (float3の後のパディング)
    
    // ヴィネット
    float vignetteIntensity;       // offset: 24
    float vignettePower;           // offset: 28
    float vignetteScale;           // offset: 32
    
    float _padding2;               // offset: 36
};

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // 元の色をサンプリング
    float4 color = gTexture.Sample(gSampler, input.texcoord);
    
    // 1. グレースケール変換 (BT.709方式)
    // 人間の目は緑成分に対して最も明るさを感じるため、重み付けを行う
    float grayscaleValue = dot(color.rgb, float3(0.2125f, 0.7154f, 0.0721f));
    float3 grayscale = float3(grayscaleValue, grayscaleValue, grayscaleValue);
    
    // グレースケールをブレンド
    color.rgb = lerp(color.rgb, grayscale, grayscaleIntensity);
    
    // 2. セピア調効果
    if (sepiaIntensity > 0.0f)
    {
        // グレースケール値を使用してセピア色を適用
        float3 sepia = grayscaleValue * sepiaColor;
        color.rgb = lerp(color.rgb, sepia, sepiaIntensity);
    }
    
    // 3. ヴィネット効果
    if (vignetteIntensity > 0.0f)
    {
        // 中心からの距離を計算
        float2 uv = input.texcoord * (1.0f - input.texcoord);
        
        // 中心が明るく、周辺が暗くなるグラデーション
        float vignette = uv.x * uv.y * vignetteScale;
        
        // カーブ調整（1.0 / powerで逆数を取る）
        vignette = pow(vignette, 1.0f / vignettePower);
        
        // 0〜1にクランプ
        vignette = saturate(vignette);
        
        // エフェクトを適用
        color.rgb *= lerp(1.0f, vignette, vignetteIntensity);
    }
    
    output.color = color;
    return output;
}