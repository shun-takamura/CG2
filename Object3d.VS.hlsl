#include "Object3d.hlsli"

struct TransformationMatrix{
    float4x4 WVP;
};

//ConstantBuffer<TransformationMatrix> gTransformatMatrix : register(b0);

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix gTransformatMatrix;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float4 texcoord : TEXCOORD0;
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, gTransformatMatrix.WVP);
    return output;
}