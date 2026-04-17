#include "PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer GaussianParams : register(b0)
{
    int kernelSize;
    float sigma;
};

static const float PI = 3.14159265;

float gauss(float x, float y, float sigma){
    float exponent = -(x * x + y * y) * rcp(2.0f * sigma * sigma);
    float denominator = 2.0f * PI * sigma * sigma;
    return exp(exponent) * rcp(denominator);
}

// ループ上限の半径（最大15x15まで対応）
// これ以上大きいカーネルが必要なら値を増やす
static const int kMaxRadius = 7;

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    
    // uvStepSizeを算出
    int width, height;
    gTexture.GetDimensions(width, height);
    float2 uvStepSize = float2(rcp((float) width), rcp((float) height));
    
    float weight = 0.0f;
    int radius = kernelSize / 2;
    
    float3 resultColor = float3(0.0f, 0.0f, 0.0f);

    // カーネルサイズ分のループを回す
    // 実際のカーネル範囲外はif文でスキップ
    for (int x = -kMaxRadius; x <= kMaxRadius; ++x)
    {
        for (int y = -kMaxRadius; y <= kMaxRadius; ++y)
        {
            // cbufferで指定されたradius範囲外はスキップ
            if (abs(x) > radius || abs(y) > radius)
            {
                continue;
            }

            // ピクセルの重みをガウス関数で計算
            float w = gauss((float) x, (float) y, sigma);
            weight += w;
            
            // 色をサンプリングして重みを掛けて足す
            float2 texcoord = input.texcoord + float2((float) x, (float) y) * uvStepSize;
            float3 fetchColor = gTexture.Sample(gSampler, texcoord).rgb;
            resultColor += fetchColor * w;
        }
    }

    
    // resultColorを正規化して出力
    resultColor *= rcp(weight);
    output.color = float4(resultColor, 1.0f);
    return output;
}