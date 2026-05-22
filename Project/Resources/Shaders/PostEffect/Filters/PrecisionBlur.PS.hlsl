// PrecisionBlur.PS.hlsl
// 精密射撃モード用：画面中央はくっきり、周辺ほどガウスぼかしを強める。
// 中央からの正規化距離で「くっきり範囲(innerRadius)→最大ぼかし」を補間し、
// その量だけサンプル間隔を広げてガウス加重サンプルする。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

cbuffer PrecisionBlurParams : register(b0)
{
    float intensity;   // 全体の効き（0=無効, 1=最大）。フェードイン/アウトに使う
    float innerRadius; // くっきり範囲（中央からの正規化距離 0〜1）
    float falloff;     // innerRadius から最大ぼかしへ遷移する幅
    float maxBlurPx;   // 周辺の最大ぼかし量（ピクセル）
    float vignette;    // 周辺減光の強さ（0=なし, 1=最大）
    float3 _padding;
};

static const int kRadius = 4; // 9x9 タップ

float gauss2(float x, float y, float sigma)
{
    float e = -(x * x + y * y) * rcp(2.0f * sigma * sigma);
    return exp(e);
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;

    float3 sharp = gTexture.Sample(gSampler, input.texcoord).rgb;

    int width, height;
    gTexture.GetDimensions(width, height);
    float aspect = (float) width / (float) height;

    // アスペクト補正して円形の中央距離を作る（0=中央, 1=隅）
    float2 d = input.texcoord - 0.5f;
    d.x *= aspect;
    float dist = length(d) * rcp(length(float2(0.5f * aspect, 0.5f)));

    // くっきり範囲を超えた分でぼかし強度 t を決める
    float t = saturate((dist - innerRadius) * rcp(max(falloff, 1e-4f)));

    float3 blurred = sharp;
    if (t > 0.001f && intensity > 0.001f)
    {
        float2 uvStep = float2(rcp((float) width), rcp((float) height))
                      * (maxBlurPx * t * rcp((float) kRadius));
        float sigma = (float) kRadius * 0.5f;

        float3 acc = float3(0.0f, 0.0f, 0.0f);
        float wsum = 0.0f;
        for (int x = -kRadius; x <= kRadius; ++x)
        {
            for (int y = -kRadius; y <= kRadius; ++y)
            {
                float w = gauss2((float) x, (float) y, sigma);
                acc += gTexture.Sample(gSampler, input.texcoord + float2((float) x, (float) y) * uvStep).rgb * w;
                wsum += w;
            }
        }
        blurred = acc * rcp(wsum);
    }

    // 距離によるブレンド → さらに全体 intensity でフェード
    float3 result = lerp(sharp, blurred, t);
    result = lerp(sharp, result, saturate(intensity));

    // 周辺減光（くっきり範囲は減光せず、周辺ほど暗く）。intensity でフェード
    float vig = vignette * smoothstep(innerRadius, 1.0f, dist) * saturate(intensity);
    result *= (1.0f - vig);

    output.color = float4(result, 1.0f);
    return output;
}
