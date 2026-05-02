// PostProcess.VS.hlsl
// ポストプロセス用の頂点シェーダー
// 頂点バッファを使わず、SV_VertexIDから全画面の三角形を生成

#include "PostProcess.hlsli"

static const uint kNumVertex = 3;

// 全画面を覆う大きな三角形の頂点座標
static const float4 kPositions[kNumVertex] =
{
    { -1.0f,  1.0f, 0.0f, 1.0f }, // 左上
    {  3.0f,  1.0f, 0.0f, 1.0f }, // 右上（画面外）
    { -1.0f, -3.0f, 0.0f, 1.0f }  // 左下（画面外）
};

// テクスチャ座標
static const float2 kTexcoords[kNumVertex] =
{
    { 0.0f, 0.0f }, // 左上
    { 2.0f, 0.0f }, // 右上
    { 0.0f, 2.0f }  // 左下
};

VertexShaderOutput main(uint vertexId : SV_VertexID)
{
    VertexShaderOutput output;
    output.position = kPositions[vertexId];
    output.texcoord = kTexcoords[vertexId];
    return output;
}
