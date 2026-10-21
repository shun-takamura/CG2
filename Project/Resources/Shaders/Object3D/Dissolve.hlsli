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

// 点からの距離（楕円体）。胸の装甲に入口を開ける（hole）・羽根を付け根から出す／先から消す（grow）
// （17_HubScene.md／player_mech_model）。ワールド座標なので体の別の部位とも、水面の映り込みともつながる。返り値は光る強さ（0..1）
//   center   : 中心（ワールド）
//   radius   : 半径 [m]。hole は内側が消える、grow は内側だけが見える
//   stretchY : 縦の伸び（1=球。2 なら縦に 2 倍の楕円体）
//   edgeWidth: 光る縁の幅 [m]（残る側に付く）
//   noiseScale / noiseAmplitude : 縁のゆらぎの細かさ [1/m] と振れ幅 [m]
float DissolveSphere(float3 worldPosition, float3 center, float radius, float stretchY, float edgeWidth,
    float noiseScale, float noiseAmplitude, bool grow)
{
    float width = max(edgeWidth, 1e-4f);
    if (radius <= 0.0f)
    {
        if (grow)
        {
            discard;
        }
        return 0.0f;
    }
    float3 d = worldPosition - center;
    d.y /= max(stretchY, 1e-3f);
    float3 p = worldPosition * noiseScale;
    float noise = DissolveValueNoise(p) * 0.65f + DissolveValueNoise(p * 2.03f + 17.1f) * 0.35f;
    float dist = length(d) + (noise - 0.5f) * 2.0f * noiseAmplitude;
    // 残る側への距離（負なら消える）
    float inside = grow ? (radius - dist) : (dist - radius);
    if (inside < 0.0f)
    {
        discard;
    }
    // hole は開き始め（半径が縁の幅より小さい間）に光を立ち上げる（半径 0 で点が光らないように）
    float fadeIn = grow ? 1.0f : saturate(radius / width);
    return (1.0f - saturate(inside / width)) * fadeIn;
}

// ワールドの六角形（DissolveHexScreen と同じ見た目をワールドの XY 面に）。胸の装甲に入口を開ける
// （17_HubScene.md §5.1。ステージ選択の六角形とそろえる）。中心に近いマスから順にマスごと消え、消える手前の帯のマスが光り、
// マスの縁が帯の先へ波のように広がる。マスは +Z を向いた面に並べ、奥行きは中心から 1m までだけを切る（背中側は残す）。
// 返り値は光る強さ（0..1）
//   center   : 中心（ワールド）
//   radius   : 消えた所の広がり [m]（帯の幅ぶん先まで光る）。0 で何もしない
//   stretchY : 縦の伸び（1=円）
//   band     : 光る帯の幅 [m]
//   cellSize : 六角形の大きさ [m]
//   jitter   : マスごとのばらつき [m]
float DissolveHexWorld(float3 worldPosition, float3 center, float radius, float stretchY, float band,
    float cellSize, float jitter)
{
    if (radius <= 0.0f || worldPosition.z < center.z - 1.0f)
    {
        return 0.0f;
    }
    const float2 s = float2(1.0f, 1.7320508f);
    const float2 h = s * 0.5f;
    float2 p = worldPosition.xy / max(cellSize, 1e-3f) + 1000.0f;   // fmod が負にならないよう正へずらす
    float2 a = fmod(p, s) - h;
    float2 b = fmod(p - h, s) - h;
    float2 local = (dot(a, a) < dot(b, b)) ? a : b;
    float2 cellId = p - local;

    float2 d = (cellId - 1000.0f) * cellSize - center.xy;
    d.y /= max(stretchY, 1e-3f);
    float dist = length(d) + (DissolveHash(float3(cellId, 5.3f)) - 0.5f) * jitter;

    float width = max(band, 1e-4f);
    float front = radius;
    if (dist < front - width)
    {
        discard;
    }
    float t = saturate((dist - (front - width)) / width);
    float fill = 1.0f - t;
    float2 q = abs(local);
    float hexDist = max(dot(q, normalize(s)), q.x);   // 中心 0、縁 0.5
    float outline = smoothstep(0.42f, 0.5f, hexDist);
    float wave = saturate(1.0f - (dist - front) / (width * 3.0f));
    float inner = 1.0f - smoothstep(0.40f, 0.44f, hexDist);
    float glow = fill * inner + outline * wave * (0.3f + 0.7f * fill);
    // 開き始め（広がりが帯の幅より小さい間）に光を立ち上げる
    return saturate(glow) * saturate(radius / width);
}

// 高さの面（ワールドの水平面）。面より下が消え、面を上げていくと下から上へほどける（死亡演出のエーテル化、
// 19_GameOverAndDifficulty.md §1）。ワールド座標なので体の別の部位とも、水面の映り込みともつながる。返り値は光る強さ（0..1）
//   height   : 面の高さ（ワールド Y）[m]
//   edgeWidth: 光る縁の幅 [m]（面の上の残る側に付く）
//   noiseScale / noiseAmplitude : 縁のゆらぎの細かさ [1/m] と振れ幅 [m]
float DissolveRise(float3 worldPosition, float height, float edgeWidth, float noiseScale, float noiseAmplitude)
{
    float width = max(edgeWidth, 1e-4f);
    float3 p = worldPosition * noiseScale;
    float noise = DissolveValueNoise(p) * 0.65f + DissolveValueNoise(p * 2.03f + 17.1f) * 0.35f;
    // 面からの高さ（負なら消える）
    float above = worldPosition.y + (noise - 0.5f) * 2.0f * noiseAmplitude - height;
    if (above < 0.0f)
    {
        discard;
    }
    return 1.0f - saturate(above / width);
}

#ifndef DISSOLVE_NO_MATERIAL
// マテリアルの値でディゾルブする。無効なら 0。
// dissolveEnable = 2（六角形）のとき、欄はこう読み替える：
//   dissolveProgress = 見えている割合（1=全部見える＝何も晴れていない、0=全部晴れた）
//   dissolveNoiseScale = 六角形の大きさ [px] / dissolveHeightMin, Max = 中心 [px] /
//   dissolveEdgeWidth = 光る帯の幅 / dissolveNoiseWeight = マスごとのばらつき
// dissolveEnable = 3（点からの距離・内側が消える）/ 4（内側だけ見える）のとき（C++ の SetDissolveSphere）：
//   dissolveHeightMin, Max, dissolvePadding = 中心（ワールド）/ dissolveProgress = 半径 [m] /
//   dissolvePadding2 = 縦の伸び / dissolveEdgeWidth = 縁の幅 [m] / dissolveNoiseScale, Weight = ゆらぎの細かさ・振れ幅 [m]
// dissolveEnable = 5（ワールドの六角形・内側が消える）のとき：中心・半径・縦の伸びは 3/4 と同じ /
//   dissolveEdgeWidth = 光る帯の幅 [m] / dissolveNoiseScale = 六角形の大きさ [m] / dissolveNoiseWeight = マスごとのばらつき [m]
// dissolveEnable = 6（高さの面・下が消える。C++ の SetDissolvePlane）のとき：
//   dissolveProgress = 面の高さ [m] / dissolveEdgeWidth = 縁の幅 [m] / dissolveNoiseScale, Weight = ゆらぎの細かさ・振れ幅 [m]
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
    if (gMaterial.dissolveEnable == 3 || gMaterial.dissolveEnable == 4)
    {
        return DissolveSphere(worldPosition,
            float3(gMaterial.dissolveHeightMin, gMaterial.dissolveHeightMax, gMaterial.dissolvePadding),
            gMaterial.dissolveProgress, gMaterial.dissolvePadding2, gMaterial.dissolveEdgeWidth,
            gMaterial.dissolveNoiseScale, gMaterial.dissolveNoiseWeight, gMaterial.dissolveEnable == 4);
    }
    if (gMaterial.dissolveEnable == 5)
    {
        return DissolveHexWorld(worldPosition,
            float3(gMaterial.dissolveHeightMin, gMaterial.dissolveHeightMax, gMaterial.dissolvePadding),
            gMaterial.dissolveProgress, gMaterial.dissolvePadding2, gMaterial.dissolveEdgeWidth,
            gMaterial.dissolveNoiseScale, gMaterial.dissolveNoiseWeight);
    }
    if (gMaterial.dissolveEnable == 6)
    {
        return DissolveRise(worldPosition, gMaterial.dissolveProgress, gMaterial.dissolveEdgeWidth,
            gMaterial.dissolveNoiseScale, gMaterial.dissolveNoiseWeight);
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
