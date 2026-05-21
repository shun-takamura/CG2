// 1 グリフ = 1 インスタンス。4 頂点トライアングルストリップで quad を描く。

struct GlyphInstance
{
    float2 screenPos;  // 左上ピクセル座標
    float2 size;       // ピクセル単位の幅高
    float4 uvRect;     // (u0, v0, u1, v1)
    float4 color;
};

StructuredBuffer<GlyphInstance> gInstances : register(t0);

cbuffer ScreenInfo : register(b0)
{
    float2 gScreenSize; // ピクセル
    float2 gPad;
};

struct VSOutput
{
    float4 position : SV_POSITION;
    float2 uv       : TEXCOORD0;
    float4 color    : COLOR0;
};

// quad 4 頂点（trianglestrip）: vid 0=左上 1=右上 2=左下 3=右下
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

    // ピクセル座標
    float2 px = inst.screenPos + corner * inst.size;

    // NDC 変換（Y は下向きが正、NDC は上が +Y なので反転）
    float2 ndc;
    ndc.x = (px.x / gScreenSize.x) * 2.0 - 1.0;
    ndc.y = 1.0 - (px.y / gScreenSize.y) * 2.0;

    VSOutput o;
    o.position = float4(ndc, 0.0, 1.0);
    // UV は uvRect 内の対応する角
    o.uv = lerp(inst.uvRect.xy, inst.uvRect.zw, corner);
    o.color = inst.color;
    return o;
}
