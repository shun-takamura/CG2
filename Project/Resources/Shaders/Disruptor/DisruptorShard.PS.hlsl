// DisruptorShard.PS.hlsl
// シーンキャプチャをサンプル → 色反転（反転世界の殻のかけら）→ α でフェード。
// 破片は通常色領域（PostEffect 非反転側）を進むので、ここで反転して描けば反転殻に見える。

struct ShardCB
{
    float4x4 wvp;
    float2   uvMin;
    float2   uvSize;
    float    alpha;
    float    distortAmount;
    float    distortFreq;
    float    _pad;
};
ConstantBuffer<ShardCB> gShard : register(b0);

Texture2D<float4> gCapture : register(t0);
SamplerState      gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
};

struct PSOutput
{
    float4 color : SV_TARGET0;
};

PSOutput main(PSInput input)
{
    PSOutput output;
    // ガラスの屈折感：サンプリング UV を sin で少し歪ませる
    float2 off = float2(sin(input.uv.y * gShard.distortFreq * 6.2831853f),
                        cos(input.uv.x * gShard.distortFreq * 6.2831853f)) * gShard.distortAmount;
    float2 uv = clamp(input.uv + off, 0.0f, 1.0f);
    float3 c = gCapture.Sample(gSampler, uv).rgb;
    float3 inverted = 1.0f - c;
    output.color = float4(inverted, gShard.alpha);
    return output;
}
