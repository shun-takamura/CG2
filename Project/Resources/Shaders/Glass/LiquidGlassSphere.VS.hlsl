#include "LiquidGlassSphere.hlsli"

cbuffer GlassTransformBuffer : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

struct VertexShaderInput
{
    float3 position : POSITION0;
};

GlassVertexOutput main(VertexShaderInput input)
{
    GlassVertexOutput output;
    float4 position = float4(input.position, 1.0f);
    output.position = mul(position, gWVP);
    output.worldPosition = mul(position, gWorld).xyz;
    return output;
}
