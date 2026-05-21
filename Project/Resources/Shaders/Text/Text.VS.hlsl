// 1 グリフ = 1 インスタンス。4 頂点トライアングルストリップで quad を描く。

struct GlyphInstance
{
    float2 screenPos;     // 左上ピクセル座標
    float2 size;          // ピクセル単位の幅高
    float4 uvRect;        // (u0, v0, u1, v1)
    float4 color;         // 本体色 RGBA
    float4 outlineColor;  // アウトライン色 RGBA（.a で有効/無効も判定）
    float  outlineWidth;  // アウトライン太さ [screen pixel]、0 で無効
    float3 _pad;
};

StructuredBuffer<GlyphInstance> gInstances : register(t0);

cbuffer ScreenInfo : register(b0)
{
    float2 gScreenSize; // ピクセル
    float2 gPad;
};

struct VSOutput
{
    float4 position     : SV_POSITION;
    float2 uv           : TEXCOORD0;
    float4 color        : COLOR0;
    float4 outlineColor : COLOR1;
    float  outlineWidth : TEXCOORD1;
};

static const float2 kCorners[4] = {
    float2(0.0, 0.0),
    float2(1.0, 0.0),
    float2(0.0, 1.0),
    float2(1.0, 1.0),
};

VSOutput main(uint vid : SV_VertexID, uint iid : SV_InstanceID)
{
    GlyphInstance inst = gInstances[iid];
    float2 corner = kCorners[vid];

    float2 px = inst.screenPos + corner * inst.size;
    float2 ndc;
    ndc.x = (px.x / gScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (px.y / gScreenSize.y) * 2.0;

    VSOutput o;
    o.position = float4(ndc, 0.0, 1.0);
    o.uv = lerp(inst.uvRect.xy, inst.uvRect.zw, corner);
    o.color = inst.color;
    o.outlineColor = inst.outlineColor;
    o.outlineWidth = inst.outlineWidth;
    return o;
}
