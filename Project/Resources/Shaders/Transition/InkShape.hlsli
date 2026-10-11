// 水に広がる墨の形と濃淡（墨のトランジション・羽根の着水の「黒が引く」で使う）。
// ノイズは cloud_noise.dds（R = 形の fBm / G = 低周波の fBm、上下左右がつながる）を t2 / s1（ラップ・線形）で読む。
// 形は ゆがみ（渦）→ 筋（指のように伸びる所）→ 縁のちぎれ の順に作る。ノイズは画面に固定
// （墨が広がりながら水の流れの中へ入っていく見え方）。長さは「画面の幅 = 1」。

#ifndef INK_SHAPE_HLSLI
#define INK_SHAPE_HLSLI

Texture2D<float2> gNoise : register(t2);
SamplerState gWrapSampler : register(s1);

struct InkShapeParams
{
    float radius;         // 墨の半径。負なら墨なし
    float edgeSoftness;   // 縁のぼかし幅
    float warpAmount;     // ゆがみの量（半径に対する割合）
    float warpScale;      // ゆがみの細かさ
    float tendrilAmount;  // 筋の長さ（半径に対する割合）
    float tendrilFreq;    // 筋の本数の目安
    float detailScale;    // 縁のちぎれの細かさ
    float wispAmount;     // 縁のちぎれの強さ
    float edgeWidth;      // 濃い芯から縁までの幅（半径に対する割合）
    float flow;           // 経過秒 × 流れる速さ（ノイズをずらす量）
};

struct InkToneParams
{
    float scale;          // 濃淡の模様の大きさ
    float minTone;        // 一番薄い所の濃さ
    float depthDarken;    // 芯の深い所（墨が古い所）ほど先に黒くなる強さ
    float timeDarken;     // 0→1：全体を黒に寄せる量
};

float2 InkNoise(float2 p)
{
    return gNoise.SampleLevel(gWrapSampler, p, 0);
}

// 墨の形 0..1（x）と芯からの深さ（y：1 以上が濃い芯、0 が縁）。
// p は墨の中心からの位置、world は画面に固定した座標（ノイズを引く）
float2 InkShape(InkShapeParams ink, float2 p, float2 world)
{
    if (ink.radius < 0.0f)
    {
        return float2(0.0f, 0.0f);
    }
    const float radius = max(ink.radius, 1e-4f);

    // ゆがみ：低周波のノイズ 2 本で位置をずらす。量は半径に比例（広がるほど大きく渦を巻く）
    const float2 warp = float2(
        InkNoise(world * ink.warpScale + float2(ink.flow, 0.0f)).g,
        InkNoise(world * ink.warpScale + float2(0.37f, 0.71f) - float2(0.0f, ink.flow)).g) * 2.0f - 1.0f;
    const float2 q = p + warp * ink.warpAmount * radius;
    const float r = length(q);
    const float2 dir = q / max(r, 1e-5f);

    // 筋：向きごとのノイズで半径を伸び縮みさせる（単位円の上で引くので角度の継ぎ目が出ない）
    const float tendril = InkNoise(dir * ink.tendrilFreq + float2(0.13f, 0.29f) + ink.flow * 0.5f).r;
    const float edgeRadius = radius * (1.0f + ink.tendrilAmount * (tendril - 0.5f));

    // 芯からの深さ：1 以上が濃い芯、0 が縁
    const float depth = (edgeRadius - r) / (radius * ink.edgeWidth + ink.edgeSoftness);

    // 縁のちぎれ：細かいノイズを縁の近くだけに効かせ、薄い所と途切れた所を作る
    const float detail = InkNoise(world * ink.detailScale - ink.flow).r;
    const float density = saturate(depth + (detail - 0.5f) * ink.wispAmount * saturate(1.0f - depth));
    return float2(smoothstep(0.0f, 1.0f, density), depth);
}

// 墨の中の濃さ 0..1。2種類の大きさのノイズで濃い所と薄い所が混ざり、
// 芯の深い所（先に墨が届いた所）と timeDarken で真っ黒が増える
float InkTone(InkToneParams tone, float2 world, float depth, float flow)
{
    const float large = InkNoise(world * tone.scale + float2(0.51f, 0.17f) + flow * 0.7f).g;
    const float small = InkNoise(world * tone.scale * 2.3f + float2(0.83f, 0.42f) - flow).r;
    const float t = smoothstep(0.25f, 0.75f, large * 0.65f + small * 0.35f);
    const float baseTone = lerp(tone.minTone, 1.0f, t);

    const float darkByDepth = saturate((depth - 1.0f) * tone.depthDarken);
    return lerp(baseTone, 1.0f, saturate(darkByDepth + tone.timeDarken));
}

#endif // INK_SHAPE_HLSLI
