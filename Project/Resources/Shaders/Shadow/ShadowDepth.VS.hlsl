// CSM シャドウパス用の深度書き込み専用 VS。
// ワールド変換したのち、カスケードのライト ViewProj で投影して深度のみ出力する（PS なし）。

struct TransformationMatrix
{
    float4x4 WVP;                  // 通常描画用（ここでは未使用）
    float4x4 World;
    float4x4 WorldInverseTranspose; // 未使用
};

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix gTransform;
};

// カスケードのライト ViewProj（C++ 側からルート定数で供給）
cbuffer CascadeBuffer : register(b1)
{
    float4x4 gCascadeViewProj;
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

float4 main(VertexShaderInput input) : SV_Position
{
    float4 worldPosition = mul(input.position, gTransform.World);
    return mul(worldPosition, gCascadeViewProj);
}
