// 墨のトランジション（InkTransition）。取り込んだシーンを上から見下ろした水面に見立て、
//   着水前：上から落ちてくる墨の雫（近い＝大きくぼけた所から小さく締まる）と水面に落ちる影
//   着水後：波紋の傾きで画面をずらし（屈折）、雫の点から墨が広がる（形と濃淡は InkShape.hlsli）
// 長さは「画面の幅 = 1」で測る。

#include "../PostEffect/Common/PostProcess.hlsli"
#include "ScreenWater.hlsli"
#include "InkShape.hlsli"

cbuffer InkParams : register(b0)
{
    ScreenWaterParams gWater; // ScreenWaterSurface::ConstantsForGPU（32 バイト）
    float2 gDropCenter;       // 雫が落ちる点（画面 UV）
    float gInkRadius;         // 墨の半径（画面の幅に対する割合）。負なら着水前
    float gEdgeSoftness;      // 縁のぼかし幅
    float gCoverage;          // 覆う進み 0→1。1 なら全面黒
    float gFade;              // 黒の濃さ（開く側で 1→0）
    float gTime;              // 遷移の経過秒
    float gDropPhase;         // 着水前の雫 0→1（着水後は負）
    float gDropStartRadius;
    float gDropEndRadius;
    float gWarpAmount;
    float gWarpScale;
    float gTendrilAmount;
    float gTendrilFreq;
    float gDetailScale;
    float gWispAmount;
    float gEdgeWidth;
    float gFlowSpeed;
    float gShadowStrength;
    float gPadding0;
    float3 gThinColor;        // 薄い所の色
    float gPadding1;
    float gToneScale;
    float gToneMin;
    float gToneDepth;
    float gToneDarkStart;     // 覆う進みがここを過ぎたら全体が黒に寄り始める（0.9 で真っ黒）
};

float4 main(VertexShaderOutput input) : SV_TARGET
{
    if (gCoverage >= 1.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float2 uv = input.texcoord;

    // 屈折：墨も水の中にあるので、画面と同じだけずらす
    const float2 slope = RippleSlope(gWater, uv);
    const float2 refractedUV = RefractUV(gWater, uv, slope);
    const float4 scene = gScene.SampleLevel(gLinearClampSampler, refractedUV, 0);

    const float2 world = ToWidthSpace(gWater, refractedUV);
    const float2 fromDrop = world - ToWidthSpace(gWater, gDropCenter);
    const float flow = gTime * gFlowSpeed;

    InkShapeParams shapeParams;
    shapeParams.radius = gInkRadius;
    shapeParams.edgeSoftness = gEdgeSoftness;
    shapeParams.warpAmount = gWarpAmount;
    shapeParams.warpScale = gWarpScale;
    shapeParams.tendrilAmount = gTendrilAmount;
    shapeParams.tendrilFreq = gTendrilFreq;
    shapeParams.detailScale = gDetailScale;
    shapeParams.wispAmount = gWispAmount;
    shapeParams.edgeWidth = gEdgeWidth;
    shapeParams.flow = flow;

    InkToneParams toneParams;
    toneParams.scale = gToneScale;
    toneParams.minTone = gToneMin;
    toneParams.depthDarken = gToneDepth;
    toneParams.timeDarken = smoothstep(gToneDarkStart, 0.9f, gCoverage);

    const float2 shape = InkShape(shapeParams, fromDrop, world);
    float ink = shape.x * InkTone(toneParams, world, shape.y, flow);
    // 最後は必ず全面黒へ（筋の隙間や角が残らないように）
    ink = lerp(ink, 1.0f, smoothstep(0.85f, 1.0f, gCoverage));
    ink *= gFade;

    // 薄い所は青みのある墨色、濃い所は黒
    const float3 inkColor = lerp(gThinColor, 0.0f.xxx, saturate(ink * 1.5f - 0.5f));
    float3 color = lerp(scene.rgb, inkColor, saturate(ink * 1.4f));

    // 着水前：水面に落ちる雫の影（光と反対側から、着水点へ寄りながら締まる）
    const float2 toDrop = ToWidthSpace(gWater, uv) - ToWidthSpace(gWater, gDropCenter);
    float dropAlpha = 0.0f;
    float dropRadius = 0.0f;
    float dropBlur = 0.0f;
    if (gDropPhase >= 0.0f)
    {
        const float fall = gDropPhase * gDropPhase; // 落ちるほど速く近づく
        const float2 shadowCenter = -gWater.lightDirection * (1.0f - fall) * 0.06f;
        const float shadowRadius = gDropEndRadius * 1.5f + (1.0f - fall) * 0.04f;
        const float shadowSoft = 0.01f + (1.0f - fall) * 0.05f;
        const float shadow = (1.0f - smoothstep(shadowRadius - shadowSoft, shadowRadius + shadowSoft, length(toDrop - shadowCenter)))
                           * gShadowStrength * smoothstep(0.0f, 0.4f, gDropPhase);
        color *= 1.0f - shadow;

        dropRadius = lerp(gDropStartRadius, gDropEndRadius, fall);
        dropBlur = lerp(gDropStartRadius * 0.6f, 0.002f, fall);
        dropAlpha = (1.0f - smoothstep(dropRadius - dropBlur, dropRadius + dropBlur, length(toDrop)))
                  * smoothstep(0.0f, 0.25f, gDropPhase);
    }

    color = ApplyRippleHighlight(gWater, color, slope);

    // 雫は水面より上（空中）にあるので屈折も明暗も受けない。光の来る側に小さな照り
    if (dropAlpha > 0.0f)
    {
        const float2 glintCenter = gWater.lightDirection * dropRadius * 0.45f;
        const float glint = (1.0f - smoothstep(0.0f, dropRadius * 0.3f + dropBlur, length(toDrop - glintCenter))) * 0.8f;
        color = lerp(color, glint.xxx, dropAlpha);
    }

    return float4(color, scene.a);
}
