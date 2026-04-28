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

// MatrixPalette用のWell構造体（C++のWellForGPUと対応）
struct Well
{
    float4x4 skeletonSpaceMatrix; // 位置用
    float4x4 skeletonSpaceInverseTransposeMatrix; // 法線用
};

// Joint数分のWellが格納されるStructuredBuffer
StructuredBuffer<Well> gMatrixPalette : register(t0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 weight : WEIGHT0;
    int4 index : INDEX0;
};

// Skinningの結果を格納する構造体
struct Skinned
{
    float4 position;
    float3 normal;
};

// Skinningを行う関数
Skinned Skinning(VertexShaderInput input)
{
    Skinned skinned;

    // weightの合計を確認
    float totalWeight = input.weight.x + input.weight.y + input.weight.z + input.weight.w;

    if (totalWeight < 0.001f)
    {
        // Skinning用のWeightが無い頂点はそのまま使用
        skinned.position = input.position;
        skinned.normal = input.normal;
        return skinned;
    }

    // 位置の変換
    skinned.position = mul(input.position, gMatrixPalette[input.index.x].skeletonSpaceMatrix) * input.weight.x;
    skinned.position += mul(input.position, gMatrixPalette[input.index.y].skeletonSpaceMatrix) * input.weight.y;
    skinned.position += mul(input.position, gMatrixPalette[input.index.z].skeletonSpaceMatrix) * input.weight.z;
    skinned.position += mul(input.position, gMatrixPalette[input.index.w].skeletonSpaceMatrix) * input.weight.w;
    skinned.position.w = 1.0f;

    // 法線の変換
    skinned.normal = mul(input.normal, (float3x3) gMatrixPalette[input.index.x].skeletonSpaceInverseTransposeMatrix) * input.weight.x;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.y].skeletonSpaceInverseTransposeMatrix) * input.weight.y;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.z].skeletonSpaceInverseTransposeMatrix) * input.weight.z;
    skinned.normal += mul(input.normal, (float3x3) gMatrixPalette[input.index.w].skeletonSpaceInverseTransposeMatrix) * input.weight.w;
    skinned.normal = normalize(skinned.normal);

    return skinned;
}

VertexShaderOutput main(VertexShaderInput input)
{
    VertexShaderOutput output;

    // Skinning計算
    Skinned skinned = Skinning(input);

    // Skinning結果を使って通常の変換
    output.position = mul(skinned.position, transformationMatrix.WVP);
    output.worldPosition = mul(skinned.position, transformationMatrix.World).xyz;
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(skinned.normal, (float3x3) transformationMatrix.WorldInverseTranspose));

    return output;
}