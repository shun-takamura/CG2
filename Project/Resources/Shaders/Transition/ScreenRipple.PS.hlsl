// 画面を上から見下ろした水面に見立て、波紋（ScreenWaterSurface の画面の波）の傾きで取り込んだシーンを揺らす。
// 羽根の飛翔で、羽根が飛び立った所の画面を揺らすのに使う。羽根はこの上に描く。
// 隙間埋め：羽根で覆い切れない隙間から後ろが見えないよう、白っぽいふわふわした層（ノイズで濃淡）を重ねる。
// 羽根の後ろに描くので見えるのは隙間だけ（遠くのぼけた羽根のように見える）。

#include "../PostEffect/Common/PostProcess.hlsli"
#include "ScreenWater.hlsli"
#include "InkShape.hlsli"   // InkNoise（cloud_noise.dds、t2 / s1）

cbuffer ScreenRippleParams : register(b0)
{
    ScreenWaterParams gWater;   // ScreenWaterSurface::ConstantsForGPU（32 バイト）
    float gFill;                // 隙間埋めの濃さ 0..1
    float gFillNoiseScale;      // 濃淡の細かさ
    float gTime;                // 濃淡を流す
    float gPadding0;
};

float4 main(VertexShaderOutput input) : SV_TARGET
{
    const float2 uv = input.texcoord;
    const float2 slope = RippleSlope(gWater, uv);
    const float4 scene = gScene.SampleLevel(gLinearClampSampler, RefractUV(gWater, uv, slope), 0);
    float3 color = ApplyRippleHighlight(gWater, scene.rgb, slope);

    if (gFill > 0.0f)
    {
        // 羽根の白に近い明るさで、ふわふわした濃淡（左上へ流す＝風の向き）
        const float2 p = ToWidthSpace(gWater, uv) * gFillNoiseScale + float2(gTime * 0.3f, -gTime * 0.2f);
        const float fluff = InkNoise(p).r * 0.6f + InkNoise(p * 2.7f + 0.5f).r * 0.4f;
        const float3 fillColor = lerp(0.82f, 0.97f, fluff).xxx;
        color = lerp(color, fillColor, saturate(gFill));
    }
    return float4(color, scene.a);
}
