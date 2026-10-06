// RippleSimulation.CS.hlsl
// 水面の波の GPU シミュレーション（2次元の波動方程式。RippleSimulation.cpp）。
// テクスチャは R=今の高さ / G=1つ前の高さ。1刻みで
//   next = (2·h - h_prev + k·(左右上下の和 - 4h)) · damping
// を計算し、(next, h) を書く。範囲の縁は edgeFade の幅で減衰させて波を吸収する（反射して戻らないように）。

#define MAX_IMPULSES 32 // RippleSimulation::kMaxImpulsesPerFrame と合わせる

struct Impulse
{
    float2 uv;       // 範囲内の UV
    float radius;    // UV 単位
    float strength;  // 正でへこむ
};

cbuffer SimParams : register(b0)
{
    float gWaveFactor;   // k = (c·Δt/Δx)²
    float gDamping;
    float gEdgeFade;
    uint gImpulseCount;
    uint gResolution;
    uint gClear;
    float2 gPadding;
    Impulse gImpulses[MAX_IMPULSES];
};

Texture2D<float2> gSource : register(t0);
RWTexture2D<float2> gDestination : register(u0);

float HeightAt(int2 p)
{
    // 範囲の外は 0（縁の外を平らな水として扱う）
    if (any(p < 0) || any(p >= int2(gResolution, gResolution)))
    {
        return 0.0f;
    }
    return gSource.Load(int3(p, 0)).r;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= gResolution || id.y >= gResolution)
    {
        return;
    }
    if (gClear != 0)
    {
        gDestination[id.xy] = float2(0.0f, 0.0f);
        return;
    }

    const int2 p = int2(id.xy);
    const float2 state = gSource.Load(int3(p, 0));
    const float h = state.r;
    const float hPrev = state.g;
    const float laplacian = HeightAt(p + int2(1, 0)) + HeightAt(p - int2(1, 0))
                          + HeightAt(p + int2(0, 1)) + HeightAt(p - int2(0, 1)) - 4.0f * h;
    float next = (2.0f * h - hPrev + gWaveFactor * laplacian) * gDamping;

    // 波源：ガウス形にへこませる
    const float2 uv = (float2(p) + 0.5f) / float(gResolution);
    for (uint i = 0; i < gImpulseCount; ++i)
    {
        const Impulse impulse = gImpulses[i];
        const float2 d = (uv - impulse.uv) / max(impulse.radius, 1e-5f);
        next -= impulse.strength * exp(-dot(d, d));
    }

    // 縁で吸収（範囲の端に近いほど強く減衰）
    const float2 edge = min(uv, 1.0f - uv);
    const float fade = smoothstep(0.0f, gEdgeFade, min(edge.x, edge.y));
    next *= fade;

    gDestination[id.xy] = float2(next, h * fade);
}
