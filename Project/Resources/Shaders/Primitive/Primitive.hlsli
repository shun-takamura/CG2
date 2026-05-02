// 頂点入力（InputLayoutと対応）
struct VertexInput
{
    float3 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

// 頂点出力（VS→PS）
struct VertexOutput
{
    float4 position : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 color : COLOR0;
};

// 変換行列（VSのb0）
struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
};

// マテリアル（PSのb0）
struct Material
{
    float4 color;
    int enableLighting;
    float alphaReference;
    float2 padding;
    float4x4 uvTransform;
};