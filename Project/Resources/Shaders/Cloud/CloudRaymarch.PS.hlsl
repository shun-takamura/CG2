// 雲海のレイマーチ本体。1/4 解像度の RT（RGBA16F）に、乗算済みの色と被覆率を書く。
// 2 枚目の RT（R32F）には雲の代表の距離を書く（CloudTemporal.PS が前のフレームの位置を求めるのに使う）。
// 合成は CloudComposite.PS で行う。式と取り決めは CloudRaymarch.hlsli

#include "CloudRaymarch.hlsli"
#include "../PostEffect/Common/PostProcess.hlsli"

// 画面座標の小さな揺らぎ（Interleaved Gradient Noise）。歩幅の縞を細かいざらつきに変える
float InterleavedGradientNoise(float2 pixel)
{
    return frac(52.9829189f * frac(dot(pixel, float2(0.06711056f, 0.00583715f))));
}

struct CloudOutput
{
    float4 color : SV_Target0;
    float depth : SV_Target1;
};

CloudOutput Output(float4 color)
{
    CloudOutput o;
    o.color = color;
    o.depth = sCloudDepth;
    return o;
}

// 画素とフレームから 0..1 の乱数（PCG）。時間方向に平均すると偏りなく消える（IGN ＋黄金比は模様が残って細い横線に見えた）
float HashJitter(uint2 pixel, uint frame)
{
    uint v = pixel.x * 1973u + pixel.y * 9277u + frame * 26699u;
    v = v * 747796405u + 2891336453u;
    v = ((v >> ((v >> 28u) + 4u)) ^ v) * 277803737u;
    v = (v >> 22u) ^ v;
    return float(v) * (1.0f / 4294967296.0f);
}

CloudOutput main(VertexShaderOutput input)
{
    sCloudDepth = gMaxDistance;
    if (gEnabled == 0 || gOpacity <= 0.0f)
    {
        return Output(float4(0.0f, 0.0f, 0.0f, 0.0f));
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
        return Output(float4(0.0f, 0.0f, 0.0f, 0.0f));
    }
    tExit = min(tExit, sceneDistance);
    if (tExit <= tEnter)
    {
        return Output(float4(0.0f, 0.0f, 0.0f, 0.0f));
    }

    // 雲海のボリュームは歩き始めを毎フレーム揺らす（黄金比ずつずらす）。CloudTemporal.PS で平均すると歩幅の縞が消える。
    // 従来の面は TAA が無い間は揺らさない（gJitter = 0 で各歩の真ん中）
    const float ign = InterleavedGradientNoise(input.position.xy);
    const float jitter = (gVolumeSea != 0) ? HashJitter(uint2(input.position.xy), uint(gFrameIndex)) : lerp(0.5f, ign, gJitter);

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

    return Output(result * gOpacity);
}
