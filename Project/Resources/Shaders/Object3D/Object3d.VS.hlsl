#include "Object3d.hlsli"

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix transformationMatrix;
};


struct Material
{
    float4 color;
    int enableLighting;
    float4x4 uvTransform;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;  // xyz=接線, w=handedness
};

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;
    output.position = mul(input.position, transformationMatrix.WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) transformationMatrix.WorldInverseTranspose));
    output.worldPosition = mul(input.position, transformationMatrix.World).xyz;

    // 接線をワールドへ。法線に対してグラムシュミット直交化し、従法線は handedness で復元
    float3 T = normalize(mul(input.tangent.xyz, (float3x3) transformationMatrix.World));
    T = normalize(T - output.normal * dot(output.normal, T));
    output.tangent = T;
    output.bitangent = cross(output.normal, T) * input.tangent.w;
    return output;
}