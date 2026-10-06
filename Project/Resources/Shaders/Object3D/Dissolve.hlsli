// オブジェクト単位のディゾルブ（Object3d / Object3dNoEnv / Object3dPBR の PS と、ID パスの WriteIDDissolve.PS が共有）。
// マテリアル版（ApplyDissolve / ApplyDissolveEdge）は gMaterial（C++ の Material.h と同じレイアウト）を宣言した後で include する。
// gMaterial を持たないシェーダは DISSOLVE_NO_MATERIAL を定義してから include し、DissolveClip を直接呼ぶ。
//
// マスクはテクスチャを使わず「ワールドの高さ（0..1）」と「ワールド座標の 3D ノイズ」を混ぜて作る。
// ワールド座標基準なので、別メッシュの部位（扉の枠と扉板）や水面の映り込み（反射パスも同じ PS）でも模様がつながる。

float DissolveHash(float3 p)
{
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

// 3D 値ノイズ（0..1）
float DissolveValueNoise(float3 x)
{
    float3 i = floor(x);
    float3 f = frac(x);
    f = f * f * (3.0f - 2.0f * f);
    return lerp(
        lerp(lerp(DissolveHash(i + float3(0, 0, 0)), DissolveHash(i + float3(1, 0, 0)), f.x),
             lerp(DissolveHash(i + float3(0, 1, 0)), DissolveHash(i + float3(1, 1, 0)), f.x), f.y),
        lerp(lerp(DissolveHash(i + float3(0, 0, 1)), DissolveHash(i + float3(1, 0, 1)), f.x),
             lerp(DissolveHash(i + float3(0, 1, 1)), DissolveHash(i + float3(1, 1, 1)), f.x), f.y),
        f.z);
}

// 消える部分は discard し、境目の発光の強さ（0..1）を返す
float DissolveClip(float3 worldPosition, float progress, float edgeWidth,
    float heightMin, float heightMax, float noiseScale, float noiseWeight)
{
    float range = max(heightMax - heightMin, 1e-4f);
    float height = saturate((worldPosition.y - heightMin) / range);
    float3 p = worldPosition * noiseScale;
    float noise = DissolveValueNoise(p) * 0.65f + DissolveValueNoise(p * 2.03f + 17.1f) * 0.35f;
    float mask = lerp(height, noise, noiseWeight);

    // progress=0 で全部消え、1 で発光帯も含めて全部見えるよう、閾値を帯の幅ぶん広げる
    float width = max(edgeWidth, 1e-4f);
    float threshold = progress * (1.0f + width);
    if (mask >= threshold)
    {
        discard;
    }
    return 1.0f - saturate((threshold - mask) / width);
}

#ifndef DISSOLVE_NO_MATERIAL
// マテリアルの値でディゾルブする。無効なら 0
float ApplyDissolve(float3 worldPosition)
{
    if (gMaterial.dissolveEnable == 0)
    {
        return 0.0f;
    }
    return DissolveClip(worldPosition, gMaterial.dissolveProgress, gMaterial.dissolveEdgeWidth,
        gMaterial.dissolveHeightMin, gMaterial.dissolveHeightMax,
        gMaterial.dissolveNoiseScale, gMaterial.dissolveNoiseWeight);
}

// 発光帯の色を乗せる（フォグより前に呼ぶ）
float3 ApplyDissolveEdge(float3 color, float edge)
{
    return lerp(color, gMaterial.dissolveEdgeColor, edge);
}
#endif
