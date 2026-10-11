// 羽根の描画（FeatherRenderer）の共通定義。
// 羽根は細かく分割した板（UV の格子）を頂点シェーダで曲げて作る：
//   羽軸（縦）に沿って弓なりに反らせ（bend）、横にも少し丸める（cup）。法線も同じ式から求める。
// インスタンスは StructuredBuffer（FeatherRenderer::InstanceForGPU と 1:1、96 バイト）。

#ifndef FEATHER_HLSLI
#define FEATHER_HLSLI

struct FeatherInstance
{
    float4x4 world;   // 大きさ・回転・位置
    float4 shape;     // x = 反り / y = 丸み / z = 不透明度 / w = ディゾルブ（0 = なし、1 = 消えた）
    float4 extra;     // x = テクスチャ番号 / y = 明るさ / z = ディゾルブのノイズのずらし / w = 未使用
};

StructuredBuffer<FeatherInstance> gInstances : register(t0);

cbuffer FeatherCamera : register(b0)
{
    float4x4 gViewProjection;
    float3 gCameraPosition;
    float gTime;
    float3 gLightDirection;   // 光の進む向き（正規化済み）
    float gAmbient;           // 光の当たらない面の明るさ
    float3 gEdgeColor;        // ディゾルブの縁の色
    float gEdgeWidth;         // ディゾルブの縁の幅（ノイズの値の幅）
    float gTranslucency;      // 裏から光が透ける強さ
    float gNoiseScale;        // ディゾルブのノイズの細かさ
    float2 gPadding;
};

struct FeatherVSOutput
{
    float4 position : SV_POSITION;
    float2 uv : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION0;
    nointerpolation uint instance : INSTANCE0;
};

#endif // FEATHER_HLSLI
