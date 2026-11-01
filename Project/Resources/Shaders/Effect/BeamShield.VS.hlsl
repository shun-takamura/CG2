// BeamShield.VS.hlsl
// ビームシールドの板（BeamShieldRenderer.cpp）。[-1, 1] の正方形を quadRect（ローカル）へ広げ、
// ローカルの XY（原点＝発生装置）を PS へ渡す。形はすべて PS が作る。

cbuffer BeamShieldTransform : register(b0)
{
    float4x4 gWVP;
    float4 gQuadRect; // minX, minY, maxX, maxY
};

struct VertexShaderInput
{
    float2 position : POSITION0;
};

struct VertexShaderOutput
{
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    float2 local = lerp(gQuadRect.xy, gQuadRect.zw, input.position * 0.5f + 0.5f);
    output.position = mul(float4(local, 0.0f, 1.0f), gWVP);
    output.local = local;
    return output;
}
