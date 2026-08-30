#ifndef FOG_HLSLI
#define FOG_HLSLI

// ============================================================================
// 距離フォグ（大気遠近）
//
// レジスタ割り当ての取り決め:
//   b0 material / b1 平行光源 / b2 カメラ / b3 点光源 / b4 スポット / b5 シャドウ
//   → フォグは b6。ルートパラメータは末尾（11番）に追加してある。
//
// 適用対象は Object3D 系の3つの PS（環境マップあり / なし / PBR）のみ。
// Primitive とパーティクルは別ルートシグネチャで、そもそもカメラ CB を持たない。
// 演出（ビーム・爆発）が霧で薄まるのは望ましくないので、対象外は意図的。
// Skybox も対象外。空そのものは霧で塗るものではなく、逆にフォグ色を空に合わせて使う。
// ============================================================================

cbuffer FogBuffer : register(b6)
{
    float4 gFogColor;
    float  gFogNear;     // ここより手前は素通し
    float  gFogFar;      // ここで density 分だけ乗る
    float  gFogDensity;  // 1.0 で far において完全にフォグ色
    int    gFogEnabled;  // 0 なら完全にスキップ
}

/// <summary>
/// カメラからの距離でフォグを乗せる。アルファは変更しない
/// （半透明のフェード具合まで変わると抜きが破綻するため）。
/// </summary>
float3 ApplyFog(float3 color, float3 worldPosition, float3 cameraWorldPosition)
{
    if (gFogEnabled == 0)
    {
        return color;
    }

    const float dist = length(worldPosition - cameraWorldPosition);
    const float range = max(gFogFar - gFogNear, 1e-3f);
    const float t = saturate((dist - gFogNear) / range);
    const float f = saturate(t * gFogDensity);

    return lerp(color, gFogColor.rgb, f);
}

#endif // FOG_HLSLI
