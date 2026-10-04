#ifndef CLOUD_SKY_HLSLI
#define CLOUD_SKY_HLSLI

// 遠景の雲（ノイズ平面加工の亜種。9_CloudRendering.md §3.6 案A）。
// 高さ gCloudHeight の水平な平面にレイを当て、その位置の 2D ノイズで雲の有無を決める。
// Skybox.PS（空）と WaterSurface.PS（映り込み）の両方から呼ぶ。CB は C++ の CloudLayer 1本を共有するので値は必ず一致する。
//
// 式はゼノブレイド2式（CEDEC 2018）と同じ形：透明度 = saturate((ノイズ − 補正値) × 乗算値)。
// 平面は「上面の1点だけ評価するレイマーチ」と見なせるので、フェーズ3の近景と同じパラメータで繋がる。
//
// 1ピクセル 5 サンプル：
//   形（大）・形（小、逆向きに流す）… 輪郭が流れながら変わり続ける。大小の混ぜ方で雲の大きさに差が出る
//   マスク … 補正値を場所ごとにずらす。大きな塊が集まる所と、小さなちぎれ雲だけの所ができる
//   ディゾルブ … 補正値を場所ごとに時間で揺らす。雲が場所ごとにバラバラに湧いたり消えたりする
//   太陽側 … 太陽の方へずらした位置の形。そちらが濃ければ光が遮られている＝暗くする（光側マーチの1ステップ相当）
//
// レジスタ：b7 / t5 / s4（b0〜b6, t0〜t4, s0〜s3 は各 PS の既存の取り決め）

// C++ の CloudLayer::ConstantsForGPU と 1:1（16 バイト単位で並べてある）
cbuffer CloudSkyParams : register(b7)
{
    float3 gCloudEyePosition;     // 空側のレイの原点（描画カメラ）
    int    gCloudEnabled;
    float3 gCloudSunDirection;    // 太陽へ向かう向き（正規化済み）
    float  gCloudHeight;          // 雲の平面の高さ [m]
    float2 gCloudShapeOffset;     // スクロール量（UV。CPU で 0..1 に折り返し済み）
    float2 gCloudDetailOffset;
    float2 gCloudMaskOffset;
    float2 gCloudEvolveOffset;
    float  gCloudShapeTiling;     // 繰り返し [回/m]
    float  gCloudDetailTiling;
    float  gCloudMaskTiling;
    float  gCloudEvolveTiling;
    float  gCloudDetailWeight;    // 小さい形を混ぜる割合
    float  gCloudDetailContrast;  // 混ぜて狭まった分布を 0..1 に戻す倍率（CPU で計算）
    float  gCloudMaskStrength;    // マスクで補正値をずらす幅
    float  gCloudEvolveAmount;    // ディゾルブで補正値を揺らす幅
    float  gCloudThreshold;       // 補正値（全体。CPU で2本のサイン波を足して揺らした後の値）
    float  gCloudSharpness;       // 乗算値（縁のくっきり度）
    float  gCloudOpacity;
    float  gCloudFadeStart;       // この距離 [m] から消し始める
    float  gCloudFadeEnd;         // この距離 [m] で消える
    float  gCloudHazeStart;       // この距離 [m] から空の色に寄せ始める（空気遠近）
    float  gCloudHazeEnd;
    float  gCloudHazeMax;
    float2 gCloudLightOffset;     // 太陽側へずらす量（形の UV）
    float  gCloudLightContrast;   // 太陽側が濃いときの暗くなりやすさ
    float  gCloudSunPower;        // 太陽の方向の明るさの絞り
    float3 gCloudLitColor;        // 光が当たる所の色
    float  gCloudShadowDepth;     // 厚い所をどれだけ影色に寄せるか
    float3 gCloudShadowColor;     // 光が遮られた所（雲の底）の色
    float  gCloudSunGlow;         // 太陽の方向で薄い所を明るくする量
    float3 gCloudEdgeColor;       // 縁の帯に足す色
    float  gCloudPadding;
};

Texture2D<float2> gCloudNoise : register(t5); // R=形 / G=マスク（どちらも 0..1 に一様分布）
SamplerState gCloudSampler : register(s4);    // WRAP + 異方性

// 大小2枚の形を混ぜる。2枚の平均は分布が中央に寄るので、0.5 を中心に広げ直して補正値の意味（被覆率）を保つ
float CloudShape(float2 xz, float2 extraUv)
{
    const float large = gCloudNoise.Sample(gCloudSampler, xz * gCloudShapeTiling + gCloudShapeOffset + extraUv).r;
    const float small = gCloudNoise.Sample(gCloudSampler, xz * gCloudDetailTiling + gCloudDetailOffset).r;
    const float mixed = lerp(large, small, gCloudDetailWeight);
    return saturate(0.5f + (mixed - 0.5f) * gCloudDetailContrast);
}

// origin から dir（正規化済み）へ見た雲。rgb=雲の色（線形）, a=被覆率。
// 空気遠近は hazeColor へ hazeScale の強さで寄せる（背後の空の色が分からない呼び出し元は 0）
float4 CloudSkyCore(float3 origin, float3 dir, float3 hazeColor, float hazeScale)
{
    if (gCloudEnabled == 0)
    {
        return float4(0.0f, 0.0f, 0.0f, 0.0f);
    }

    // 下向き・水平のレイは平面に当たらない。サンプルは分岐の外で行い（ミップ選択の微分を壊さない）、最後に α で消す
    const float dirY = max(dir.y, 1e-3f);
    const float distance = max(gCloudHeight - origin.y, 0.0f) / dirY;
    const float2 xz = origin.xz + dir.xz * distance;

    const float shape = CloudShape(xz, float2(0.0f, 0.0f));
    const float mask = gCloudNoise.Sample(gCloudSampler, xz * gCloudMaskTiling + gCloudMaskOffset).g;
    const float evolve = gCloudNoise.Sample(gCloudSampler, xz * gCloudEvolveTiling + gCloudEvolveOffset).g;

    // 場所ごとの補正値：マスクが高い所は下げて大きな塊に、低い所は上げて小さなちぎれ雲に。ディゾルブで時間変化
    const float threshold = gCloudThreshold
        - (mask - 0.5f) * gCloudMaskStrength
        + (evolve - 0.5f) * gCloudEvolveAmount;

    // ゼノブレ式の透明度。縁は smoothstep 相当で丸める
    const float over = (shape - threshold) * gCloudSharpness;
    float alpha = saturate(over);
    alpha = alpha * alpha * (3.0f - 2.0f * alpha);

    // 太陽側の形のほうが濃い＝そちらに雲が続いていて光が遮られる。塊ごとに明暗の側ができる
    const float sunwardShape = CloudShape(xz, gCloudLightOffset);
    const float occlusion = saturate((sunwardShape - shape) * gCloudLightContrast);
    const float thickness = saturate((over - 1.0f) * gCloudShadowDepth);
    const float darkness = saturate(max(occlusion, thickness));

    const float edge = 4.0f * alpha * (1.0f - alpha);
    const float sun = pow(saturate(dot(dir, gCloudSunDirection)), gCloudSunPower);
    float3 color = lerp(gCloudLitColor, gCloudShadowColor, darkness);
    color += gCloudEdgeColor * edge;
    color += gCloudLitColor * (sun * gCloudSunGlow * (1.0f - darkness));

    // 空気遠近：遠い雲ほど背後の空の色に寄り、コントラストが落ちる
    const float haze = saturate((distance - gCloudHazeStart) / max(gCloudHazeEnd - gCloudHazeStart, 1.0f)) * gCloudHazeMax;
    color = lerp(color, hazeColor, haze * hazeScale);

    // 水平線ぎりぎりは交点が遠すぎて UV が暴れるので消す（ミップ・異方性と合わせてちらつきを抑える）
    const float fade = saturate((gCloudFadeEnd - distance) / max(gCloudFadeEnd - gCloudFadeStart, 1.0f));
    const float above = (dir.y > 0.0f && origin.y < gCloudHeight) ? 1.0f : 0.0f;
    alpha *= fade * above * gCloudOpacity;

    return float4(color, alpha);
}

// 空（Skybox / 水面の映り込み）用：その方向の空の色 skyColor に雲を重ねた色を返す。空気遠近は skyColor へ寄せる
float3 ApplyCloudSky(float3 skyColor, float3 origin, float3 dir)
{
    const float4 cloud = CloudSkyCore(origin, dir, skyColor, 1.0f);
    return lerp(skyColor, cloud.rgb, cloud.a);
}

// 物の鏡面反射（Object3dPBR）用：雲だけを返す。背後の空の色を持たないので空気遠近は掛けない
float4 CloudSky(float3 origin, float3 dir)
{
    return CloudSkyCore(origin, dir, float3(0.0f, 0.0f, 0.0f), 0.0f);
}

#endif // CLOUD_SKY_HLSLI
