#include "Object3d.hlsli"

//cbuffer gTransformationMatrix : register(b0)
//{
//    float4x4 wvp;
//    float4x4 world;
//};

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

//ConstantBuffer<TransformationMatrix> gTransformationMatrix : register(b0);

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix transformationMatrix;
};


struct Material{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
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
    output.position = mul(input.position, transformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) transformationMatrix.World));
    return output;
}