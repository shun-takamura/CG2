// LiquidGlassSphere.hlsli
// 液体の入ったガラスの球（LiquidGlassSphere.cpp）。VS/PS で共有する定義。
// 液体はメッシュを持たず、PS で視線と球・液面（平面）の交わりを解いて描く。

struct GlassVertexOutput
{
    float4 position : SV_POSITION;
    float3 worldPosition : POSITION0;
};
