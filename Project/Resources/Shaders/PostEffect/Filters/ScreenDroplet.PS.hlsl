// ScreenDroplet.PS.hlsl
// 画面（レンズ）に付いた水滴（ScreenDropletEffect。13_RiverWater.md §9）。
// 水滴は状態を持たず、格子に分けてマスごとの乱数で位置・大きさ・現れる時刻を決める。
//   - 止まっている水滴（小さい物）：2 段の格子。周期ごとに位置・大きさ・出るかを引き直して現れ、大きさはそのままで薄れて消える。
//     マスのどこにでも置き、周りのマスも調べる（中心に寄せると格子に並んで見える）。付く範囲の内側ほど小さく、外側ほど大きい
//   - 垂れる水滴（大きい物）：列ごとに 1 粒。その場でつながりながら膨らみ、流れる向きへ加速して画面の外へ抜けて消える
//     （乾いて消えない）。通り道の止まっている水滴を消す（吸い込んで合体したように見える）。
//     通った跡には、ところどころ途切れる薄い筋と、取り残された小さな粒が残り、古い方から薄れる
//   - つながり：水滴ごとの「広がり」を足し合わせて境目を決める（メタボール）。近い水滴同士は橋でつながって 1 つになる
//
// 流れの向きは 2 通り（gRadial）：
//   - 重力（0）：水面すれすれ。画面の下の方に付き、大きい物が下へ垂れる。流れの座標 = (x, y)
//   - 放射（1）：雲の縦穴。画面の周り（ビネットと同じ測り方）に付き、すべての水滴が外へ流れる。
//     流れの座標 = (角度, 中心からの距離)。止まっている水滴の格子は対数極座標（マスが外ほど大きい正方形）で、
//     格子ごと外へ流す（gFlowSpeed）ので、水滴はマスをまたいでも切れずに流れていく。
//     さらに大きい水滴ほど速く滑り（gSizeSpeed）、途中の水滴とつながって細い筋を引く（全部が同じ速さだと泡に見える）
// 水滴の形・レンズ・光は画面の座標で作る（流れの座標でゆがまない）。
// 濡れ具合（gWetness）が上がるほど、数・大きさ・垂れる割合が増え、付く範囲が広がる。
// 水滴の中はレンズとして、丸みの分だけずらした位置の景色（逆さに縮んだ像）を映し、ふちを暗く、上側を光らせる。
// 画面の座標は画面の高さを 1 とする単位（横は縦横比を掛ける）。テクスチャの y は下向き。

#include "../Common/PostProcess.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

Texture2D<float4> gTexture : register(t0);
SamplerState gSampler : register(s0);

// C++ の ScreenDropletEffect::ParamsCB と 1:1
cbuffer ScreenDropletParams : register(b0)
{
    float gTime;            // [s]
    float gWetness;         // 0..1
    float gDensity;         // 止まっている水滴の格子の細かさ（重力：画面の高さあたりのマス数／放射：1 周のマス数 ÷ 2π）
    float gSizeMin;         // 水滴の半径の下限（マスに対する割合）
    float gSizeMax;         // 上限
    float gDryScale;        // 濡れ具合 0 の時の大きさの倍率
    float gLifetime;        // 止まっている水滴が現れてから消えるまでの平均 [s]
    float gRefraction;      // レンズのずらしの強さ
    float gDripChance;      // 濡れ具合 1 の時に垂れる列の割合
    float gDripSpeed;       // 流れ始めの速さ [画面の高さ/s]（放射は中心からの距離の単位）
    float gDripColumns;     // 垂れる列の数（重力：画面の幅あたり／放射：1 周で × 2.5）
    float gTrailLength;     // 跡が薄れきるまでの長さ
    float gCoverageTopDry;  // 濡れ具合 0 の時、水滴が付き始める所（重力：画面の高さ 0=上端／放射：中心からの距離 0=中心・辺 0.71・角 1）
    float gCoverageTopWet;  // 濡れ具合 1 の時
    float gCoverageFade;    // 付き始めから濃くなる幅
    float gHighlight;       // 上側の光の強さ
    float gRimDarkness;     // ふちの暗さ
    float gDripRadius;      // 垂れる水滴の膨らみきった半径 [画面の高さ]
    float gDripAccel;       // 流れる速さの伸び（合体して重くなる分）[1/s]
    float gDripStepRate;    // 流れながらためらう回数 [回/s]
    float gDripGrow;        // 画面の外まで流れた時の大きさの伸び（1 で 2 倍）
    float gTopSizeScale;    // 付く範囲の内側の端での大きさの倍率（外側の端で 1）
    float gDripGrowTime;    // 垂れる水滴がその場で膨らむ時間 [s]
    float gTrailBeadSpacing;// 跡に残る粒の間隔
    float gSizeWetness;     // 大きさに使う濡れ具合（乾く時はゆっくり下がる＝見えている水滴が縮まない）
    float gRadial;          // 0 = 重力、1 = 放射
    float gFlowSpeed;       // 放射：止まっている水滴の格子が外へ流れる速さ（中心からの距離が e 倍になる回数 [1/s]）
    float gSizeSpeed;       // 大きい水滴が寿命の間に余分に滑る量 [マス]（大きさの 2 乗に比例・加速する。2 マス未満）
};

static const float kTwoPi = 6.2831853f;
// 1 粒の広がりの届く範囲（半径の倍率）。広がりを足し合わせ、kEdge を越えた所を水滴にする。
// 1 粒だけなら kEdge の所がちょうど半径（d = 1）になる
static const float kReach = 1.4f;
static const float kEdge = 0.1176f; // (1 - 1 / kReach²)³
// 流れながらためらう強さ（1 未満なら止まらない＝なめらか）
static const float kHesitation = 0.35f;
// 垂れる水滴が消える所（流れの座標の v。重力は画面の下端の少し先、放射は角の少し先）
static const float kDripEnd = 1.2f;

// main で決める画面の形
static float gAspect;   // 縦横比
static float2 gCenter;  // 画面の中心
static float gRadius;   // 中心から角までの長さ（放射の中心からの距離 1 の長さ）

float3 Hash32(float2 p)
{
    float3 q = frac(float3(p.xyx) * float3(0.1031f, 0.1030f, 0.0973f));
    q += dot(q, q.yxz + 33.33f);
    return frac((q.xxy + q.yxx) * q.zyx);
}

bool IsRadial()
{
    return gRadial > 0.5f;
}

// ---------------------------------------------------------------------------
// 流れの座標（u = 流れに直交、v = 流れる向き）
//   重力：(x, y)。放射：(画面を正方形に直した中での角度, 中心からの距離)。距離はビネットと同じ測り方
//   （0=中心、辺の中央 0.71、角 1）なので、付く範囲の値と垂れる水滴の v がそろう
// ---------------------------------------------------------------------------

static const float kSqrtHalf = 0.70710678f;

float2 ToFlow(float2 p)
{
    if (!IsRadial())
    {
        return p;
    }
    const float2 n = (p - gCenter) / gCenter;
    return float2(atan2(n.y, n.x), length(n) * kSqrtHalf);
}

float2 FlowToScreen(float2 f)
{
    if (!IsRadial())
    {
        return f;
    }
    return gCenter + gCenter * (f.y / kSqrtHalf) * float2(cos(f.x), sin(f.x));
}

// u が 1 増えた時の画面上の長さ（放射は楕円の周の長さ）
float FlowAcrossScale(float2 f)
{
    if (!IsRadial())
    {
        return 1.0f;
    }
    return length(gCenter * float2(-sin(f.x), cos(f.x))) * max(f.y, 1e-3f) / kSqrtHalf;
}

// 流れる向き（画面）。水滴の重い側（ふくらむ側）
float2 FlowDown(float2 p)
{
    if (!IsRadial())
    {
        return float2(0.0f, 1.0f);
    }
    const float2 g = (p - gCenter) / (gCenter * gCenter);
    return g / max(length(g), 1e-6f);
}

// 水滴が付く度合いを測る値：重力は画面の高さ（0=上端）、放射は流れの座標の v（中心からの距離）
float CoverageMetric(float2 p)
{
    return IsRadial() ? ToFlow(p).y : p.y;
}

// ---------------------------------------------------------------------------
// 止まっている水滴の格子（重力：正方形のマス／放射：対数極座標のマスを外へ流す）
// ---------------------------------------------------------------------------

float AngularCells(float density)
{
    return max(round(kTwoPi * density), 3.0f);
}

float2 ToGrid(float2 p, float density)
{
    if (!IsRadial())
    {
        return p * density;
    }
    const float k = AngularCells(density) / kTwoPi;
    const float2 d = p - gCenter;
    const float rho = max(length(d) / gRadius, 1e-3f);
    return float2(atan2(d.y, d.x) * k, (log(rho) - gFlowSpeed * gTime) * k);
}

float2 GridToScreen(float2 g, float density)
{
    if (!IsRadial())
    {
        return g / density;
    }
    const float k = AngularCells(density) / kTwoPi;
    const float theta = g.x / k;
    const float rho = exp(g.y / k + gFlowSpeed * gTime);
    return gCenter + gRadius * rho * float2(cos(theta), sin(theta));
}

// マス 1 つの画面上の大きさ（放射は外ほど大きい）
float GridCellSize(float2 g, float density)
{
    if (!IsRadial())
    {
        return 1.0f / density;
    }
    const float k = AngularCells(density) / kTwoPi;
    return gRadius * exp(g.y / k + gFlowSpeed * gTime) / k;
}

// 乱数を引くためのマスの番号（放射は 1 周で同じマスに戻す）
float2 GridHashId(float2 id, float density)
{
    if (!IsRadial())
    {
        return id;
    }
    const float n = AngularCells(density);
    return float2(id.x - n * floor((id.x + n * 0.5f) / n), id.y);
}

// ---------------------------------------------------------------------------
// 水滴を足し合わせる（メタボール）
// ---------------------------------------------------------------------------

struct DropSum
{
    float field;    // 広がりの合計
    float opacity;  // 不透明度（広がりで重み付けした合計）。つながった水滴は形を共有し、濃さは混ざる
    float2 offset;  // レンズのずらし（広がりで重み付けした合計）
    float light;    // 光（一番強い物）
    float trail;    // 筋の覆い（足し合わせずに一番強い物）
    float2 trailOffset;
};

DropSum EmptySum()
{
    DropSum s;
    s.field = 0.0f;
    s.opacity = 0.0f;
    s.offset = float2(0.0f, 0.0f);
    s.light = 0.0f;
    s.trail = 0.0f;
    s.trailOffset = float2(0.0f, 0.0f);
    return s;
}

// 水滴 1 粒を足す。local = 水滴の中心からの位置、radius = 半径（どちらも画面の高さの単位）、shape = 形の乱数 0..1、
// opacity = 不透明度（現れる・消える時は大きさを変えずにこれで薄くする）、down = 流れる向き（重い側がふくらむ）。
// 形は乱数で向き・縦横の伸び・ふちのでこぼこを変える（真円だと作り物に見える）
void AddDrop(inout DropSum sum, float2 local, float radius, float3 shape, float opacity, float2 down)
{
    const float2 q = local / max(radius, 1e-5f);
    // 重さで流れる側が少しふくらむ
    const float2 across = float2(-down.y, down.x);
    float along = dot(q, down);
    along *= (along > 0.0f) ? 0.85f : 1.1f;
    const float2 qs = across * dot(q, across) + down * along;
    // 形を測る座標：向きを回して縦横に伸ばす
    const float angle = shape.x * kTwoPi;
    const float c = cos(angle);
    const float sn = sin(angle);
    float2 k = float2(c * qs.x - sn * qs.y, sn * qs.x + c * qs.y);
    const float stretch = lerp(0.8f, 1.25f, shape.y);
    k *= float2(stretch, 1.0f / stretch);
    // ふちのでこぼこ
    const float around = atan2(k.y, k.x);
    const float wobble = 1.0f + 0.1f * sin(3.0f * around + shape.z * kTwoPi) + 0.06f * sin(5.0f * around + shape.x * 12.0f);
    const float d = length(k) / wobble / kReach;
    if (d >= 1.0f)
    {
        return;
    }
    float w = 1.0f - d * d;
    w = w * w * w;
    sum.field += w;
    sum.opacity += w * opacity;
    // 丸みの分だけ外の景色を逆向きに引く（小さな逆さの像）
    sum.offset += -qs * gRefraction * radius * w;
    // 光は画面の向きのまま左上。小さな水滴は光だけが目立って白い点に見えるので弱める
    const float light = smoothstep(0.45f, 0.0f, length(q - float2(-0.35f, -0.4f))) * smoothstep(0.004f, 0.012f, radius);
    sum.light = max(sum.light, light * saturate(w / kEdge) * opacity);
}

// 細い筋（線分 a→b、太さ width）を足す。筋は足し合わせない（水滴の広がりを太らせない）。b が新しい側
void AddStreak(inout DropSum sum, float2 p, float2 a, float2 b, float width, float strength)
{
    const float2 ab = b - a;
    const float t = saturate(dot(p - a, ab) / max(dot(ab, ab), 1e-8f));
    const float2 diff = p - (a + ab * t);
    // 古い方ほど細く薄い
    const float w = width * lerp(0.3f, 1.0f, t);
    const float cover = smoothstep(w, w * 0.3f, length(diff)) * lerp(0.2f, 1.0f, t) * strength * 0.55f;
    if (cover > sum.trail)
    {
        sum.trail = cover;
        sum.trailOffset = -diff / max(w, 1e-5f) * gRefraction * w;
    }
}

// 濡れているほど付く範囲が広がり（重力は上へ、放射は内へ）、数も増える
float CoverageTop()
{
    return lerp(gCoverageTopDry, gCoverageTopWet, gWetness);
}

float Presence(float metric)
{
    const float top = CoverageTop();
    const float mask = smoothstep(top, top + max(gCoverageFade, 1e-3f), metric);
    return mask * smoothstep(0.0f, 0.3f, gWetness) * lerp(0.25f, 0.7f, gWetness);
}

// ---------------------------------------------------------------------------
// 垂れる水滴（流れの座標で動かす）
// ---------------------------------------------------------------------------

struct Drip
{
    bool active;
    float startV;   // 膨らんだ所（流れ始めの v）
    float headV;    // 今の先頭の v
    float radius;   // 今の大きさ
    float u0;       // 膨らんだ所の u
    float width;    // 列の幅（u）
    float seed;     // この 1 粒の乱数（揺れ方・跡の粒）
};

float DripColumnWidth()
{
    return IsRadial() ? kTwoPi / max(round(gDripColumns * 2.5f), 3.0f) : gAspect / max(gDripColumns, 1.0f);
}

// 流れながらの u。ゆるく小さく横に揺れる（膨らんだ所ではまっすぐ）
float DripU(Drip drip, float v)
{
    const float dv = max(v - drip.startV, 0.0f);
    const float ease = saturate(dv * 8.0f);
    return drip.u0 + (sin(dv * 7.0f + drip.seed * kTwoPi) * 0.06f
                    + sin(dv * 15.0f + drip.seed * 17.0f) * 0.025f) * drip.width * ease;
}

float2 DripScreen(Drip drip, float v, float uOffset)
{
    return FlowToScreen(float2(DripU(drip, v) + uOffset, v));
}

// 列 column の今の垂れる水滴。その場で膨らむ → 流れ始めて加速 → 画面の外へ抜けて消える → 間を空けて次の 1 粒
Drip GetDrip(float column)
{
    Drip drip;
    drip.active = false;
    drip.width = DripColumnWidth();
    drip.u0 = 0.0f;
    drip.startV = 0.0f;
    drip.headV = 0.0f;
    drip.radius = 0.0f;
    drip.seed = 0.0f;

    const float3 h = Hash32(float2(column, 91.7f));
    const float chance = gDripChance * smoothstep(0.2f, 0.8f, gWetness);
    if (h.x >= chance)
    {
        return drip;
    }

    const float speed = max(gDripSpeed, 0.01f) * lerp(0.7f, 1.3f, h.z);
    const float growTime = max(gDripGrowTime, 0.01f) * lerp(0.7f, 1.3f, h.y);
    const float accel = max(gDripAccel, 0.0f);
    // 一番内側から膨らんだ時に抜けきるまでの時間で 1 周期を決める（周期の長さが粒ごとに変わると時刻が飛ぶため）
    const float firstV = gCoverageTopWet + gCoverageFade;
    const float maxTravel = kDripEnd - firstV;
    const float maxSlideTime = (accel > 1e-4f)
        ? (-1.0f + sqrt(1.0f + 4.0f * accel * maxTravel / speed)) / (2.0f * accel) / (1.0f - kHesitation)
        : maxTravel / speed / (1.0f - kHesitation);
    const float cycle = growTime + maxSlideTime + lerp(0.3f, 1.5f, h.y);

    const float cycles = gTime / cycle + h.z * 3.1f;
    const float t = frac(cycles) * cycle;
    // 1 粒ごとに膨らむ場所を引き直す（列の中のどこか・付く範囲のどこか）
    const float3 n = Hash32(float2(column * 1.37f, floor(cycles) * 7.31f + 11.0f));
    drip.seed = n.z;
    drip.u0 = (column + 0.2f + 0.6f * n.x) * drip.width;
    drip.startV = lerp(firstV, 0.92f, n.y);
    const float radius = gDripRadius * lerp(0.8f, 1.2f, h.z);

    if (t < growTime)
    {
        // その場で膨らむ（まわりの水滴を集めてつながっていく）
        drip.active = true;
        drip.headV = drip.startV;
        drip.radius = radius * lerp(0.35f, 1.0f, smoothstep(0.0f, 1.0f, t / growTime));
        return drip;
    }

    // 流れる：加速しながら、少しためらう（止まりはしないのでなめらか）
    const float ts = t - growTime;
    const float rate = max(gDripStepRate, 0.01f) * kTwoPi;
    const float distance = speed * (ts + accel * ts * ts - kHesitation * sin(rate * ts) / rate);
    const float travel = kDripEnd - drip.startV;
    if (distance > travel)
    {
        return drip; // 画面の外へ抜けた。次の 1 粒までの間
    }
    drip.active = true;
    drip.headV = drip.startV + distance;
    // 進むほど大きくなる（吸い込んだ分）
    drip.radius = radius * (1.0f + gDripGrow * distance / max(travel, 1e-3f));
    return drip;
}

// 止まっている水滴が、垂れる水滴の通り道にある（吸い込まれた）か
bool EatenByDrip(float2 center)
{
    const float2 f = ToFlow(center);
    const Drip drip = GetDrip(floor(f.x / DripColumnWidth()));
    if (!drip.active || f.y < drip.startV - drip.radius || f.y > drip.headV + drip.radius)
    {
        return false;
    }
    return abs(f.x - DripU(drip, f.y)) * FlowAcrossScale(f) < drip.radius * 1.5f;
}

void AddDripLayer(inout DropSum sum, float2 p)
{
    const float2 f = ToFlow(p);
    const float column = floor(f.x / DripColumnWidth());
    const Drip drip = GetDrip(column);
    if (!drip.active)
    {
        return;
    }
    const float2 down = FlowDown(p);

    // 先頭の粒（止まっている水滴とつながる）
    AddDrop(sum, p - DripScreen(drip, drip.headV, 0.0f), drip.radius, Hash32(float2(column, 37.1f + drip.seed)), 1.0f, down);

    // 通った跡（膨らんだ所から先頭の少し手前まで）。古い方＝先頭から遠いほど薄れる
    const float behind = drip.headV - f.y;
    if (f.y < drip.startV || behind < drip.radius * 0.5f)
    {
        return;
    }
    const float fade = 1.0f - saturate(behind / max(gTrailLength, 1e-3f));
    if (fade <= 0.0f)
    {
        return;
    }

    // 薄い濡れた筋。ところどころ途切れる（水が切れて乾いた所）
    {
        const float segment = max(drip.radius * 1.2f, 1e-3f);
        const float3 g = Hash32(float2(column * 3.1f + drip.seed * 11.0f, floor((f.y - drip.startV) / segment)));
        const float keep = smoothstep(0.25f, 0.45f, g.x);
        const float trailWidth = drip.radius * 0.22f;
        const float dx = (f.x - DripU(drip, f.y)) * FlowAcrossScale(f);
        const float cover = smoothstep(trailWidth, trailWidth * 0.3f, abs(dx)) * fade * keep * 0.55f;
        if (cover > sum.trail)
        {
            sum.trail = cover;
            sum.trailOffset = float2(-down.y, down.x) * (-dx / max(trailWidth, 1e-5f) * gRefraction * trailWidth);
        }
    }

    // 取り残された小さな粒。画面上の位置に留まり（先頭と一緒に動かない）、古い方から薄れる
    const float spacing = max(gTrailBeadSpacing, 1e-3f);
    const float index = floor((f.y - drip.startV) / spacing);
    [unroll]
    for (int j = -1; j <= 1; ++j)
    {
        const float i = index + j;
        const float3 b = Hash32(float2(column * 5.7f + drip.seed * 23.0f, i * 1.13f));
        if (b.x < 0.45f)
        {
            continue; // 粒が残らなかった所
        }
        const float bv = drip.startV + (i + b.y) * spacing;
        const float beadBehind = drip.headV - bv;
        if (bv < drip.startV || beadBehind < drip.radius * 1.2f)
        {
            continue;
        }
        const float beadFade = 1.0f - saturate(beadBehind / max(gTrailLength, 1e-3f));
        if (beadFade <= 0.0f)
        {
            continue;
        }
        const float radius = drip.radius * lerp(0.15f, 0.35f, b.z);
        const float uOffset = (b.z - 0.5f) * drip.radius * 0.4f / FlowAcrossScale(float2(DripU(drip, bv), bv));
        AddDrop(sum, p - DripScreen(drip, bv, uOffset), radius, b, beadFade, down);
    }
}

// ---------------------------------------------------------------------------
// 止まっている水滴（小さい物。大きさはそのままで薄れて消える。放射では格子ごと外へ流れる）
// ---------------------------------------------------------------------------

// 1 段。density = 格子の細かさ
void AddStaticLayer(inout DropSum sum, float2 p, float density, float sizeScale, float salt)
{
    const float2 g = ToGrid(p, density);
    // 外（下）のマスほど付きやすいので、1 マス半外まで水滴が付かなければ調べない
    // （放射の付く度合いは画面の高さ 1 あたり最大 1.42 変わる＝縦方向で 1 / (0.5 × √2)）
    const float margin = 1.5f * GridCellSize(g, density) * (IsRadial() ? 1.42f : 1.0f);
    if (Presence(CoverageMetric(p) + margin) <= 0.0f)
    {
        return;
    }

    const float top = CoverageTop();
    const float2 base = floor(g);
    // 大きい水滴は余分に滑るので、流れの上流（内側・上）は 2 マスまで調べる
    const float slideCells = clamp(gSizeSpeed, 0.0f, 1.8f);
    [unroll]
    for (int y = -2; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            const float2 id = base + float2(x, y);
            const float2 hashId = GridHashId(id, density);
            const float3 h = Hash32(hashId + salt);
            // 周期（マスごとに長さと始まりをずらす）。現れるたびに位置・大きさ・形・出るかを引き直す
            const float period = max(gLifetime, 0.1f) * lerp(0.6f, 1.4f, h.z);
            const float cycles = gTime / period + h.x * 7.13f;
            const float t = frac(cycles);
            const float2 key = hashId + salt + floor(cycles) * 13.17f;
            const float3 r = Hash32(key);
            const float3 shape = Hash32(key + 5.3f);
            // マスのどこにでも置く（隣のマスも調べるのではみ出しても切れない）
            const float2 origin = id + 0.1f + r.xy * 0.8f;
            // 大きい水滴ほど速く滑る（加速）。小さい水滴は格子の流れのまま
            const float slide = slideCells * r.z * r.z * t * t;
            const float2 cellPosition = origin + float2(0.0f, slide);
            const float2 center = GridToScreen(cellPosition, density);
            // 付く割合は水滴の中心で決める（水滴の途中で欠けないように）
            const float metric = CoverageMetric(center);
            const float exist = saturate((Presence(metric) - shape.y) * 8.0f);
            if (exist <= 0.0f)
            {
                continue;
            }
            // 付く範囲の内側ほど小さく（小さい方へ偏る）、外側ほど大きく（大きい方へ偏る）
            const float depth = saturate((metric - top) / max(1.0f - top, 1e-3f));
            const float sizeRandom = pow(r.z, lerp(2.0f, 0.6f, depth));
            const float sizeAtDepth = lerp(gTopSizeScale, 1.0f, depth);
            // すぐ現れ、少し留まってからだんだん薄れて消える（縮めると違和感があるので大きさは変えない）。
            // 濡れ具合が下がって付く割合を割った所も薄れて消える
            const float life = smoothstep(0.0f, 0.05f, t) * (1.0f - smoothstep(0.35f, 1.0f, t));
            const float opacity = life * exist;
            const float radius = lerp(gSizeMin, gSizeMax, sizeRandom) * GridCellSize(cellPosition, density) * sizeScale * sizeAtDepth;
            if (opacity <= 1e-3f || EatenByDrip(center))
            {
                continue;
            }
            AddDrop(sum, p - center, radius, shape, opacity, FlowDown(center));
            // 滑った水滴は通った所に細い筋を引く（途中の水滴とつながって垂れていくように見える）
            if (slide > 0.2f)
            {
                AddStreak(sum, p, GridToScreen(origin, density), center, radius * 0.3f, opacity);
            }
        }
    }
}

PixelShaderOutput main(VertexShaderOutput input)
{
    PixelShaderOutput output;
    const float2 uv = input.texcoord;
    const float4 scene = gTexture.Sample(gSampler, uv);

    uint width;
    uint height;
    gTexture.GetDimensions(width, height);
    gAspect = float(width) / max(float(height), 1.0f);
    gCenter = float2(gAspect * 0.5f, 0.5f);
    gRadius = length(gCenter);
    const float2 p = float2(uv.x * gAspect, uv.y);

    // 大小 2 段と垂れる水滴を同じ広がりに足し合わせる（互いにつながる）
    const float sizeScale = lerp(gDryScale, 1.0f, gSizeWetness);
    DropSum sum = EmptySum();
    AddStaticLayer(sum, p, gDensity, sizeScale, 0.0f);
    AddStaticLayer(sum, p, gDensity * 1.7f, sizeScale * 0.85f, 17.0f);
    AddDripLayer(sum, p);

    // 広がりが kEdge を越えた所が水滴。筋とは覆いの強い方を使う
    float cover = smoothstep(kEdge * 0.75f, kEdge * 1.6f, sum.field) * sum.opacity / max(sum.field, 1e-5f);
    float2 offset = sum.offset / max(sum.field, 1e-5f);
    float light = sum.light;
    if (sum.trail > cover)
    {
        cover = sum.trail;
        offset = sum.trailOffset;
        light = 0.0f;
    }
    if (cover <= 0.0f)
    {
        output.color = scene;
        return output;
    }

    const float2 lensUv = saturate(uv + float2(offset.x / gAspect, offset.y));
    float3 lens = gTexture.Sample(gSampler, lensUv).rgb;
    // ふち（覆いの変わり目）を暗く
    lens *= 1.0f - gRimDarkness * saturate(cover * (1.0f - cover) * 4.0f);
    float3 color = lerp(scene.rgb, lens, cover);
    color += light * gHighlight * cover;

    output.color = float4(color, scene.a);
    return output;
}
