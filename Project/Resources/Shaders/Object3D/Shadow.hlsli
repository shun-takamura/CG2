// CSM + PCSS（Percentage-Closer Soft Shadows）
// 平行光源1個の影。仮想的な太陽の角サイズ(gShadowLightSize)で、
// 接地点はくっきり・離れるほど半影が広がるコンタクトハードニングを出す。
// Object3d.PS / Object3dNoEnv.PS から include される。

#define SHADOW_CASCADE_COUNT 3
#define SHADOW_MAP_SIZE 2048.0f
#define PCSS_BLOCKER_SAMPLES 16
#define PCSS_PCF_SAMPLES 16
// 深度ギャップ→半影UV半径の換算係数（このエンジンの正射影は深度幅:XY幅 = 3r:2r で一定なので定数でよい）
#define PCSS_PENUMBRA_SCALE 1.5f

cbuffer ShadowBuffer : register(b5)
{
    float4x4 gCascadeViewProj[SHADOW_CASCADE_COUNT];
    float4   gCascadeSplitsView; // 各カスケード遠端のビュー空間Z（現状未使用）
    float    gShadowBias;
    float    gShadowNormalOffset; // 法線方向の受光点オフセット（ワールド単位）
    float    gShadowEnabled;      // 1=影あり, 0=影なし
    float    gShadowLightSize;    // 仮想太陽の角サイズ（ソフトネス）。0で硬い影
}
Texture2DArray<float> gShadowMap : register(t3);
SamplerComparisonState gShadowSampler : register(s1); // 深度比較（PCF）
SamplerState gShadowPointSampler : register(s2);      // 生深度読み（ブロッカー探索）

// Poisson ディスク（16点）。ブロッカー探索と可変PCFのサンプル配置に使う
static const float2 kPoissonDisk[16] =
{
    float2(-0.94201624, -0.39906216), float2(0.94558609, -0.76890725),
    float2(-0.094184101, -0.92938870), float2(0.34495938, 0.29387760),
    float2(-0.91588581, 0.45771432), float2(-0.81544232, -0.87912464),
    float2(-0.38277543, 0.27676845), float2(0.97484398, 0.75648379),
    float2(0.44323325, -0.97511554), float2(0.53742981, -0.47373420),
    float2(-0.26496911, -0.41893023), float2(0.79197514, 0.19090188),
    float2(-0.24188840, 0.99706507), float2(-0.81409955, 0.91437590),
    float2(0.19984126, 0.78641367), float2(0.14383161, -0.14100790)
};

// ガウスカーネルのグリッド半幅（PCF_GRID=2 で 5x5）と σ
#define PCF_GRID 2
#define PCF_SIGMA 1.0f

// 画面座標からカーネル回転角を作る（Interleaved Gradient Noise）。格子模様をノイズ化する。
float2x2 ShadowKernelRotation(float2 screenPos)
{
    float ign = frac(52.9829189f * frac(dot(screenPos, float2(0.06711056f, 0.00583715f))));
    float angle = ign * 6.2831853f; // 2π
    float s = sin(angle);
    float c = cos(angle);
    return float2x2(c, -s, s, c);
}

// ブロッカー探索：受光点より手前(ライト側)にある遮蔽物の平均深度を返す。遮蔽物無しは -1。
float ShadowFindBlockerDepth(float2 uv, int cascade, float receiverDepth, float searchRadius, float2x2 rot)
{
    float sum = 0.0f;
    int count = 0;
    [unroll] for (int i = 0; i < PCSS_BLOCKER_SAMPLES; ++i)
    {
        float2 sampleUv = uv + mul(rot, kPoissonDisk[i]) * searchRadius;
        float d = gShadowMap.SampleLevel(gShadowPointSampler, float3(sampleUv, cascade), 0);
        if (d < receiverDepth)
        {
            sum += d;
            count++;
        }
    }
    if (count == 0)
        return -1.0f;
    return sum / count;
}

// 可変半径の「ガウス重み付き」PCF。中心ほど重く端は滑らかに減衰させ、カーネルの四角い縁を消す。
float ShadowPcfGaussian(float2 uv, int cascade, float receiverDepth, float filterRadius, float2x2 rot)
{
    float sum = 0.0f;
    float weightSum = 0.0f;
    float stepUv = filterRadius / float(PCF_GRID); // グリッド1マスのUV幅（グリッド端が filterRadius）
    [unroll] for (int y = -PCF_GRID; y <= PCF_GRID; ++y)
    {
        [unroll] for (int x = -PCF_GRID; x <= PCF_GRID; ++x)
        {
            float2 cell = float2(x, y);
            float w = exp(-dot(cell, cell) / (2.0f * PCF_SIGMA * PCF_SIGMA)); // ガウス重み
            float2 sampleUv = uv + mul(rot, cell) * stepUv;
            sum += w * gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(sampleUv, cascade), receiverDepth);
            weightSum += w;
        }
    }
    return sum / weightSum;
}

// ワールド座標が平行光源から見て影かを返す（1=照らされる, 0=影）。PCSSで距離に応じた半影。
float CalcShadowFactor(float3 worldPos, float3 normal, float2 screenPos)
{
    if (gShadowEnabled < 0.5f)
        return 1.0f;

    // 受光点を法線方向にずらしてセルフシャドウ（アクネ）を抑える
    float3 offsetPos = worldPos + normal * gShadowNormalOffset;

    // ピクセルごとにカーネルを回転させ、格子状の模様をディザ化
    float2x2 rot = ShadowKernelRotation(screenPos);

    // 近いカスケード（高解像度）から順に、範囲内に収まる最初のものを採用
    for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        float4 lightClip = mul(float4(offsetPos, 1.0f), gCascadeViewProj[i]);
        float3 proj = lightClip.xyz / lightClip.w;
        float2 uv = proj.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
        // 範囲外（XY または 深度）なら次のカスケードへ（宙に黒図形が出る誤判定対策）
        if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
            proj.z < 0.0f || proj.z > 1.0f)
            continue;

        float current = proj.z - gShadowBias;
        float minRadius = 1.0f / SHADOW_MAP_SIZE;
        float searchRadius = max(gShadowLightSize, minRadius);

        // 1. ブロッカー探索
        float blocker = ShadowFindBlockerDepth(uv, i, current, searchRadius, rot);
        if (blocker < 0.0f)
            return 1.0f; // 遮蔽物なし＝照らされる

        // 2. 半影幅の推定（平行光：深度ギャップ×ソフトネス）。接地点ほど小さく、離れるほど大きい。
        float penumbra = (current - blocker) * gShadowLightSize * PCSS_PENUMBRA_SCALE;
        float filterRadius = clamp(penumbra, minRadius, searchRadius);

        // 3. ガウス重み付き可変PCF
        return ShadowPcfGaussian(uv, i, current, filterRadius, rot);
    }
    return 1.0f; // どのカスケードにも入らない＝影なし
}
