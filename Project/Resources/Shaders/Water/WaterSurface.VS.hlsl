#include "WaterSurface.hlsli"

cbuffer WaterTransformBuffer : register(b0)
{
    float4x4 gWVP;
    float4x4 gWorld;
};

struct VertexShaderInput
{
    float3 position : POSITION0;
};

WaterVertexOutput main(VertexShaderInput input)
{
    WaterVertexOutput output;
    float4 position = float4(input.position, 1.0f);
    output.position = mul(position, gWVP);
    output.worldPosition = mul(position, gWorld).xyz;
    return output;
}
