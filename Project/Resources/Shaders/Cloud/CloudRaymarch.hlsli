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
// もこもこの陰影の強さの基準にするノイズの値（preview_cloud_raymarch.py の MOKO_NOISE_REF と同じ）
static const float kMokoNoiseRef = 0.65f;

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
    float  gShaftWallBlend; float  gShaftDarkness;  float  gUndersideBright; float gMokoLodBias; // 縦穴の陰影・底面の明るさ / もこもこを粗いミップから読む段数
    float  gCapTop;         float  gCapBottom;      float  gErosionSharp;   float  gLightExtinction; // 縦穴のふた / B' の塊のくっきり度・光側の消散の倍率
    float  gGrazingMin;     float2 gHvWind;         float  gGrazingPower;       // 見下ろす角度の下限 / 雲が風で流れた量 [m]（3D ノイズも一緒に流す）
    // ハイトマップ誘導のボリューム雲（上から見下ろす雲海）。MarchHeightVolume の説明を参照
    int    gHvEnabled;      float  gHvRelief;       float  gHvFloorLow;     float  gHvBumpPower;
    float  gHvFluff;        float  gHvMarchDepth;   float  gHvMaxMarch;     int    gHvSteps;
    float  gHvExtinction;   float  gHvErosion;      float  gHvErosionTiling; float gHvWarp;
    float  gHvWarpTiling;   float  gHvWarpRamp;     int    gHvSegments;     float  gHvLightSigma;
    float  gHvPowderMix;    float  gHvAoBias;       float  gHvAoRange;      float  gHvAoDepth;
    float3 gHvSkyAmbient;   float  gHvAmbientScale;
    float3 gHvLitColor;     float  gHvSunScale;
    float3 gHvShadowColor;  float  gHvShapeTiling;
    float  gHvDetailTiling; float  gHvDetailWeight; float  gHvDetailContrast; float gOcclusionSoft; // gOcclusionSoft: 雲海の上面の影のやわらかさ [m]
    float4 gHvLightSteps;                           // 太陽側の 4 点の距離 [m]
    float  gDetailFadeStart; float gDetailFadeEnd;  float  gDetailFadeLod;  float  gPixelAngle; // 遠くの細部を薄める距離 [m] / 形を粗くする段数 / RT の 1 画素の角度
    int    gAnisoEnabled;   float  gSlopeClampNoise; float gShapeGradLodBias; int gDebugView; // 異方性で読む / 谷の傾きの増幅の上限（このノイズ値で頭打ち）/ 形の傾きを粗く読む段数
    float  gShapeLodBias;   float  gMokoInHeight;   float  gPadding8;       float  gPadding9;       // 形を全距離で粗く読む段数 / もこもこを形に入れる割合（0 = 陰影だけ）
    // 雲海のボリューム（2026-10-23）と時間方向の積み重ね。MarchSeaVolume / CloudTemporal.PS の説明を参照
    float4x4 gPrevViewProj;                         // 前のフレームのビュー射影（積み重ねで前の画面の位置を求める）
    int    gFrameIndex;     int    gHistoryValid;   int    gVolumeSea;      float  gVolShapeTiling;
    float  gVolDetailTiling; float gVolFluff;       float  gVolErosion;     float  gVolExtinction;
    int    gVolSteps;       float  gVolStepMin;     float  gVolStepGrowth;  float  gVolLightSigma;
    float  gVolSunScale;    float  gVolAmbientScale; float gVolPhaseG;      float  gVolPowder;
    float3 gVolSkyAmbient;  float  gVolMacroShadow;
    float3 gVolWind;        float  gTemporalBlend;  // gVolWind: 3D ノイズを読む位置のずれ [m]
    float  gVolDensityGain; float  gVolMultiScatter; float gVolVerticalStretch; float gPadding10; // こぶの濃さの立ち上がり / 多重散乱の 2 段目以降の強さ / 3D の形を縦に引き伸ばす倍率
    float4 gShaftPoints[kShaftPointMax];            // xyz = 降下の線の点（w は未使用）
};

#ifndef CLOUD_NO_TEXTURES
Texture2D<float> gDepth : register(t0);
Texture2D<float2> gNoise : register(t1);
TextureCube<float4> gSkyCube : register(t2);
Texture2D<float> gMoko : register(t3);
// 形・もこもこの傾き（gen_cloud_noise.py --grad。R = d/du, G = d/dv、UV 1 あたり）。上面の法線に使う
Texture2D<float2> gShapeGrad : register(t4);
Texture2D<float2> gMokoGrad : register(t5);
// 雲海のボリュームの 3D ノイズ（gen_cloud_noise3d.py）。R = Perlin-Worley、GBA = Worley の fBm / 細部は RGB = Worley の fBm
Texture3D<float4> gShape3D : register(t6);
Texture3D<float4> gDetail3D : register(t7);
#endif
SamplerState gWrapSampler : register(s0);
SamplerState gPointClampSampler : register(s1);
SamplerState gLinearClampSampler : register(s2);

#ifndef CLOUD_NO_TEXTURES

// ---------------------------------------------------------------------------
// ノイズ
// ---------------------------------------------------------------------------

// 距離からミップを選ぶ（ループの中なので微分は使えない）。tiling は [回/m]
float NoiseLod(float distance, float tiling)
{
    return log2(max(distance * gLodScale * tiling, 1.0f));
}

// 0..1。大きい形ともこもこを混ぜてから gNoiseFloor..1 に寄せる
// 遠くの細部の薄め具合を決める「本当の距離」[m]。ミップ用の distance は浅い角度で水増し（÷|dir.y|）されているので使わない
// （使うと 3° 見下ろしでは 100m 先も 2000m 扱いになり、近くまでのっぺりした）。歩く処理が点ごとに書き込む
static float sFadeDistance = 0.0f;

// 遠くの細部の薄め具合 0..1。浅い角度で見る遠くでは細かい起伏が奥行き方向に押しつぶされ、等高線のような縞に見えた
float DetailFade(float distance)
{
    return smoothstep(gDetailFadeStart, gDetailFadeEnd, sFadeDistance);
}

// ---------------------------------------------------------------------------
// 異方性の読み（2026-10-22）。浅い角度では 1 画素が雲の面の上で「横は数 m、奥行きは数十〜数百 m」の細長い範囲を覆う。
// SampleLevel（縦横同じぼかし）だと横幅に合わせたミップになり、奥行き方向が間引かれて等高線のような縞（モアレ）になった。
// レイの広がりから面の上の縦横の幅（sFootprintA = 横、sFootprintB = 奥行き、[m]）を出して SampleGrad に渡し、
// GPU の異方性フィルタ（s0 = 異方性 16）で奥行き方向だけをぼかす。歩く処理が点ごとに SetFootprint を呼ぶ
// ---------------------------------------------------------------------------
static float2 sFootprintA = float2(0.0f, 0.0f);
static float2 sFootprintB = float2(0.0f, 0.0f);
static bool sUseGrad = false;

void SetFootprint(float3 dir, float t)
{
    const float2 h = dir.xz;
    const float2 fwd = h / max(length(h), 1e-4f);
    sFootprintA = float2(-fwd.y, fwd.x) * (t * gPixelAngle);
    sFootprintB = fwd * (t * gPixelAngle / max(abs(dir.y), gGrazingMin));
    sUseGrad = gAnisoEnabled != 0;
}

// tiling [回/m]、bias = 何段粗く読むか（SampleGrad では幅を 2^bias 倍）
float2 SampleRG(Texture2D<float2> tex, float2 uv, float tiling, float distance, float bias)
{
    [branch]
    if (sUseGrad)
    {
        const float s = tiling * exp2(bias);
        return tex.SampleGrad(gWrapSampler, uv, sFootprintA * s, sFootprintB * s);
    }
    return tex.SampleLevel(gWrapSampler, uv, NoiseLod(distance, tiling) + bias);
}

float SampleR(Texture2D<float> tex, float2 uv, float tiling, float distance, float bias)
{
    [branch]
    if (sUseGrad)
    {
        const float s = tiling * exp2(bias);
        return tex.SampleGrad(gWrapSampler, uv, sFootprintA * s, sFootprintB * s);
    }
    return tex.SampleLevel(gWrapSampler, uv, NoiseLod(distance, tiling) + bias);
}

float CloudNoise(float2 xz, float distance)
{
    const float fade = DetailFade(distance);
    // 形は全距離で gShapeLodBias 段粗く読む。高さは ÷ノイズ なので、谷ではノイズの細かい段が尖った凹凸に拡大され、
    // 浅い角度でその凹凸が手前を隠したり出したりして指紋のような縞になった（法線をなめらかにしても消えなかった）
    const float large = SampleRG(gNoise, xz * gShapeTiling + gShapeOffset, gShapeTiling, distance, fade * gDetailFadeLod + gShapeLodBias).r;
    // もこもこは gMokoLodBias 段粗いミップから読む。上面の傾きは 1/ノイズ² 倍されるので、谷では一番細かい段（小さなこぶの輪）が
    // 急な起伏に化けて細い縞に見えた
    const float moko = SampleR(gMoko, xz * gMokoTiling + gMokoOffset, gMokoTiling, distance, gMokoLodBias);
    // もこもこも同じ理由で形には gMokoInHeight の割合だけ入れる（陰影には GetSurfaceFrame で gMokoWeight のまま入れる）
    return lerp(gNoiseFloor, 1.0f, lerp(large, moko, gMokoWeight * gMokoInHeight * (1.0f - fade)));
}

// 場所ごとの補正値（マスクで大きな塊の所 / 薄い所を作る）
float CloudCoverage(float2 xz, float distance)
{
    const float mask = SampleRG(gNoise, xz * gMaskTiling + gMaskOffset, gMaskTiling, distance, 0.0f).g;
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

    // 上面：高さ場 H = 最高点 − 深さ × 補正値 / ノイズ。法線は焼いた傾き（gShapeGrad / gMokoGrad）から作る。
    // 高さを差分して取ると、バイリニアの折れ目と BC4 の量子化の段差を拾って等高線のような縞が出た
    const float n0 = max(CloudNoise(p.xz, distance), 1e-3f);
    const float coverage = CloudCoverage(p.xz, distance);
    const float fade = DetailFade(distance);
    // 形の傾きは gShapeGradLodBias 段粗く読む（5 オクターブの細かい成分が、谷で増幅されて縞を強めた）
    const float2 shapeGrad = SampleRG(gShapeGrad, p.xz * gShapeTiling + gShapeOffset, gShapeTiling, distance,
        fade * gDetailFadeLod + gShapeLodBias + gShapeGradLodBias) * gShapeTiling;
    const float2 mokoGrad = SampleRG(gMokoGrad, p.xz * gMokoTiling + gMokoOffset, gMokoTiling, distance, gMokoLodBias) * gMokoTiling;
    const float mokoW = gMokoWeight * (1.0f - fade);
    // 大きな形の傾きは 1/ノイズ² 倍（高さの式どおり）。もこもこは一定の強さ（ノイズ kMokoNoiseRef の所の倍率）で足す。
    // もこもこまで 1/ノイズ² 倍すると、谷で細かい段が急な起伏に化けて縞に見えた（CPU 版はこちらで描いていた）
    // 谷での増幅に上限（ノイズ gSlopeClampNoise の所で頭打ち）。谷（ノイズ 0.3 前後）では頂上の約 11 倍になり、小さな起伏が縞になった
    const float nk = max(n0, gSlopeClampNoise);
    const float k = gSeaDepth * coverage / (nk * nk) * (1.0f - gNoiseFloor);
    const float kMoko = gSeaDepth * coverage / (kMokoNoiseRef * kMokoNoiseRef) * (1.0f - gNoiseFloor);
    // 形の傾きは形に入れたもこもこの分だけ弱める（形に入れない時は形の傾きをそのまま、もこもこは陰影にだけ足す）
    const float2 slope = k * (1.0f - mokoW * gMokoInHeight) * shapeGrad + kMoko * mokoW * mokoGrad;
    float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));
    // 谷：こぶの高さの範囲（ノイズ 1 〜 gNoiseFloor の面の高さ）の中で低いほど暗い。
    // 止まった点の高さではなくその場所の上面の高さで決める（遠くはレイが面の少し上で止まり、止まる高さが段々に変わる）
    const float depthMin = gSeaDepth * coverage;
    const float depthMax = gSeaDepth * coverage / max(gNoiseFloor, 1e-3f);
    const float surfaceY = min(p.y, SurfaceHeight(p.xz, distance));
    const float valley = saturate((gTop - surfaceY - depthMin) / max(depthMax - depthMin, 1.0f));
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
// 影の判定用の上面の高さ。大きな形だけ（もこもこ無し）を kOcclusionLodBias 段粗く読む。
// もこもこ込みの高さだと小さな起伏で影が付いたり消えたりして、影の中に横縞が出た（確認用の表示「3」）
static const float kOcclusionLodBias = 2.0f;

float OcclusionHeight(float2 xz, float distance)
{
    const float fade = DetailFade(distance);
    const float large = SampleRG(gNoise, xz * gShapeTiling + gShapeOffset, gShapeTiling, distance,
        fade * gDetailFadeLod + max(gShapeLodBias, kOcclusionLodBias)).r;
    const float noise = max(lerp(gNoiseFloor, 1.0f, large), 1e-3f);
    return gTop - min(gSeaDepth * CloudCoverage(xz, distance) / noise, gSeaDepth);
}

// 太陽側の遮り 0..1（1 = 遮られている）。
// 上面は高さの地形の影：太陽の方へ進んだ点（3 点）の真下の上面がその点より高いほど影（3 点の平均で段階的に）。
// 以前は濃さの式（上面のすぐ近くで 0→1 に急変する薄い膜）を 18m 先で調べていて、膜をかすめる所で縞模様の斑点になった
// （確認用の表示「3: 太陽側の遮り」で特定）。縦穴の壁・下面は従来どおり濃さの式で
float SurfaceOcclusion(float3 p, float distance, SurfaceFrame f)
{
    // 出発点も同じ粗い高さに置く（細かい起伏の上下で判定がぶれないように）
    const float3 base = float3(p.x, OcclusionHeight(p.xz, distance), p.z);
    float occ = 0.0f;
    [unroll] for (int i = 1; i <= 3; ++i)
    {
        const float3 q = base + gSunDirection * (gLightStep * i);
        occ += saturate((OcclusionHeight(q.xz, distance) - q.y) / gOcclusionSoft);
    }
    occ *= 1.0f / 3.0f;
    [branch]
    if (f.wall > 0.0f)
    {
        occ = lerp(occ, saturate(CloudOver(p + gSunDirection * gLightStep, distance)), f.wall);
    }
    return occ;
}

float SurfaceLight(float3 p, float distance, SurfaceFrame f)
{
    const float ndl = saturate(dot(f.normal, gSunDirection) * (1.0f - gWrap) + gWrap);
    const float occlusion = SurfaceOcclusion(p, distance, f);
    const float sunlit = ndl * exp(-occlusion * gDensity);
    return saturate((sunlit * (1.0f - gAmbient) + gAmbient) * f.ao);
}

// ---------------------------------------------------------------------------
// ゼノブレ式：透明度が 0 を超えた最初の点で止める。rgb = 色（乗算済みではない）, a = 透明度
// ---------------------------------------------------------------------------

// 上から見下ろすレイの高さ場トレース：面との高さの差 /（下向きの傾き ＋ 面の最大傾斜 gTraceSlope）だけ進む。
// 面を飛び越えないので、斜めにかすめるレイでも輪郭が歩幅で階段にならない（等間隔の歩きだと水平方向でギザギザになった）
// 止まった点（面の gTraceEpsilon 上）から面の上へ寄せる。直前の点との高さの差の縮み方から残りを外挿する（2 回）。
// 浅い角度では面の 0.5m 上でも横に数 m 手前で、何歩目で止まったかが変わる所で陰影が段になり、等高線の縞に見えた
void RefineSurfaceHit(float3 origin, float3 dir, float grazing, float prevT, float prevGap, inout float t, float gap)
{
    [unroll] for (int k = 0; k < 2; ++k)
    {
        const float rate = (prevGap - gap) / max(t - prevT, 1e-3f); // 1m 進むごとに縮む高さの差
        if (prevGap > 1e8f || rate <= 0.0f)
        {
            return;
        }
        prevT = t;
        prevGap = gap;
        t += gap / max(rate, 0.02f); // 縮み方がほぼ 0 の所で遠くへ飛ばない上限（0.5m の差で 25m）
        const float3 p = origin + dir * t;
        sFadeDistance = t;
        SetFootprint(dir, t);
        gap = p.y - SurfaceHeight(p.xz, t / grazing);
    }
}

bool TraceSurface(float3 origin, float3 dir, float tStart, float tEnd, inout float t)
{
    const float descent = max(-dir.y, 1e-4f) + gTraceSlope;
    // 浅い角度ほど粗いミップ（|dir.y|^gGrazingPower で割る）。1 乗だと浅い角度の細かい凹凸が画素より細くなり、縞・点線に見えた
    const float grazing = pow(max(abs(dir.y), gGrazingMin), gGrazingPower);
    t = tStart;
    float prevT = tStart;
    float prevGap = 1e9f;
    [loop] for (int i = 0; i < gSteps; ++i)
    {
        const float3 p = origin + dir * t;
        sFadeDistance = t;
        SetFootprint(dir, t);
        const float gap = p.y - SurfaceHeight(p.xz, t / grazing);
        if (gap < gTraceEpsilon)
        {
            RefineSurfaceHit(origin, dir, grazing, prevT, prevGap, t, gap);
            return true;
        }
        prevT = t;
        prevGap = gap;
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
        sFadeDistance = ti;
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

// ---------------------------------------------------------------------------
// ハイトマップ誘導のボリューム雲（上から見下ろす雲海。2026-10-22、CPU 版 preview_cloud_raymarch.py の HeightVolume と同じ式）
//   ① 上面 HvTop(x,z) を高さの地形としてトレース（面の上まで 2 分探索で戻す）
//   ② 上面の下の「綿の層」だけを gHvSteps 歩積み上げる。濃さ = 上面からの深さ ＋ 3D ノイズ（縁が溶けてふわっとする）
//   ③ 陰影は法線を使わず、太陽側へ 4 点濃さを積む光側マーチ＋powder＋谷の暗さ（ぼかした上面との高さ比較）
//   ④ 綿の層を抜けて空中に出たレイは、次の上面まで歩き直す（gHvSegments 回）
//   法線を使わないので、高さの差分で法線を取っていた頃の縞は出ない。縦穴はふたで隠れるので上からは扱わない
// ---------------------------------------------------------------------------

float HvHeightNoise(float2 xz, float distance, float lodBias)
{
    // 大小とも同じ「雲の座標」（風で流れた量を引いた位置）で引く。UV のずれを共有すると大小が違う速さで流れて形が変わり続けた
    const float2 c = xz - gHvWind;
    const float large = gNoise.SampleLevel(gWrapSampler, c * gHvShapeTiling, NoiseLod(distance, gHvShapeTiling) + lodBias).r;
    const float small = gNoise.SampleLevel(gWrapSampler, c * gHvDetailTiling + float2(0.517f, 0.263f),
        NoiseLod(distance, gHvDetailTiling) + lodBias).r;
    const float n = lerp(large, small, gHvDetailWeight);
    return saturate(0.5f + (n - 0.5f) * gHvDetailContrast);
}

// 上面の高さ。ノイズが gHvFloorLow から 1 にかけてなだらかに gHvRelief [m] 盛り上がる
float HvTop(float2 xz, float distance, float lodBias)
{
    const float n = HvHeightNoise(xz, distance, lodBias);
    const float x = saturate((n - gHvFloorLow) / max(1.0f - gHvFloorLow, 1e-3f));
    const float bump = x * x * (3.0f - 2.0f * x);
    return gTop - gHvRelief * (1.0f - pow(bump, gHvBumpPower));
}

// 0..1 に広げた 3D ノイズ（勾配ノイズ 2 オクターブは 0.41〜0.59 に固まるので kErosionContrast 倍）
float HvNoise3D(float3 q)
{
    return saturate((GradientNoise3D(q) * 0.65f + GradientNoise3D(q * 2.7f) * 0.35f - 0.5f) * kErosionContrast + 0.5f);
}

// 綿の層の濃さ 0..1
float HvDensity(float3 p, float distance)
{
    float2 xz = p.xz;
    // 3D ノイズは雲と一緒に風で流す（ハイトマップだけ流れて 3D ノイズが止まっていると、形がうねうね変わり続けた）
    const float3 cloudP = p - float3(gHvWind.x, 0.0f, gHvWind.y);
    // ハイトマップを読む位置を 3D ノイズでずらす（急な側面で模様が縦に伸びるのを崩す）。上面の近くはずらさない（切れ端が浮いた）
    const float ramp = saturate((HvTop(xz, distance, 0.0f) - p.y) / gHvWarpRamp);
    [branch]
    if (ramp > 0.0f)
    {
        const float3 w = cloudP * gHvWarpTiling;
        xz += (float2(GradientNoise3D(w), GradientNoise3D(w + 31.7f)) - 0.5f) * 2.0f * gHvWarp * ramp;
    }
    const float depth = (HvTop(xz, distance, 0.0f) - p.y) / gHvFluff;
    return saturate(depth + (HvNoise3D(cloudP * gHvErosionTiling) - 0.5f) * gHvErosion);
}

// その点の色（光の色 × 太陽の届き方 ＋ 環境光）
float3 HvLight(float3 p, float distance, float density)
{
    // 太陽側へ 4 点、濃さを積む（自分の厚みも、隣の塊が落とす影もこれで入る）
    float od = 0.0f;
    float prev = 0.0f;
    [unroll] for (int i = 0; i < 4; ++i)
    {
        const float s = gHvLightSteps[i];
        od += HvDensity(p + gSunDirection * s, distance) * (s - prev);
        prev = s;
    }
    const float sunVis = exp(-gHvLightSigma * od);
    const float powder = 1.0f - exp(-2.0f * density);
    const float sunTerm = sunVis * ((1.0f - gHvPowderMix) + gHvPowderMix * powder);

    // 谷の暗さ：その場所の上面が、まわり（粗いミップ）より低いほど空が見えず暗い。深いほど暗い
    const float tHere = HvTop(p.xz, distance, 0.0f);
    const float tAvg = HvTop(p.xz, distance, gHvAoBias);
    float ao = saturate(0.5f + (min(p.y, tHere) - tAvg) / gHvAoRange);
    ao *= exp(-max(tHere - p.y, 0.0f) / gHvAoDepth);
    const float3 ambient = lerp(gHvShadowColor, gHvSkyAmbient, ao);
    return gHvLitColor * (sunTerm * gHvSunScale) + ambient * gHvAmbientScale;
}

// 上面に当たるまで：面との高さの差 /（下向きの傾き＋最大傾斜）だけ進む。突き抜けた分は 2 分探索で戻す
bool HvTrace(float3 origin, float3 dir, float tStart, float tEnd, out float t)
{
    const float descent = max(-dir.y, 1e-4f) + gTraceSlope;
    t = tStart;
    bool hit = false;
    [loop] for (int i = 0; i < gSteps; ++i)
    {
        const float3 p = origin + dir * t;
        const float gap = p.y - HvTop(p.xz, t, 0.0f);
        if (gap < gTraceEpsilon)
        {
            hit = true;
            break;
        }
        t += gap / descent;
        if (t >= tEnd)
        {
            return false;
        }
    }
    if (!hit)
    {
        return true; // 歩数を使い切った（地平線付近）。最後の点で止める
    }
    float lo = max(t - 200.0f, tStart);
    float hi = t;
    [unroll] for (int j = 0; j < 8; ++j)
    {
        const float mid = 0.5f * (lo + hi);
        const float3 q = origin + dir * mid;
        if (q.y < HvTop(q.xz, mid, 0.0f)) hi = mid; else lo = mid;
    }
    t = hi;
    return true;
}

// rgb = 色（乗算済みではない）, a = 透明度。上から見下ろす下向きのレイは必ず a = 1（抜けた分は水平線の色）
float4 MarchHeightVolume(float3 origin, float3 dir, float tStart, float tEnd)
{
    const float slope = max(-dir.y, gGrazingMin);
    float3 color = float3(0.0f, 0.0f, 0.0f);
    float trans = 1.0f;
    float3 lastColor = gHvShadowColor;
    float firstHit = -1.0f;
    bool endInside = false;
    float tCur = tStart;

    [loop] for (int seg = 0; seg < gHvSegments; ++seg)
    {
        float tHit;
        if (!HvTrace(origin, dir, tCur, tEnd, tHit))
        {
            break;
        }
        if (firstHit < 0.0f)
        {
            firstHit = tHit;
        }
        const float marchLength = min(gHvFluff * gHvMarchDepth / slope, gHvMaxMarch);
        const float dt = marchLength / float(gHvSteps);
        [loop] for (int i = 0; i < gHvSteps; ++i)
        {
            const float t = tHit + (float(i) + 0.5f) * dt;
            const float3 p = origin + dir * t;
            const float density = HvDensity(p, t);
            if (density <= 0.0f)
            {
                continue;
            }
            const float a = 1.0f - exp(-gHvExtinction * density * dt);
            const float3 c = HvLight(p, t, density);
            if (density > 0.05f)
            {
                lastColor = c;
            }
            color += trans * a * c;
            trans *= 1.0f - a;
            if (trans < 0.02f)
            {
                break;
            }
        }
        const float tNext = tHit + marchLength;
        const float3 q = origin + dir * tNext;
        endInside = q.y < HvTop(q.xz, tNext, 0.0f) - gHvFluff * 0.5f;
        // 中身の詰まった所で終わったか、もう透けない所は終わり。空中に出たら次の上面まで歩き直す
        if (endInside || trans < 0.02f || tNext >= tEnd)
        {
            break;
        }
        tCur = tNext;
    }

    // 中身の詰まった雲の中で終わった分は、それまで積んだ色の平均で埋める（最後の 1 点の色だと明るさのずれた板になった）
    if (firstHit >= 0.0f && endInside && trans > 0.0f)
    {
        const float acc = 1.0f - trans;
        const float3 meanColor = color / max(acc, 1e-4f);
        color += trans * lerp(lastColor, meanColor, saturate(acc / 0.01f));
        trans = 0.0f;
    }

    // 空気遠近と遠くのフェード（最初に当たった距離で）。抜けた分は水平線の色で埋める
    const float alpha = 1.0f - trans;
    const float3 horizon = HorizonSky(dir);
    const float dist = (firstHit >= 0.0f) ? firstHit : gMaxDistance;
    const float haze = saturate((dist - gHazeStart) / max(gHazeEnd - gHazeStart, 1.0f)) * gHazeMax;
    const float fade = saturate((dist - gFadeStart) / max(gFadeEnd - gFadeStart, 1.0f));
    color = lerp(color, horizon * alpha, max(haze, fade));
    color += (1.0f - alpha) * horizon;
    return float4(color, 1.0f);
}

// ---------------------------------------------------------------------------
// 雲海のボリューム（2026-10-23。上から見下ろす雲海。Nubis / Horizon Zero Dawn の雲の作り方を雲海用に）
//   形：大枠は 2D の形（ゼノブレ式の高さ、gShapeLodBias 段粗く読んだなめらかな面）＝ SeaTop。
//       その面の上下 gVolFluff [m] を「こぶの層」にして、3D の Perlin-Worley で上ほど薄く削る（上ほど濃い値だけ残る＝頭の丸いこぶ）。
//       さらに細部の Worley で縁を削ってカリフラワー状にする。層より深い所は詰まった雲
//   歩き方：上面＋層の半分（こぶの頭の上限）までは高さ場で飛ばし、そこから濃さを積む。こぶの上の空中に出たらまた飛ばす
//   光：太陽側の 2 点の濃さ（自分の塊の厚み）＋ 2 点の上面の高さ（隣の大きな塊の影）を透過率にし、多重散乱の近似（3 段）と
//       前方・後方の位相、Powder、上からの空の光（深いほど弱い）
//   縞：歩き始めを毎フレーム揺らし、CloudTemporal.PS で前のフレームと混ぜて平均する（面の当たり＋法線の方式で出ていた
//       指紋・等高線の縞は、面の形の細かい凹凸と法線の折れ目が原因で、ボリュームでは出ない）
// ---------------------------------------------------------------------------

static float sCloudDepth = 0.0f; // 雲の代表の距離 [m]（時間方向の積み重ねで前の画面の位置を求める）
static float sLightJitter = 1.0f; // 太陽側の点の距離の揺らし（0.75〜1.25。決まった距離だと濃さの段差が等高線の縞になる）

float Remap(float v, float lo, float hi, float newLo, float newHi)
{
    return newLo + (v - lo) / max(hi - lo, 1e-5f) * (newHi - newLo);
}

// テクセルの間をなめらかにつなぐ読み方（1 回の読みのまま、補間の重みを smoothstep にする）。
// 線形補間はテクセルの境目で傾きが折れ、浅い角度で見下ろすと折れ目が層状の弧（等高線のような縞）に見えた。
// 重みを smoothstep にすると境目で傾きがつながる。ミップは整数段 2 つを混ぜる（段ごとにテクセルの大きさが違う）
float3 SmoothUvw(float3 uvw, float size)
{
    const float3 x = uvw * size - 0.5f;
    const float3 i = floor(x);
    float3 f = x - i;
    f = f * f * (3.0f - 2.0f * f);
    return (i + f + 0.5f) / size;
}

float4 SampleSmooth3D(Texture3D<float4> tex, float3 uvw, float lod, float size)
{
    const float l0 = floor(lod);
    const float4 a = tex.SampleLevel(gWrapSampler, SmoothUvw(uvw, size * exp2(-l0)), l0);
    const float blend = lod - l0;
    [branch]
    if (blend > 0.01f)
    {
        const float4 b = tex.SampleLevel(gWrapSampler, SmoothUvw(uvw, size * exp2(-l0 - 1.0f)), l0 + 1.0f);
        return lerp(a, b, blend);
    }
    return a;
}

float SampleSmooth2DR(Texture2D<float2> tex, float2 uv, float lod, float size)
{
    const float l0 = floor(lod);
    const float a = tex.SampleLevel(gWrapSampler, SmoothUvw(float3(uv, 0.0f), size * exp2(-l0)).xy, l0).r;
    const float blend = lod - l0;
    [branch]
    if (blend > 0.01f)
    {
        const float b = tex.SampleLevel(gWrapSampler, SmoothUvw(float3(uv, 0.0f), size * exp2(-l0 - 1.0f)).xy, l0 + 1.0f).r;
        return lerp(a, b, blend);
    }
    return a;
}

// 補正値（マスクは 9km 周期でゆっくり変わるので、雲海のボリュームでは画素ごとに 1 回だけ読む。負なら毎回読む）
static float sCoverage = -1.0f;

// なめらかな上面の高さ（大きい形を gShapeLodBias 段粗く ＋ マスク）
float SeaTop(float2 xz, float distance)
{
    const float large = SampleSmooth2DR(gNoise, xz * gShapeTiling + gShapeOffset, NoiseLod(distance, gShapeTiling) + gShapeLodBias, 512.0f);
    const float noise = lerp(gNoiseFloor, 1.0f, large);
    float coverage = sCoverage;
    if (coverage < 0.0f)
    {
        const float mask = gNoise.SampleLevel(gWrapSampler, xz * gMaskTiling + gMaskOffset, NoiseLod(distance, gMaskTiling)).g;
        coverage = gCoverage - (mask - 0.5f) * gMaskStrength;
    }
    return gTop - min(gSeaDepth * coverage / noise, gSeaDepth);
}

// 3D ノイズのミップ（1 画素の幅とテクセルの大きさから）
float VolLod(float distance, float tiling, float size)
{
    return log2(max(distance * gPixelAngle * tiling * size, 1.0f));
}

// 濃さ 0..1。top = その場所の SeaTop。detail = 細部で縁を削るか（光側の点では省く）
float VolDensity(float3 p, float top, float distance, bool detail)
{
    const float s = top - p.y;                                   // なめらかな上面からの深さ
    const float h = saturate((s + gVolFluff * 0.5f) / gVolFluff); // こぶの層の中の位置（0 = こぶの頭の上限 → 1 = 層の底）
    [branch]
    if (h <= 0.0f)
    {
        return 0.0f;
    }
    // 縦は引き伸ばして読む（縦の変化が細かいと、高さの勾配で決まる雲の面が段々の棚になり、浅い角度で横縞に見えた）
    const float3 q = (p + gVolWind) * float3(1.0f, 1.0f / gVolVerticalStretch, 1.0f) * gVolShapeTiling;
    const float4 n = SampleSmooth3D(gShape3D, q, VolLod(distance, gVolShapeTiling, 128.0f), 128.0f);
    const float fbm = n.g * 0.625f + n.b * 0.25f + n.a * 0.125f;
    float shape = saturate(Remap(n.r, fbm - 1.0f, 1.0f, 0.0f, 1.0f));
    // 上ほど濃い値だけ残す（こぶの頭が丸く盛り上がる）。Nubis と同じく remap の後に被覆率を掛け戻す＝ shape − (1 − h)。
    // remap だけ（÷h）だと上の方でノイズの小さな差が何倍にも拡大され、テクセルの境目が層状の段（等高線の縞）に見えた
    const float hh = pow(h, 0.7f);
    float density = saturate((shape - (1.0f - hh)) * gVolDensityGain);
    [branch]
    if (detail && density > 0.0f && density < 0.999f)
    {
        const float3 dq = (p + gVolWind) * gVolDetailTiling;
        const float3 d = SampleSmooth3D(gDetail3D, dq, VolLod(distance, gVolDetailTiling, 32.0f), 32.0f).rgb;
        const float dfbm = d.r * 0.625f + d.g * 0.25f + d.b * 0.125f;
        // 塊の境目（Worley の外側）を削る＝丸いこぶが残る。遠くは細部が画素より細かいので弱める
        const float far = saturate(distance / 6000.0f);
        const float erode = (1.0f - dfbm) * gVolErosion * (1.0f - far) * (1.0f - h * 0.6f);
        density = saturate(Remap(density, erode, 1.0f, 0.0f, 1.0f));
    }
    // 層より深い所は詰まった雲（中が抜けない）
    density = max(density, saturate((s - gVolFluff * 0.5f) / gVolFluff));

    // 降下の縦穴（壁・天井・床は従来の式）
    float3 closest;
    float dist;
    [branch]
    if (InHoleRegion(p, closest, dist))
    {
        density *= saturate(HoleOver(p, distance, closest, dist));
    }
    return density;
}

// 位相（等方 = 1 になるよう 4π を掛けた Henyey-Greenstein）。前方と少しの後方を混ぜる
// 半分は等方（太陽を背にして見ても日の当たる頭が白い。HG だけだと前方以外が暗く、雲全体が灰色に沈んだ）
float PhaseDual(float cosTheta, float g)
{
    return lerp(1.0f, lerp(PhaseHG(cosTheta, g), PhaseHG(cosTheta, -0.25f), 0.25f), 0.5f);
}

// その点の光（乗算済みではない色）
float3 VolLight(float3 p, float density, float top, float distance, float cosTheta)
{
    // 太陽側の 2 点：自分の塊の厚み（3D の濃さ、細部なし）
    const float k = gVolFluff / 80.0f * sLightJitter;
    float tau = 0.0f;
    {
        const float3 q1 = p + gSunDirection * (8.0f * k);
        tau += VolDensity(q1, top, distance, false) * (8.0f * k);
        const float3 q2 = p + gSunDirection * (28.0f * k);
        tau += VolDensity(q2, top, distance, false) * (20.0f * k);
    }
    // 太陽側の 2 点：隣の大きな塊の影（上面より下は詰まっている扱い）
    float tauMacro = 0.0f;
    {
        const float3 q3 = p + gSunDirection * (90.0f * sLightJitter);
        tauMacro += saturate((SeaTop(q3.xz, distance) - q3.y) / gVolFluff) * 60.0f;
        const float3 q4 = p + gSunDirection * (220.0f * sLightJitter);
        tauMacro += saturate((SeaTop(q4.xz, distance) - q4.y) / gVolFluff) * 130.0f;
    }
    const float od = (tau * gVolLightSigma + tauMacro * gVolMacroShadow) * gVolExtinction;

    // 多重散乱の近似（Wrenninge）：段ごとに散乱を弱め・透過を強め・前方散乱を弱める
    float sun = 0.0f;
    float a = 1.0f;
    float b = 1.0f;
    float c = 1.0f;
    [unroll] for (int i = 0; i < 3; ++i)
    {
        sun += a * PhaseDual(cosTheta, gVolPhaseG * c) * exp(-od * b);
        a *= gVolMultiScatter; // 2 段目以降の強さ（小さいほど影が締まる）
        b *= 0.35f;
        c *= 0.5f;
    }
    // Powder：薄い所（縁）は中まで光が回らず暗い
    const float powder = lerp(1.0f, 1.0f - exp(-density * gVolExtinction * 60.0f), gVolPowder);

    // 上からの空の光：こぶの頭ほど明るく、上面から深いほど暗い。上（太陽側）に雲が被さっている所（こぶの間の谷・張り出しの下）も暗い
    const float s = top - p.y + gVolFluff * 0.5f;
    const float skyVis = exp(-max(s, 0.0f) / gVolFluff * 1.5f) * exp(-tau * gVolExtinction * 0.5f);
    const float3 ambient = lerp(gShadowColor * 0.6f, gVolSkyAmbient, skyVis) * gVolAmbientScale;
    // 確認用の表示（ImGui「Cloud Debug View」）：8 = 太陽側の透過率 / 9 = 空の光だけ
    if (gDebugView == 8) return exp(-od).xxx;
    if (gDebugView == 9) return ambient;
    return gLitColor * (sun * powder * gVolSunScale) + ambient;
}

// rgb = 乗算済みの色, a = 被覆率。sCloudDepth に代表の距離を書く
float4 MarchSeaVolume(float3 origin, float3 dir, float tStart, float tEnd, float jitter)
{
    const float descent = max(-dir.y, 1e-4f) + gTraceSlope;
    const float rise = gVolFluff * 0.5f;
    const float cosTheta = dot(dir, gSunDirection);
    sCloudDepth = gMaxDistance;
    sLightJitter = 0.75f + 0.5f * frac(jitter * 7.31f + 0.37f);
    // マスクはこの画素の 1 km 先あたりで 1 回だけ読む（9km 周期なので画素の中ではほぼ一定）
    {
        const float3 pm = origin + dir * min(tStart + 1000.0f, tEnd);
        const float mask = gNoise.SampleLevel(gWrapSampler, pm.xz * gMaskTiling + gMaskOffset, NoiseLod(tStart, gMaskTiling)).g;
        sCoverage = gCoverage - (mask - 0.5f) * gMaskStrength;
    }

    // こぶの頭の上限（上面＋層の半分）まで高さ場で飛ばす
    float t = tStart;
    bool reached = false;
    [loop] for (int i = 0; i < gSteps; ++i)
    {
        const float3 p = origin + dir * t;
        const float gap = p.y - (SeaTop(p.xz, t) + rise);
        if (gap < 1.0f)
        {
            reached = true;
            break;
        }
        t += gap / descent;
        if (t >= tEnd)
        {
            break;
        }
    }
    if (!reached)
    {
        if (t < tEnd)
        {
            reached = true; // 歩数を使い切った（地平線付近）。そこから積む
        }
        else
        {
            return (tEnd >= gMaxDistance - 1.0f) ? float4(HorizonSky(dir), 1.0f) : float4(0.0f, 0.0f, 0.0f, 0.0f);
        }
    }

    float3 color = float3(0.0f, 0.0f, 0.0f);
    float trans = 1.0f;
    float depthSum = 0.0f;
    float weightSum = 0.0f;
    // 1 歩で最低これだけ [m] 下りる（浅い角度では層の中を水平に長く進むので、距離だけで歩幅を決めると歩数が足りなかった）
    const float minDrop = 1.2f;
    const float dropStep = minDrop / max(-dir.y, 0.03f);
    t += max(max(gVolStepMin, t * gVolStepGrowth), dropStep) * jitter;
    bool exhausted = true;
    float emptyRun = 0.0f;
    [loop] for (int j = 0; j < gVolSteps; ++j)
    {
        if (t >= tEnd)
        {
            exhausted = false;
            break;
        }
        const float3 p = origin + dir * t;
        // 雲の無い所が続いたら歩幅を広げる（最大 2 倍）
        const float ds = max(max(gVolStepMin, t * gVolStepGrowth), dropStep) * (1.0f + emptyRun);
        const float top = SeaTop(p.xz, t);
        const float gap = p.y - (top + rise);
        [branch]
        if (gap > 0.0f)
        {
            // こぶの上の空中：次のこぶの頭まで飛ばし、着いた所でまた揺らす（揺らさないと着く位置が毎フレーム同じになり、
            // こぶの頭の等高線に沿った縞が残った）
            t += gap / descent + ds * frac(jitter + float(j) * 0.61803399f);
            continue;
        }
        const float density = VolDensity(p, top, t, true);
        emptyRun = (density > 0.002f) ? 0.0f : min(emptyRun + 0.25f, 1.0f);
        [branch]
        if (density > 0.002f)
        {
            const float sigma = gVolExtinction * density;
            const float a = 1.0f - exp(-sigma * ds);
            const float3 light = VolLight(p, density, top, t, cosTheta);
            color += trans * a * light;
            depthSum += trans * a * t;
            weightSum += trans * a;
            trans *= 1.0f - a;
            if (trans < 0.01f)
            {
                exhausted = false;
                break;
            }
        }
        t += ds;
    }

    // 歩数を使い切った分は、それまで積んだ色の平均で埋める（雲海は奥まで続いている。水平線の色で埋めると谷に青い穴が開いた）。
    // 何も積んでいなければ水平線の色。不透明物で止まった分は透かす
    const float3 horizon = HorizonSky(dir);
    float alpha = 1.0f - trans;
    if (exhausted || trans < 0.01f)
    {
        const float3 fill = (alpha > 0.02f) ? color / alpha : horizon;
        color += trans * fill;
        alpha = 1.0f;
    }
    sCloudDepth = (weightSum > 1e-4f) ? depthSum / weightSum : min(t, gMaxDistance);
    // 確認用の表示：10 = 代表の距離（40m ごとの縞）
    if (gDebugView == 10)
    {
        return float4(frac(sCloudDepth / 40.0f).xxx, 1.0f);
    }
    const float haze = saturate((sCloudDepth - gHazeStart) / max(gHazeEnd - gHazeStart, 1.0f)) * gHazeMax;
    const float fade = saturate((sCloudDepth - gFadeStart) / max(gFadeEnd - gFadeStart, 1.0f));
    color = lerp(color, horizon * alpha, max(haze, fade));
    // 呼び出し側（MarchSurface の約束）は乗算済みでない色
    return float4(alpha > 1e-4f ? color / alpha : color, alpha);
}

float4 MarchSurface(float3 origin, float3 dir, float tStart, float tEnd, float jitter)
{
    [branch]
    if (gVolumeSea != 0 && origin.y > gTop)
    {
        return MarchSeaVolume(origin, dir, tStart, tEnd, jitter);
    }
    if (tEnd <= tStart)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
    [branch]
    if (gHvEnabled != 0 && origin.y > gTop)
    {
        return MarchHeightVolume(origin, dir, tStart, tEnd);
    }

    // 上から見下ろす時は上面を高さ場としてトレースする。当たった所が雲でなければ（穴）、その先を等比で歩き続ける。
    // 雲の層の中（降下の縦穴）からは、近くの壁を拾うため最初から等比で歩く
    float t = tStart;
    bool hit = false;
    if (origin.y > gTop)
    {
        hit = TraceSurface(origin, dir, tStart, tEnd, t);
        // 穴かどうかは止まった点ではなく、その場所の上面より 1m 下で調べる。トレースは面の gTraceEpsilon 上で止まり、
        // そこは式の上では必ず雲の外なので、ほぼ全画素が「穴」扱いで等比の歩きに回り、当たり位置のずれが木目の縞になった
        const float3 stop = origin + dir * t;
        const float3 below = float3(stop.x, min(stop.y, SurfaceHeight(stop.xz, t)) - 1.0f, stop.z);
        if (hit && CloudOver(below, t) <= 0.0f)
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
    sCloudDepth = t;

    // 面に当たったら不透明（雲海の面の奥は中身の詰まった雲）。奥を測って半透明にすると、レイの向きでは水平に近い視点で
    // こぶの縁から cubemap の下半分の青が透け、真下へ測るとふたを突き抜けて穴が透けた。遠くは ApplyDistance で水平線の色へ溶かす
    // 陰影のミップは見る角度でも粗くする。浅い角度では 1 画素が奥行き方向に長い範囲を覆い、細かい凹凸がモアレ（木目の縞）になった
    const float shadeDistance = t / pow(max(abs(dir.y), gGrazingMin), gGrazingPower);
    sFadeDistance = t;
    SetFootprint(dir, t);
    const SurfaceFrame frame = GetSurfaceFrame(p, shadeDistance);
    const float alpha = 1.0f;

    // 確認用の表示（ImGui「StagePlay Tuning」→「Cloud Debug View」）。縞がどの項目から出ているかを見分ける
    [branch]
    if (gDebugView != 0)
    {
        float3 v = float3(1.0f, 0.0f, 1.0f);
        if (gDebugView == 1)       v = frame.normal * 0.5f + 0.5f;                                                   // 法線
        else if (gDebugView == 2)  v = saturate(dot(frame.normal, gSunDirection) * (1.0f - gWrap) + gWrap).xxx;      // 法線の当たり
        else if (gDebugView == 3)  v = (1.0f - SurfaceOcclusion(p, shadeDistance, frame)).xxx;                       // 太陽側の遮り（白＝遮りなし）
        else if (gDebugView == 4)  v = frame.ao.xxx;                                                                 // 谷の暗さ
        else if (gDebugView == 5)  v = frac(t / 25.0f).xxx;                                                          // 当たった距離（25m ごとの縞）
        else if (gDebugView == 6)  v = frac((gTop - p.y) / 5.0f).xxx;                                                // 当たった高さ（5m ごとの縞）
        else if (gDebugView == 7)  v = SurfaceLight(p, shadeDistance, frame).xxx;                                    // 陰影の合計
        return float4(v, 1.0f);
    }

    const float light = SurfaceLight(p, shadeDistance, frame);
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

#endif // CLOUD_NO_TEXTURES

#endif // CLOUD_RAYMARCH_HLSLI
