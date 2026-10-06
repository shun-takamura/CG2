// LightShaft.PS.hlsl
// スクリーンスペースのライトシャフト（GPU Gems 3 13章「Volumetric Light Scattering as a Post-Process」）。
// IDマスク（R8_UINT）が targetId のピクセルだけを光源とし、各ピクセルから光源の中心へ向かって
// マスクをサンプルして減衰をかけながら足し合わせ、加算する（放射状の筋＋後光）。
// outline 用 root signature（color t0 + aux t1 + cbuffer b0 + linear s0 + point s1）を共用。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
Texture2D<uint>   gIdMask  : register(t1);
SamplerState      gSampler : register(s0);
SamplerState      gPoint   : register(s1);

cbuffer LightShaftParams : register(b0)
{
    float2 lightUV;     // 光源の中心（画面 UV）
    float density;      // 光源へ向かってどこまでサンプルするか（1=光源の中心まで）
    float decay;        // 1サンプルごとの減衰
    float3 shaftColor;
    float weight;       // 1サンプルあたりの寄与
    float intensity;
    uint sampleCount;
    uint targetId;
    float padding;
};

static const uint kMaxSamples = 128; // LightShaftEffect.cpp と合わせる

float LoadMask(float2 uv, uint2 size)
{
    int2 idx = clamp(int2(uv * float2(size)), int2(0, 0), int2(size) - int2(1, 1));
    return (gIdMask.Load(int3(idx, 0)) == targetId) ? 1.0f : 0.0f;
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    float4 color = gTexture.Sample(gSampler, input.texcoord);
    output.color = color;

    uint2 size;
    gIdMask.GetDimensions(size.x, size.y);

    // 2値のマスクを等間隔で拾うと縞が出るので、ピクセルごとに開始位置をずらす（interleaved gradient noise）
    float2 pixel = input.texcoord * float2(size);
    float jitter = frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));

    float2 delta = (input.texcoord - lightUV) * (density / (float) sampleCount);
    float2 uv = input.texcoord - delta * jitter;
    float illumination = 1.0f;
    float sum = 0.0f;

    [loop]
    for (uint i = 0; i < kMaxSamples; ++i)
    {
        if (i >= sampleCount)
        {
            break;
        }
        uv -= delta;
        sum += LoadMask(uv, size) * illumination * weight;
        illumination *= decay;
    }

    output.color.rgb = color.rgb + shaftColor * (sum * intensity);
    return output;
}
