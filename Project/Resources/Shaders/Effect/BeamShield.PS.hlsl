// BeamShield.PS.hlsl
// ビームシールド本体（BeamShieldRenderer.cpp）。原点＝発生装置のローカルの XY で、
//   形   ：縦長の六角形（上下がとがる）の距離。左右対称なので右半分の 3 辺だけで測る
//   縁   ：発生装置から見た角度のノイズ（角度で一周するように周期を合わせ、時間方向へ滑らかに変える）で出し入れする
//          ＝大まかな形は変わらず、縁だけ細かく変わる
//   集中線：同じく角度のノイズを鋭くした放射状の筋。ゆっくり入れ替わる
//   透け ：発生装置の近く・それを囲む小さな六角形・縁の帯は不透明に近く、その間は透ける
// 出力は乗算済み α（本体）／発光 RT へは光る分だけ（bloomPass）。

cbuffer BeamShieldParams : register(b0)
{
    float gHalfWidth;
    float gHalfHeight;
    float gTopY;
    float gShoulderY;

    float gWaistX;
    float gWaistY;
    float gBottomY;
    float gTime;

    float gEdgeNoiseAmp;
    float gEdgeNoiseFreq;
    float gEdgeNoiseSpeed;
    float gStreakCount;

    float gStreakSharpness;
    float gStreakSpeed;
    float gStreakIntensity;
    float gCoreRadius;

    float gCoreAlpha;
    float gEdgeWidth;
    float gEdgeAlpha;
    float gMidAlpha;

    float3 gColor;
    float gIntensity;

    float3 gEdgeColor;
    float gFlash;

    float gFlashEdgeNoise;
    float gBloomScale;
    int gBloomPass;
    float gFlashIntensity;

    float gInnerScale;
    float gInnerAlpha;
    float2 gPadding0;
};

struct PixelShaderInput
{
    float4 position : SV_POSITION;
    float2 local : TEXCOORD0;
};

static const float kPi = 3.14159265f;

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// x 方向に period で一周する 2D の値ノイズ（角度の継ぎ目を出さない）
float PeriodicNoise(float x, float y, float period)
{
    float2 i = floor(float2(x, y));
    float2 f = frac(float2(x, y));
    float2 u = f * f * (3.0f - 2.0f * f);
    float x0 = fmod(i.x, period);
    float x1 = fmod(i.x + 1.0f, period);
    float a = Hash21(float2(x0, i.y));
    float b = Hash21(float2(x1, i.y));
    float c = Hash21(float2(x0, i.y + 1.0f));
    float d = Hash21(float2(x1, i.y + 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// 辺 a→b（右回り）から外へ測った距離
float EdgeDistance(float2 p, float2 a, float2 b)
{
    float2 dir = b - a;
    float2 n = normalize(float2(-dir.y, dir.x));
    return dot(p - a, n);
}

// 六角形の距離（内側が負）。scale で発生装置を中心に縮める
float ShapeDistance(float2 p, float scale)
{
    p /= scale;
    p.x = abs(p.x);
    float2 top = float2(0.0f, gTopY * gHalfHeight);
    float2 shoulder = float2(gHalfWidth, gShoulderY * gHalfHeight);
    float2 waist = float2(gWaistX * gHalfWidth, gWaistY * gHalfHeight);
    float2 bottom = float2(0.0f, gBottomY * gHalfHeight);
    float d = EdgeDistance(p, top, shoulder);
    d = max(d, EdgeDistance(p, shoulder, waist));
    d = max(d, EdgeDistance(p, waist, bottom));
    return d * scale;
}

float4 main(PixelShaderInput input) : SV_TARGET
{
    float2 p = input.local;
    float r = length(p);
    // 発生装置から見た角度（0..1 で一周）
    float angle01 = atan2(p.y, p.x) / (2.0f * kPi) + 0.5f;

    // ----- 縁：角度のノイズで出し入れ（2 オクターブ。被弾の瞬間は大きく揺らす） -----
    float edgePeriod = max(1.0f, round(gEdgeNoiseFreq));
    float edgeT = gTime * gEdgeNoiseSpeed;
    float edgeNoise = PeriodicNoise(angle01 * edgePeriod, edgeT, edgePeriod) * 0.65f
                    + PeriodicNoise(angle01 * edgePeriod * 3.0f, edgeT * 1.7f + 17.0f, edgePeriod * 3.0f) * 0.35f;
    float edgeAmp = gEdgeNoiseAmp * (1.0f + gFlash * gFlashEdgeNoise);
    float d = ShapeDistance(p, 1.0f) - (edgeNoise - 0.5f) * 2.0f * edgeAmp;
    // 発生装置を囲む小さな六角形（同じ形を縮めた物。縁は同じノイズで少しだけ揺らす）
    float dInner = ShapeDistance(p, gInnerScale) - (edgeNoise - 0.5f) * 2.0f * gEdgeNoiseAmp * gInnerScale;

    float aa = max(fwidth(d), 1e-5f);
    float inside = 1.0f - smoothstep(-aa, aa, d);
    if (inside <= 0.0f) discard;

    // 発生装置（0）から縁（1）までのどこにいるか
    float t = saturate(r / max(r - d, 1e-4f));

    // ----- 集中線：角度のノイズを鋭くした放射状の筋。根元の集まる所と縁では弱める -----
    float streakPeriod = max(1.0f, round(gStreakCount));
    float streakT = gTime * gStreakSpeed;
    // 細かい筋と、まばらな太い筋の 2 層
    float streakPeriod2 = max(1.0f, round(gStreakCount * 0.37f));
    float streak = max(pow(PeriodicNoise(angle01 * streakPeriod, streakT, streakPeriod), gStreakSharpness),
        0.7f * pow(PeriodicNoise(angle01 * streakPeriod2, streakT * 0.6f + 31.0f, streakPeriod2), gStreakSharpness * 2.0f));
    streak = streak * smoothstep(0.08f, 0.35f, t) * (1.0f - smoothstep(0.85f, 1.0f, t));

    // ----- 透け：芯と縁の帯は不透明に近く、間は透ける -----
    float core = exp(-(r * r) / max(gCoreRadius * gCoreRadius, 1e-5f));
    float rim = 1.0f - smoothstep(0.0f, max(gEdgeWidth, 1e-4f), -d);
    rim = max(rim, gInnerAlpha * (1.0f - smoothstep(0.0f, max(gEdgeWidth * 0.6f, 1e-4f), abs(dInner))));
    float alpha = max(max(core * gCoreAlpha, rim * gEdgeAlpha), gMidAlpha + streak * gStreakIntensity * 0.5f);
    alpha = saturate(alpha) * inside;

    float hot = saturate(max(core, rim));
    float3 color = lerp(gColor, gEdgeColor, hot);
    float glow = (alpha + streak * gStreakIntensity) * gIntensity * (1.0f + gFlash * gFlashIntensity);
    float3 emissive = color * glow * inside;

    if (gBloomPass != 0)
    {
        // 光る分だけ（芯・縁・筋が強く、透ける所は少し）
        return float4(emissive * (0.25f + 0.75f * max(hot, streak)) * gBloomScale, 0.0f);
    }
    return float4(emissive, alpha);
}
