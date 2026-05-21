// R8 アトラスをアルファとして読み、color と乗算して出力する。

Texture2D<float>   gAtlas   : register(t0);
SamplerState       gSampler : register(s0);

struct PSInput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

float4 main(PSInput input) : SV_TARGET0
{
    float alpha = gAtlas.Sample(gSampler, input.uv);
    float4 outColor = float4(input.color.rgb, input.color.a * alpha);
    if (outColor.a <= 0.001f) discard;
    return outColor;
}
