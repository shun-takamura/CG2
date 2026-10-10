// 雲海のレイマーチ本体。1/4 解像度の RT（RGBA16F）に、乗算済みの色と被覆率を書く。
// 合成は CloudComposite.PS で行う。式と取り決めは CloudRaymarch.hlsli

#include "CloudRaymarch.hlsli"
#include "../PostEffect/Common/PostProcess.hlsli"

// 画面座標の小さな揺らぎ（Interleaved Gradient Noise）。歩幅の縞を細かいざらつきに変える
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    if (gEnabled == 0 || gOpacity <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    const float2 uv = input.texcoord;
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);

    // 視線（遠い面の点から向きを作る）と、不透明物までの距離
    float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), gInvViewProj);
    farPoint.xyz /= farPoint.w;
    const float3 dir = normalize(farPoint.xyz - gEye);

    const float depth = gDepth.SampleLevel(gPointClampSampler, uv, 0);
    float sceneDistance = gMaxDistance;
    if (depth < 1.0f)
    {
        float4 scenePoint = mul(float4(ndc, depth, 1.0f), gInvViewProj);
        scenePoint.xyz /= scenePoint.w;
        sceneDistance = min(length(scenePoint.xyz - gEye), gMaxDistance);
    }

    float tEnter, tExit;
    if (!IntersectSlab(gEye, dir, tEnter, tExit))
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    tExit = min(tExit, sceneDistance);
    if (tExit <= tEnter)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // TAA が無い間は揺らさない（gJitter = 0 で各歩の真ん中）。揺らすと歩幅の縞が細かいざらつきに変わる
    const float jitter = lerp(0.5f, InterleavedGradientNoise(input.position.xy), gJitter);

    // 手前 gVolumeRange × 重み を B'（雲の中のもわもわ）、その先をゼノブレ式で描いて前後に重ねる
    float4 result = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float tSurface = tEnter;
    [branch]
    if (gVolumeWeight > 0.0f)
    {
        const float tVolumeEnd = min(tEnter + gVolumeRange * gVolumeWeight, tExit);
        result = MarchVolume(gEye, dir, tEnter, tVolumeEnd, jitter, tSurface);
    }
    [branch]
    if (result.a < 0.99f)
    {
        const float4 surface = MarchSurface(gEye, dir, tSurface, tExit, jitter);
        result.rgb += (1.0f - result.a) * surface.rgb * surface.a;
        result.a += (1.0f - result.a) * surface.a;
    }

    return result * gOpacity;
}
