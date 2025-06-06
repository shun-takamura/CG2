#include "Object3d.hlsli"

struct TransformationMatrix{
    float4x4 WVP;
    float4x4 World;
};

struct DirectionalLight{
    float4 color;
    float3 direction;
    float intensity;
};

//ConstantBuffer<TransformationMatrix> gTransformatMatrix : register(b0);

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix gTransformatMatrix;
};

struct Material{
    float4 color;
    int enableLighting;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformatMatrix.WVP);
    output.texcoord = input.texcoord;
    return output;
}