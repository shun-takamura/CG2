#ifndef CLOUD_RAYMARCH_HLSLI
#define CLOUD_RAYMARCH_HLSLI

// 雲海のレイマーチ（9_CloudRendering.md フェーズ3）。C++ は CloudRaymarcher。
// 見た目は tools/Python で CPU 版（同じ式）を描いて詰めた。式を変えたら CPU 版と見比べる。
//
// 雲の形は 1 本の「濃さ」の式で決め、2 つの描き方で共有する（境目で形がずれない）：
//   ゼノブレ式（一帯・円柱の壁）… 当たったら止める。軽い。表面だけ
//   B'（入口・出口の近く）     … 手前 gVolumeRange [m] だけ濃さを積み上げ、3D ノイズで縁を崩す。その先はゼノブレ式
//
// ゼノブレ2式（CEDEC 2018）のまま：
//   雲海の濃さ = clamp((最高点 gTop − y) / 雲海の深さ gSeaDepth, 0, 1)        … 上 0 → 下 1
//   透明度     = clamp((ノイズ × 雲海の濃さ − 補正値) × 乗算値, 0, 1)         … 0 を超えた所が雲
//   ノイズ = 大きい形（Perlin＋Worley）ともこもこ（ドーム）の混ぜ。gNoiseFloor..1 に寄せてから掛けるので、
//   帯の底（濃さ 1）では必ず補正値を超え、帯より下は詰まった雲になる（隙間から未ロードの地上が見えない）。
//   降下の縦穴（ゼノブレ2の資料「雲海に穴を空けて」「雲海を縦にも配置可能に」と同じ考え方）：
//     穴の外側の雲を、雲海と同じ式（ノイズ × 濃さ − 補正値）で 3 方向に作る。壁＝中心線から外へ離れるほど濃い横向きの雲海、
//     天井＝その場所の上面から gCapTop 下で上ほど濃い下向きの雲海、床＝下面から gCapBottom 上で下ほど濃い上向きの雲海。
//     穴の中のノイズは gShaftNoiseTiling 倍に細かくする（穴の半径に対して雲海のノイズは大きすぎて、なめらかな筒になった）。
//     天井はその場所の上面から測るので、上から穴は見えない（最高点から測ったら、上面が低い所で穴が開いて見えた）。
//     天井と床は B' で「雲の中」として抜ける（入る・出るときのもわもわ）
//
// 陰影：上面は「ノイズ × 濃さ = 補正値」の高さ場（H = 最高点 − 深さ × 補正値 / ノイズ）なので、ノイズの勾配から法線が取れる。
//   法線の当たり（ラップ付き）× 太陽側 1 タップの遮り（ゼノブレ式）× 谷の暗さ（こぶの高さの範囲の中で低いほど暗い）
//   縦穴の壁は中心線へ向く法線で、深いほど暗い（空が見えない）。下面は下向きの法線で、照り返しだけの暗さ
//   （上を向いた面として照らすと、壁も底も真上の太陽で白一色になり、降下中の画面が真っ白になった）
//
// レジスタ：b0 = このファイルの CB / t0 = 深度 / t1 = 雲のノイズ（R=形, G=マスク）/ t2 = 空の cubemap / t3 = もこもこの高さ
//           s0 = ラップ・線形 / s1 = クランプ・点 / s2 = クランプ・線形

static const int kShaftPointMax = 16;
// 勾配ノイズ 2 オクターブの値は 0.41〜0.59（10〜90%）に固まるので、0..1 に広げ直す倍率（CPU 版で分布を測った）
static const float kErosionContrast = 4.6f;

// C++ の CloudRaymarcher::ConstantsForGPU と 1:1（16 バイト単位で並べてある）
cbuffer CloudRaymarchParams : register(b0)
{
    float4x4 gInvViewProj;
    float3 gEye;            int    gEnabled;
    float3 gSunDirection;   float  gOpacity;        // 太陽へ向かう向き
    float  gTop;            float  gBottom;         float  gSeaDepth;       float  gBottomFade;
    float2 gShapeOffset;    float2 gMokoOffset;
    float2 gMaskOffset;     float  gShapeTiling;    float  gMokoTiling;
    float  gMaskTiling;     float  gMokoWeight;     float  gNormalEps;      float  gMaskStrength;
    float  gCoverage;       float  gSharpness;      float  gLodScale;       float  gTime;
    int    gSteps;          float  gFirstStep;      float  gMaxDistance;    float  gFloorDepth;     // 床のこぶの盛り上がりの幅
    float  gLightStep;      float  gDensity;        float  gFadeStart;      float  gFadeEnd;
    float  gHazeStart;      float  gHazeEnd;        float  gHazeMax;        float  gRimIntensity;
    float3 gLitColor;       float  gRimG;
    float3 gShadowColor;    float  gAmbient;        // 太陽が当たらない所の明るさ
    float3 gRimColor;       float  gSunGlow;
    int    gShaftCount;     float  gShaftRadius;    float  gShaftWallDepth; float  gCapDepth;       // 壁 / 天井・床の雲海の深さ
    float3 gShaftMin;       float  gShaftNoiseTiling;                                           // 穴の中のノイズを細かくする倍率
    float3 gShaftMax;       float  gVolumeWeight;   // 0 = ゼノブレ式だけ / 1 = 手前を B'（CPU が入口・出口からの距離で決める）
    int    gVolumeSteps;    float  gVolumeRange;    float  gExtinction;     float  gErosion;
    float  gErosionScale;   float  gPowder;         float  gAmbientStrength; float gVolumeLightStep;
    float3 gErosionWind;    float  gNoiseFloor;     // ノイズを gNoiseFloor..1 に寄せる（帯の底で必ず雲になる）
    float2 gInvTargetSize;  float  gJitter;         float  gWrap;           // gJitter: 歩き始めの揺らし（TAA が無い間は 0）
    float  gValley;         float  gValleyPow;      float  gTraceSlope;     float  gTraceEpsilon; // 谷の暗さ / 高さ場のトレース
    float  gShaftWallBlend; float  gShaftDarkness;  float  gUndersideBright; float gPadding4; // 縦穴の陰影・底面の明るさ
    float  gCapTop;         float  gCapBottom;      float  gErosionSharp;   float  gLightExtinction; // 縦穴のふた / B' の塊のくっきり度・光側の消散の倍率
    float4 gShaftPoints[kShaftPointMax];            // xyz = 降下の線の点（w は未使用）
};

Texture2D<float> gDepth : register(t0);
Texture2D<float2> gNoise : register(t1);
TextureCube<float4> gSkyCube : register(t2);
Texture2D<float> gMoko : register(t3);
SamplerState gWrapSampler : register(s0);
SamplerState gPointClampSampler : register(s1);
SamplerState gLinearClampSampler : register(s2);

// ---------------------------------------------------------------------------
// ノイズ
// ---------------------------------------------------------------------------

// 距離からミップを選ぶ（ループの中なので微分は使えない）。tiling は [回/m]
float NoiseLod(float distance, float tiling)
{
    return log2(max(distance * gLodScale * tiling, 1.0f));
}

// 0..1。大きい形ともこもこを混ぜてから gNoiseFloor..1 に寄せる
float CloudNoise(float2 xz, float distance)
{
    const float large = gNoise.SampleLevel(gWrapSampler, xz * gShapeTiling + gShapeOffset, NoiseLod(distance, gShapeTiling)).r;
    const float moko = gMoko.SampleLevel(gWrapSampler, xz * gMokoTiling + gMokoOffset, NoiseLod(distance, gMokoTiling));
    return lerp(gNoiseFloor, 1.0f, lerp(large, moko, gMokoWeight));
}

// 場所ごとの補正値（マスクで大きな塊の所 / 薄い所を作る）
float CloudCoverage(float2 xz, float distance)
{
    const float mask = gNoise.SampleLevel(gWrapSampler, xz * gMaskTiling + gMaskOffset, NoiseLod(distance, gMaskTiling)).g;
    return gCoverage - (mask - 0.5f) * gMaskStrength;
}

// 3D の勾配ノイズ（B' の縁の侵食用）。テクスチャを増やさないよう計算で作る
float3 Hash33(float3 p)
{
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx) * 2.0f - 1.0f;
}

float GradientNoise3D(float3 p)
{
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    float n = 0.0f;
    [unroll] for (int z = 0; z < 2; ++z)
    [unroll] for (int y = 0; y < 2; ++y)
    [unroll] for (int x = 0; x < 2; ++x)
    {
        const float3 o = float3(x, y, z);
        const float w = (x ? u.x : 1.0f - u.x) * (y ? u.y : 1.0f - u.y) * (z ? u.z : 1.0f - u.z);
        n += w * dot(Hash33(i + o), f - o);
    }
    return n * 0.5f + 0.5f; // ≒ 0..1
}

// ---------------------------------------------------------------------------
// 濃さ
// ---------------------------------------------------------------------------

// 降下の線（折れ線）までの距離と最近点
float ShaftClosest(float3 p, out float3 closest)
{
    float best = 1e9f;
    closest = p;
    [loop] for (int i = 0; i + 1 < gShaftCount; ++i)
    {
        const float3 a = gShaftPoints[i].xyz;
        const float3 ab = gShaftPoints[i + 1].xyz - a;
        const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-3f));
        const float3 c = a + ab * t;
        const float d = length(p - c);
        if (d < best)
        {
            best = d;
            closest = c;
        }
    }
    return best;
}

float ShaftDistance(float3 p)
{
    float3 closest;
    return ShaftClosest(p, closest);
}

// 上面の高さ H = 最高点 − 深さ × 補正値 / ノイズ（ノイズ × 濃さ = 補正値 の所。穴は含まない）
float SurfaceHeight(float2 xz, float distance)
{
    const float noise = max(CloudNoise(xz, distance), 1e-3f);
    return gTop - min(gSeaDepth * CloudCoverage(xz, distance) / noise, gSeaDepth);
}

// 縦穴のまわり（穴の雲の式を評価する範囲）か。closest / d は降下の線の最近点と距離
bool InHoleRegion(float3 p, out float3 closest, out float d)
{
    closest = p;
    d = 1e9f;
    if (gShaftCount < 2 || any(p < gShaftMin) || any(p > gShaftMax))
    {
        return false;
    }
    d = ShaftClosest(p, closest);
    return d < gShaftRadius + gShaftWallDepth * 1.5f;
}

// 穴の外側の雲（> 0 で雲）。壁・天井・床をそれぞれ雲海の式で作り、どれかが雲なら雲
float HoleOver(float3 p, float distance, float3 closest, float d)
{
    const float k = gShaftNoiseTiling;
    const float kd = distance * k;
    // 壁：ノイズは壁に沿った座標で引く。ZY 面と XY 面を壁の向きで混ぜる（角度で巻くと継ぎ目が出る）
    const float2 outward = abs((p - closest).xz);
    const float2 blend = outward / max(outward.x + outward.y, 1e-3f);
    const float wallNoise = CloudNoise(float2(p.z, p.y) * k, kd) * blend.x + CloudNoise(float2(p.x, p.y) * k, kd) * blend.y;
    const float wallOver = (wallNoise * saturate((d - gShaftRadius) / gShaftWallDepth) - gCoverage) * gSharpness;
    // 天井：その場所の上面から gCapTop 下。上ほど濃い（ノイズは場所をずらして上面と別の模様に）
    const float ceiling = SurfaceHeight(p.xz, distance) - gCapTop;
    const float ceilNoise = CloudNoise(p.xz * k + float2(777.0f, 333.0f), kd);
    const float ceilOver = (ceilNoise * saturate((p.y - ceiling) / gCapDepth) - gCoverage) * gSharpness;
    // 床：下面から gCapBottom 上より下は詰まった雲、こぶはその上へ gFloorDepth で盛り上がる。床は抜ける時に白くなる区間なので天井より薄くする。
    // （床の高さから下へ濃くする形だと、ふたが gCapDepth より薄い時に濃さが足りず、床が隙間だらけで地形が透けた）
    const float floorY = gBottom + gCapBottom;
    const float floorNoise = CloudNoise(p.xz * k + float2(-555.0f, 999.0f), kd);
    const float floorOver = (floorNoise * saturate((floorY + gFloorDepth - p.y) / gFloorDepth) - gCoverage) * gSharpness;
    return max(wallOver, max(ceilOver, floorOver));
}

// over > 0 で雲（saturate したものが透明度）。distance はミップ選択用
float CloudOver(float3 p, float distance)
{
    const float noise = CloudNoise(p.xz, distance);
    const float coverage = CloudCoverage(p.xz, distance);
    // 雲海の濃さ（上 0 → 下 1）。下面は薄く丸める（下から見上げた時の雲の底）
    const float seaDensity = saturate((gTop - p.y) / gSeaDepth) * saturate((p.y - gBottom) / gBottomFade);
    float over = (noise * seaDensity - coverage) * gSharpness;
    // 穴：雲海の中で、穴の外側の雲（壁・天井・床）でも無い所
    float3 closest;
    float d;
    if (InHoleRegion(p, closest, d))
    {
        over = min(over, HoleOver(p, distance, closest, d));
    }
    return over;
}

// ---------------------------------------------------------------------------
// 共通
// ---------------------------------------------------------------------------

float PhaseHG(float cosTheta, float g)
{
    const float g2 = g * g;
    return (1.0f - g2) / pow(max(1.0f + g2 - 2.0f * g * cosTheta, 1e-4f), 1.5f);
}

// 雲の層 [gBottom, gTop] とレイの交わる区間。無ければ false
bool IntersectSlab(float3 origin, float3 dir, out float tEnter, out float tExit)
{
    const float dy = (abs(dir.y) < 1e-5f) ? 1e-5f : dir.y;
    const float tA = (gTop - origin.y) / dy;
    const float tB = (gBottom - origin.y) / dy;
    tEnter = max(min(tA, tB), 0.0f);
    tExit = max(tA, tB);
    if (origin.y <= gTop && origin.y >= gBottom)
    {
        tEnter = 0.0f;
    }
    return tExit > tEnter;
}

// 水平線の空の色。雲海を見るレイは下向きなので、そのまま引くと cubemap の下半分（青）を拾って雲が水色に染まる
float3 HorizonSky(float3 dir)
{
    return gSkyCube.SampleLevel(gLinearClampSampler, normalize(float3(dir.x, max(dir.y, 0.02f), dir.z)), 0).rgb;
}

// 空気遠近と遠くのフェード。color は線形、alpha は被覆率。
// 遠くは透明にせず水平線の空の色へ溶かす（透明にすると後ろの cubemap の下半分の青が水平線のすぐ下に帯で見えた）
float4 ApplyDistance(float3 color, float alpha, float3 dir, float distance)
{
    const float3 sky = HorizonSky(dir);
    const float haze = saturate((distance - gHazeStart) / max(gHazeEnd - gHazeStart, 1.0f)) * gHazeMax;
    const float fade = saturate((distance - gFadeStart) / max(gFadeEnd - gFadeStart, 1.0f));
    color = lerp(color, sky, max(haze, fade));
    return float4(color, alpha);
}

// 雲の面の向きと暗さ。normal は外向き、ao は空の見えにくさによる暗さ、wall は縦穴の壁らしさ（0..1）
struct SurfaceFrame
{
    float3 normal;
    float ao;
    float wall;
};

SurfaceFrame GetSurfaceFrame(float3 p, float distance)
{
    SurfaceFrame f;

    // 上面：高さ場 H = 最高点 − 深さ × 補正値 / ノイズ の勾配から法線。差分の幅は遠いほど広げる（細かすぎる凹凸でちらつかないように）
    const float eps = max(gNormalEps, distance * gLodScale / 256.0f);
    const float n0 = max(CloudNoise(p.xz, distance), 1e-3f);
    const float nx = CloudNoise(p.xz + float2(eps, 0.0f), distance);
    const float nz = CloudNoise(p.xz + float2(0.0f, eps), distance);
    const float coverage = CloudCoverage(p.xz, distance);
    const float k = gSeaDepth * coverage / (n0 * n0);
    float3 normal = normalize(float3(-k * (nx - n0) / eps, 1.0f, -k * (nz - n0) / eps));
    // 谷：こぶの高さの範囲（ノイズ 1 〜 gNoiseFloor の面の高さ）の中で低いほど暗い
    const float depthMin = gSeaDepth * coverage;
    const float depthMax = gSeaDepth * coverage / max(gNoiseFloor, 1e-3f);
    const float valley = saturate((gTop - p.y - depthMin) / max(depthMax - depthMin, 1.0f));
    float ao = 1.0f - gValley * pow(valley, gValleyPow);

    // 縦穴のまわり：濃さの式の傾き（中心差分）から法線。深いほど空が見えず暗い
    f.wall = 0.0f;
    float3 closest;
    float d;
    if (InHoleRegion(p, closest, d))
    {
        // 天井より下だけ（上面まで穴の陰影にすると、穴の真上だけ上面の色が周りと違って見えた）
        const float belowCeiling = saturate((SurfaceHeight(p.xz, distance) - gCapTop * 0.5f - p.y) / max(gCapTop * 0.5f, 1.0f));
        f.wall = saturate((gShaftRadius + gShaftWallDepth * 1.5f - d) / max(gShaftWallBlend, 1e-3f)) * belowCeiling;
        const float h = gNormalEps * 0.5f;
        const float3 grad = float3(
            CloudOver(p + float3(h, 0.0f, 0.0f), distance) - CloudOver(p - float3(h, 0.0f, 0.0f), distance),
            CloudOver(p + float3(0.0f, h, 0.0f), distance) - CloudOver(p - float3(0.0f, h, 0.0f), distance),
            CloudOver(p + float3(0.0f, 0.0f, h), distance) - CloudOver(p - float3(0.0f, 0.0f, h), distance));
        normal = lerp(normal, -grad / max(length(grad), 1e-6f), f.wall);
        // 穴の中の深さ（天井 → 床で 0 → 1）。深いほど空が見えず暗い。床が太陽で白く浮かないように
        const float holeDepth = saturate((SurfaceHeight(p.xz, distance) - gCapTop - p.y) / max(SurfaceHeight(p.xz, distance) - gCapTop - gBottom - gCapBottom, 1.0f));
        ao = lerp(ao, 1.0f - gShaftDarkness * sqrt(holeDepth), f.wall);
    }

    // 下面：下向き。地面の照り返しだけの暗さ
    const float underside = 1.0f - saturate((p.y - gBottom) / gBottomFade);
    normal = lerp(normal, float3(0.0f, -1.0f, 0.0f), underside);
    ao = lerp(ao, gUndersideBright, underside);

    f.normal = normalize(normal);
    f.ao = ao;
    return f;
}

// 雲の面の陰影（0 = 影色, 1 = 光の色）。p は雲の面の点、distance はミップ用
float SurfaceLight(float3 p, float distance, SurfaceFrame f)
{
    const float ndl = saturate(dot(f.normal, gSunDirection) * (1.0f - gWrap) + gWrap);
    // ゼノブレ式：太陽側の点の透明度で遮る
    const float occlusion = saturate(CloudOver(p + gSunDirection * gLightStep, distance));
    const float sunlit = ndl * exp(-occlusion * gDensity);
    return saturate((sunlit * (1.0f - gAmbient) + gAmbient) * f.ao);
}

// ---------------------------------------------------------------------------
// ゼノブレ式：透明度が 0 を超えた最初の点で止める。rgb = 色（乗算済みではない）, a = 透明度
// ---------------------------------------------------------------------------

// 上から見下ろすレイの高さ場トレース：面との高さの差 /（下向きの傾き ＋ 面の最大傾斜 gTraceSlope）だけ進む。
// 面を飛び越えないので、斜めにかすめるレイでも輪郭が歩幅で階段にならない（等間隔の歩きだと水平方向でギザギザになった）
bool TraceSurface(float3 origin, float3 dir, float tStart, float tEnd, inout float t)
{
    const float descent = max(-dir.y, 1e-4f) + gTraceSlope;
    t = tStart;
    [loop] for (int i = 0; i < gSteps; ++i)
    {
        const float3 p = origin + dir * t;
        const float gap = p.y - SurfaceHeight(p.xz, t);
        if (gap < gTraceEpsilon)
        {
            return true;
        }
        t += gap / descent;
        if (t >= tEnd)
        {
            return false;
        }
    }
    return true; // 歩数を使い切った（地平線付近）。最後の点で止める
}

// 前の点（over < 0）と今の点（over > 0）の間で over = 0 になる位置を線形補間（歩幅の縞を消す）
float CrossingT(float prevT, float prevOver, float t, float over)
{
    return lerp(prevT, t, saturate(prevOver / min(prevOver - over, -1e-6f)));
}

// 等比で歩いて透明度が 0 を超える最初の点を探す（雲の層の中・縦穴の底へ向かうレイ用）
bool FindHitGeometric(float3 origin, float3 dir, float tStart, float tEnd, float jitter, inout float t)
{
    const float span = tEnd - tStart;
    if (span <= 0.0f)
    {
        return false;
    }
    const float growth = log2(1.0f + span / gFirstStep);
    float prevT = tStart;
    float prevOver = CloudOver(origin + dir * tStart, tStart);
    if (prevOver > 0.0f)
    {
        t = tStart;
        return true;
    }
    [loop] for (int i = 1; i <= gSteps; ++i)
    {
        const float k = (float(i) - jitter) / float(gSteps);
        const float ti = tStart + gFirstStep * (exp2(growth * k) - 1.0f);
        const float over = CloudOver(origin + dir * ti, ti);
        if (over > 0.0f)
        {
            t = CrossingT(prevT, prevOver, ti, over);
            return true;
        }
        prevT = ti;
        prevOver = over;
    }
    return false;
}

float4 MarchSurface(float3 origin, float3 dir, float tStart, float tEnd, float jitter)
{
    if (tEnd <= tStart)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 上から見下ろす時は上面を高さ場としてトレースする。当たった所が雲でなければ（穴）、その先を等比で歩き続ける。
    // 雲の層の中（降下の縦穴）からは、近くの壁を拾うため最初から等比で歩く
    float t = tStart;
    bool hit = false;
    if (origin.y > gTop)
    {
        hit = TraceSurface(origin, dir, tStart, tEnd, t);
        if (hit && CloudOver(origin + dir * t, t) <= 0.0f)
        {
            hit = FindHitGeometric(origin, dir, t, tEnd, jitter, t);
        }
    }
    else
    {
        hit = FindHitGeometric(origin, dir, tStart, tEnd, jitter, t);
    }
    if (!hit)
    {
        // 上から見下ろして、物に遮られずに最大距離まで当たらなかった＝水平線のすぐ下。雲海が続いている扱いで水平線の色
        if (origin.y > gTop && tEnd >= gMaxDistance - 1.0f)
        {
            return float4(HorizonSky(dir), 1.0f);
        }
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float3 p = origin + dir * t;

    // 面に当たったら不透明（雲海の面の奥は中身の詰まった雲）。奥を測って半透明にすると、レイの向きでは水平に近い視点で
    // こぶの縁から cubemap の下半分の青が透け、真下へ測るとふたを突き抜けて穴が透けた。遠くは ApplyDistance で水平線の色へ溶かす
    const SurfaceFrame frame = GetSurfaceFrame(p, t);
    const float alpha = 1.0f;

    const float light = SurfaceLight(p, t, frame);
    float3 color = lerp(gShadowColor, gLitColor, light);

    // 縁のリム（太陽の方を見ると前方散乱で光る）
    const float cosSun = dot(dir, gSunDirection);
    const float edge = 4.0f * alpha * (1.0f - alpha);
    color += gRimColor * edge * gRimIntensity * PhaseHG(cosSun, gRimG) * light;
    color += gLitColor * pow(saturate(cosSun), 8.0f) * gSunGlow * light;

    return ApplyDistance(color, alpha, dir, t);
}

// ---------------------------------------------------------------------------
// B'：手前を積み上げる（ふたの中・縦穴の中・入口と出口の近く）。rgb = 乗算済みの色, a = 被覆率。
// 中身も積み上げ、3D ノイズで濃淡と隙間を作る＝雲の中を抜ける「もわもわ」。消散係数 gExtinction で数十〜百 m 先まで透ける。
// （最初の版は消散が強く崩しも弱く、詰まった中身が模様の無い白い壁になった。縁だけにした版は、ふたの中で一色に塗りつぶされた）
// ---------------------------------------------------------------------------
float VolumeDensity(float3 p, float distance)
{
    const float base = saturate(CloudOver(p, distance));
    if (base <= 0.0f)
    {
        return 0.0f;
    }
    // 3D ノイズが gErosion を超える所だけを塊にする（塊と隙間がはっきり分かれる＝もわもわ）。
    // 中身から一律に引くだけだと、何層も積み上げるうちに平均されて模様の無い灰色のもやになった
    const float3 q = p * gErosionScale + gErosionWind * gTime;
    const float noise = saturate((GradientNoise3D(q) * 0.65f + GradientNoise3D(q * 2.7f) * 0.35f - 0.5f) * kErosionContrast + 0.5f);
    return base * saturate((noise - gErosion) * gErosionSharp);
}

float4 MarchVolume(float3 origin, float3 dir, float tStart, float tEnd, float jitter, out float tStop)
{
    tStop = tEnd;
    const float span = tEnd - tStart;
    if (span <= 0.0f)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    const float dt = span / float(gVolumeSteps);
    const float cosSun = dot(dir, gSunDirection);
    const float phase = lerp(1.0f, PhaseHG(cosSun, gRimG), 0.5f);

    float transmittance = 1.0f;
    float3 color = float3(0.0f, 0.0f, 0.0f);
    [loop] for (int i = 0; i < gVolumeSteps; ++i)
    {
        const float t = tStart + (float(i) + jitter) * dt;
        const float3 p = origin + dir * t;
        const float density = VolumeDensity(p, t);
        if (density <= 0.0f)
        {
            continue;
        }
        // 太陽側 2 タップ
        float optical = 0.0f;
        [unroll] for (int s = 1; s <= 2; ++s)
        {
            optical += VolumeDensity(p + gSunDirection * (gVolumeLightStep * s), t) * gVolumeLightStep;
        }
        const float sun = exp(-optical * gExtinction * gLightExtinction);
        const float powder = 1.0f - exp(-density * gPowder);
        const float height = saturate((p.y - gBottom) / max(gTop - gBottom, 1.0f));
        const float3 lit = gLitColor * (sun * powder * phase) + lerp(gShadowColor, gLitColor, height) * gAmbientStrength;

        const float absorb = 1.0f - exp(-density * gExtinction * dt);
        color += transmittance * absorb * lit;
        transmittance *= 1.0f - absorb;
        if (transmittance < 0.01f)
        {
            break;
        }
    }
    const float alpha = 1.0f - transmittance;
    const float4 faded = ApplyDistance(alpha > 1e-4f ? color / alpha : color, alpha, dir, tStart);
    return float4(faded.rgb * faded.a, faded.a);
}

#endif // CLOUD_RAYMARCH_HLSLI
