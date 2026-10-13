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

// 画面座標の六角形（蜂の巣）。中心のマスから順に晴れて（discard）、晴れる手前の帯のマスが光る。
// 全天周囲モニターが外を映す演出（18_HubTransitions.md ①の IN）。返り値は光る強さ（0..1）
//   reveal   : 晴れた割合（0=何も晴れていない / 1=全部晴れた）
//   cellSize : 六角形の大きさ [px]
//   center   : 晴れ始める中心 [px]
//   band     : 光る帯の幅（マスの「中心からの遠さ」0..1 の単位）
//   jitter   : マスごとのばらつき（0..1）
float DissolveHexScreen(float2 screenPosition, float reveal, float cellSize, float2 center,
    float band, float jitter)
{
    const float2 s = float2(1.0f, 1.7320508f);
    const float2 h = s * 0.5f;
    float2 p = screenPosition / max(cellSize, 1.0f) + 1000.0f;   // fmod が負にならないよう正へずらす
    float2 a = fmod(p, s) - h;
    float2 b = fmod(p - h, s) - h;
    float2 local = (dot(a, a) < dot(b, b)) ? a : b;
    float2 cellId = p - local;

    // マスの中心が画面の中心からどれだけ遠いか（0..1。画面の角で 1）
    float2 cellCenter = (cellId - 1000.0f) * cellSize;
    float maxDist = max(length(center), 1.0f);
    float dist = length(cellCenter - center) / maxDist;
    dist += (DissolveHash(float3(cellId, 3.7f)) - 0.5f) * jitter;

    // 晴れる位置を帯の幅（とばらつき）ぶん広げ、reveal=0 で何も晴れず・何も光らず、reveal=1 で全部晴れるようにする
    float width = max(band, 1e-4f);
    float front = reveal * (1.0f + width * 4.0f + jitter) - width * 3.0f;
    if (dist < front - width)
    {
        discard;
    }
    // 帯の中：前（晴れる直前）ほど明るく塗る
    float t = saturate((dist - (front - width)) / width);
    float fill = 1.0f - t;

    // 六角形の縁（マスの外周）は帯の先へ波のように広がり、だんだん薄くなる（マスの形を見せる）
    float2 q = abs(local);
    float hexDist = max(dot(q, normalize(s)), q.x);   // 中心 0、縁 0.5
    float outline = smoothstep(0.42f, 0.5f, hexDist);
    float wave = saturate(1.0f - (dist - front) / (width * 3.0f));
    // マスの間のすき間（縁の外側）は塗りを抜いて、六角形が並んで見えるようにする
    float inner = 1.0f - smoothstep(0.40f, 0.44f, hexDist);
    float glow = fill * inner + outline * wave * (0.3f + 0.7f * fill);
    return saturate(glow);
}

#ifndef DISSOLVE_NO_MATERIAL
// マテリアルの値でディゾルブする。無効なら 0。
// dissolveEnable = 2（六角形）のとき、欄はこう読み替える：
//   dissolveProgress = 見えている割合（1=全部見える＝何も晴れていない、0=全部晴れた）
//   dissolveNoiseScale = 六角形の大きさ [px] / dissolveHeightMin, Max = 中心 [px] /
//   dissolveEdgeWidth = 光る帯の幅 / dissolveNoiseWeight = マスごとのばらつき
float ApplyDissolve(float3 worldPosition, float2 screenPosition)
{
    if (gMaterial.dissolveEnable == 0)
    {
        return 0.0f;
    }
    if (gMaterial.dissolveEnable == 2)
    {
        return DissolveHexScreen(screenPosition, 1.0f - gMaterial.dissolveProgress, gMaterial.dissolveNoiseScale,
            float2(gMaterial.dissolveHeightMin, gMaterial.dissolveHeightMax),
            gMaterial.dissolveEdgeWidth, gMaterial.dissolveNoiseWeight);
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

// 発光（Material::emissive）を足す（フォグより前に呼ぶ）。emissive.w = 1 ならテクスチャの色を掛ける（文字を描いた板など）
float3 ApplyEmissive(float3 color, float3 textureRgb)
{
    return color + gMaterial.emissive.rgb * lerp(float3(1.0f, 1.0f, 1.0f), textureRgb, gMaterial.emissive.w);
}
#endif
