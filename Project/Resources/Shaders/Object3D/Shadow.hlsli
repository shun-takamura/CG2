// CSM + PCSS（Percentage-Closer Soft Shadows）
// 平行光源1個の影。接地点はくっきり・地面から離れるほど半影が広がる（距離で変化）。
// サンプルは Vogel ディスク（渦巻き状の均等分布）＋ピクセル毎回転で、
// 大きなボケでも格子状の縞ではなく滑らかなノイズになるようにしている。
// Object3d.PS / Object3dNoEnv.PS から include される。

#define SHADOW_CASCADE_COUNT 3
#define SHADOW_MAP_SIZE 2048.0f
#define BLOCKER_SAMPLES 24    // ブロッカー探索のサンプル数
#define PCF_SAMPLES 32        // 影フィルタのサンプル数
#define GOLDEN_ANGLE 2.39996323f
#define TWO_PI 6.2831853f

cbuffer ShadowBuffer : register(b5)
{
    float4x4 gCascadeViewProj[SHADOW_CASCADE_COUNT];
    float4   gCascadeSplitsView; // 各カスケード遠端のビュー空間Z（現状未使用）
    float    gShadowBias;
    float    gShadowNormalOffset; // 法線方向の受光点オフセット（ワールド単位）
    float    gShadowEnabled;      // 1=影あり, 0=影なし
    float    gShadowSoftness;     // 深度差→ボケ幅の係数（接地で硬く、離れるほど柔らかく＝距離で変化）
    float    gShadowDebug;        // 1=影係数をそのままグレースケール出力（デバッグ）
    float    gShadowMaxBlur;      // ボケ半径の上限（UV）。washout/サンプル不足対策の頭打ち
    float    gShadowFadeRadius;   // penumbra がこの値に達すると影が消える（距離フェード）。0で無効
    float    gShadowPad;
}
Texture2DArray<float> gShadowMap : register(t3);
SamplerComparisonState gShadowSampler : register(s1); // 深度比較（PCF）
SamplerState gShadowPointSampler : register(s2);      // 生深度読み（ブロッカー探索）

// 画面座標からサンプル回転角を作る（Interleaved Gradient Noise）。固定パターンをノイズ化。
float ShadowRotationAngle(float2 screenPos)
{
    float ign = frac(52.9829189f * frac(dot(screenPos, float2(0.06711056f, 0.00583715f))));
    return ign * TWO_PI;
}

// Vogel ディスクの i 番目のオフセット（単位円内・面積均等）。半径は 0..~1。
float2 VogelDiskSample(int i, int count, float rotAngle)
{
    float r = sqrt((float(i) + 0.5f) / float(count));
    float theta = float(i) * GOLDEN_ANGLE + rotAngle;
    float s, c;
    sincos(theta, s, c);
    return float2(c, s) * r;
}

// ブロッカー探索：受光点より手前(ライト側)にある遮蔽物の平均深度を返す。遮蔽物無しは -1。
float ShadowFindBlockerDepth(float2 uv, int cascade, float receiverDepth, float searchRadius, float rotAngle)
{
    float sum = 0.0f;
    int count = 0;
    [unroll] for (int i = 0; i < BLOCKER_SAMPLES; ++i)
    {
        float2 sampleUv = uv + VogelDiskSample(i, BLOCKER_SAMPLES, rotAngle) * searchRadius;
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

// 可変半径の「ガウス重み × 窓関数」PCF（Vogel ディスク）。
// 窓 (1-r²) でディスク外周の重みを0にし、端の不連続（輪郭線）を消す。
float ShadowPcf(float2 uv, int cascade, float receiverDepth, float filterRadius, float rotAngle)
{
    float sum = 0.0f;
    float weightSum = 0.0f;
    [unroll] for (int i = 0; i < PCF_SAMPLES; ++i)
    {
        float2 disk = VogelDiskSample(i, PCF_SAMPLES, rotAngle);
        float rNorm = length(disk); // 0..~1
        float window = saturate(1.0f - rNorm * rNorm);          // 端で0
        float gauss = exp(-rNorm * rNorm / (2.0f * 0.5f * 0.5f)); // σ=0.5
        float w = gauss * window;
        float2 sampleUv = uv + disk * filterRadius;
        sum += w * gShadowMap.SampleCmpLevelZero(gShadowSampler, float3(sampleUv, cascade), receiverDepth);
        weightSum += w;
    }
    return sum / weightSum;
}

// 指定カスケードでの影係数を返す。範囲外なら -1。
// edgeFade: そのカスケードのUV端への近さ（0=端, 1=中央寄り）。カスケード境界のブレンドに使う。
float ShadowSampleCascade(float3 offsetPos, int cascade, float rotAngle, out float edgeFade)
{
    edgeFade = 1.0f;

    float4 lightClip = mul(float4(offsetPos, 1.0f), gCascadeViewProj[cascade]);
    float3 proj = lightClip.xyz / lightClip.w;
    float2 uv = proj.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);
    if (uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f ||
        proj.z < 0.0f || proj.z > 1.0f)
        return -1.0f;

    // UV境界への近さ（端から band 以内をブレンド帯にする）
    float2 dist2 = min(uv, 1.0f - uv);
    float edge = min(dist2.x, dist2.y);
    const float band = 0.1f;
    edgeFade = saturate(edge / band);

    float current = proj.z - gShadowBias;

    // 距離で変化する PCSS。ボケ半径 = (受光点と遮蔽物の深度差) × Softness。
    // 接地点(深度差≒0)は minBlur で硬く、離れるほど深度差が増えて柔らかくなる。
    // MaxBlur はサンプル数の都合の頭打ち（Vogel化で大きめでも崩れにくい）。
    float minBlur = 1.5f / SHADOW_MAP_SIZE;
    float maxBlur = max(gShadowMaxBlur, minBlur);
    float searchRadius = maxBlur; // ブロッカー探索は上限範囲で

    float blocker = ShadowFindBlockerDepth(uv, cascade, current, searchRadius, rotAngle);
    if (blocker < 0.0f)
        return 1.0f; // 遮蔽物なし＝照らされる

    float penumbra = (current - blocker) * gShadowSoftness;
    float filterRadius = clamp(penumbra, minBlur, maxBlur);
    float shadow = ShadowPcf(uv, cascade, current, filterRadius, rotAngle);

    // 距離フェード：penumbra（＝深度差×Softness）が大きいほど影を薄く（lit側へ）。
    // 物体が地面から離れるほど影が薄くなり、FadeRadius で完全に消える。
    // ※ MaxBlur で filterRadius は頭打ちでも penumbra は伸び続けるので、頭打ち後も薄くなる。
    if (gShadowFadeRadius > 0.0f)
    {
        float fade = saturate(penumbra / gShadowFadeRadius);
        shadow = lerp(shadow, 1.0f, fade);
    }
    return shadow;
}

// ワールド座標が平行光源から見て影かを返す（1=照らされる, 0=影）。PCSS＋カスケード境界ブレンド。
float CalcShadowFactor(float3 worldPos, float3 normal, float2 screenPos)
{
    if (gShadowEnabled < 0.5f)
        return 1.0f;

    // 受光点を法線方向にずらしてセルフシャドウ（アクネ）を抑える
    float3 offsetPos = worldPos + normal * gShadowNormalOffset;

    // ピクセルごとにサンプルを回転させ、固定パターンをディザ化
    float rotAngle = ShadowRotationAngle(screenPos);

    // 近いカスケード（高解像度）から順に、範囲内に収まる最初のものを採用
    [unroll] for (int i = 0; i < SHADOW_CASCADE_COUNT; ++i)
    {
        float edgeFade;
        float s = ShadowSampleCascade(offsetPos, i, rotAngle, edgeFade);
        if (s < 0.0f)
            continue; // このカスケードには入っていない

        // 端に近く、次のカスケードがあるなら次とブレンドして継ぎ目を消す
        if (edgeFade < 1.0f && (i + 1) < SHADOW_CASCADE_COUNT)
        {
            float nextFade;
            float sNext = ShadowSampleCascade(offsetPos, i + 1, rotAngle, nextFade);
            if (sNext >= 0.0f)
                s = lerp(sNext, s, edgeFade);
        }
        return s;
    }
    return 1.0f; // どのカスケードにも入らない＝影なし
}
