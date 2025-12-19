Texture2D tex : register(t0);
SamplerState smp : register(s0);

cbuffer Material : register(b0)
{
    float4 color;
};

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD) : SV_TARGET
{
    float4 texColor = tex.Sample(smp, uv);
    return texColor * color;
}