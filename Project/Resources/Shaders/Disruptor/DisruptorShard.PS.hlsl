// DisruptorShard.PS.hlsl
// シーンキャプチャ（PostEffect 前の通常色シーン）を baked UV でサンプル → 色反転 → α でブレンド。
// 破片は PostEffect の後に描く（二重反転しない）。baked UV ＝破片が剥がれた画面位置なので、
// 破片は「自分の場所の反転絵」を持ったまま飛ぶ（割れたガラスが画面のかけらを運ぶイメージ）。

struct ShardCB
{
    float4x4 wvp;
    float    alpha;
    float    satBoost;
    float2   pad;
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
    float3 c = gCapture.Sample(gSampler, input.uv).rgb;
    float3 inverted = 1.0f - c;
    // 彩度ブースト：グレー成分から離す＝拾った色を強調（背景に溶けて灰色化するのを補う）
    float gray = dot(inverted, float3(0.299f, 0.587f, 0.114f));
    inverted = lerp(float3(gray, gray, gray), inverted, gShard.satBoost);
    inverted = saturate(inverted);

    output.color = float4(inverted, gShard.alpha);
    return output;
}
