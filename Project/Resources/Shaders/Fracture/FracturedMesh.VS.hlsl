#include "FracturedMesh.hlsli"

struct VertexShaderInput
{
    float3 position : POSITION0;
    float3 normal : NORMAL0;
    float interior : TEXCOORD0;
    uint fragment : FRAGMENT0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    // 割る前の岩は 1 インスタンス = 1 個の岩、破片は 1 インスタンス = 1 個の岩の全破片
    uint index = gInstanceBase + (gFragmentMode == 0 ? instanceId : instanceId * gFragmentCount + input.fragment);
    InstanceData inst = gInstances[index];

    float4 world = mul(float4(input.position, 1.0f), inst.world);

    VertexShaderOutput output;
    output.position = mul(world, gViewProjection);
    output.worldPosition = world.xyz;
    output.rockPosition = input.position;
    output.rockNormal = input.normal;
    // 拡大縮小は外して向きだけにする（法線用）
    output.axis0 = normalize(inst.world[0].xyz);
    output.axis1 = normalize(inst.world[1].xyz);
    output.axis2 = normalize(inst.world[2].xyz);
    output.interior = input.interior;
    output.visibility = inst.params.x;
    output.textureScale = inst.params.y;
    return output;
}
