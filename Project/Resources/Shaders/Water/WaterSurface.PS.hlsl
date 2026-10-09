#include "WaterSurface.hlsli"

// 浅い水面。反射（映す物＋空）と、水底の床の透過をフレネルで混ぜる。
// 水面は板1枚なので頂点は動かさず、さざ波は法線だけを揺らす（反射方向・反射 RT の UV・屈折・フレネルに効く）。
// レジスタは Object3D と取り決めを共有：b1=平行光源 / b5,t3,s1,s2=シャドウ / b6=フォグ。t2 は PBR 法線用に空ける。
// b7,t5,s4,t9 は遠景の雲（CloudSky.hlsli。Skybox と同じ CB を共有）。

#define MAX_EMITTED_RINGS 16 // C++ の WaterSurface::kMaxEmittedRings と合わせる

// C++ の WaterSurface::EmittedRingForGPU と 1:1
struct EmittedRing
{
    float2 center;      // ワールド XZ
    float  emitTime;
    float  amplitude;   // 0 なら未使用
    float  wavelength;
    float  packetLength;
    float  maxRadius;
    float  padding;
};

cbuffer WaterParams : register(b0)
{
    float4 gWaterColor;          // 深い所で寄っていく水の色
    float3 gAbsorption;          // 吸収係数 [1/m]
    float  gDepth;               // 水深 [m]
    float3 gCameraPosition;
    float  gWaterHeight;
    float2 gScreenSize;
    float  gFresnelF0;
    float  gReflectionIntensity;
    float  gFloorTiling;         // 床テクスチャの繰り返し [回/m]
    float  gAmbient;
    float  gIor;
    float  gSkyIntensity;
    int    gHasReflection;
    int    gDebugView;           // 0=合成 / 1=床のみ / 2=反射のみ / 3=フレネル / 4=法線
    float2 gPadding;
    float4x4 gReflectionViewProj; // 反射を作った視点の ViewProj（鏡像前）
    float3 gShadingEye;          // 反射を作った視点の位置
    float  gPadding2;
    float2 gRippleCenter;        // 同心円の中心（ワールド XZ）
    float  gRingAmplitude;
    float  gRingWavelength;
    float  gRingSpeed;
    float  gRingFalloff;
    float  gNoiseAmplitude;
    float  gNoiseScale;
    float  gNoiseSpeed;
    float  gDistortion;
    float  gTime;
    float  gRingInterval;        // 束が出る平均間隔 [s]
    float  gRingJitter;          // 間隔のばらつき（0..1）
    float  gRingMinAmplitude;    // 束の高さの下限（比）
    float  gRingPacketLength;    // 束の長さ（波長の数）
    float  gRingMaxRadius;       // 波紋が届く距離 [m]
    float  gAmbientRingScale;    // ランダムな波（待機中の波）の振幅倍率
    float3 gPadding3;
    EmittedRing gEmittedRings[MAX_EMITTED_RINGS]; // CPU から EmitRing で出した波
    float2 gSimCenter;           // 波のシミュレーションの範囲の中心（ワールド XZ）
    float  gSimSize;             // 範囲の一辺 [m]
    float  gSimNormalScale;      // 高さの傾きを法線に足す倍率
    float  gSimTexel;            // 1 テクセルの UV 幅
    float3 gPadding4;
    float  gFloorParallaxDepth;  // 床の凹凸の深さ（UV 1 あたり）
    float  gFloorNormalStrength;
    int    gFloorUseNormalMap;
    int    gFloorUseParallax;
    float  gFloorParallaxMinLayers;
    float  gFloorParallaxMaxLayers;
    float  gFloorNormalFlipY;
    float  gPadding5;
    // ----- 大河モード（13_RiverWater.md）。C++ の WaterSurface::Params の末尾と 1:1 -----
    int    gRiverMode;           // 0=従来（床を透かす不透明）/ 1=大河（川のマップ・乗算済み α）
    float  gRiverDepthThreshold; // これより浅い所は描かない [m]
    float  gRiverMaxDepth;       // 川のマップの R=1 の水深 [m]
    float  gRiverOpacity;        // 水の濁り [1/m]
    float2 gRiverMapOrigin;      // (x_min, z_max)
    float2 gRiverMapInvSize;     // (1/幅X, 1/幅Z)
    float4 gRiverShallowColor;
    float4 gRiverDeepColor;
    float  gRiverDeepDepth;      // この水深で深い色になりきる [m]
    float  gRiverMinCos;         // 水の中を通る長さの角度補正の下限
    float  gRiverShoreFade;      // 水際で反射ごと消えていく幅 [m]
    float  gReflectionRtWeight;  // 反射 RT（映す物）の重み。0 で空だけ
    float  gReflectionDistance;  // > 0 なら反射 RT も波で傾いた反射の向きで引く（映る物までの想定距離 [m]）。0 で従来
    float3 gPadding6;
};

struct DirectionalLight
{
    float4 color;
    float3 direction;
    float intensity;
    int lightingType;
};

cbuffer DirectionalLightBuffer : register(b1)
{
    DirectionalLight gDirectionalLight;
};

Texture2D<float4> gReflectionTexture : register(t0);
TextureCube<float4> gSkyTexture : register(t1);
Texture2D<float4> gFloorTexture : register(t4);
Texture2D<float2> gRippleHeight : register(t6); // 波のシミュレーション（R=高さ。RippleSimulation.CS）
Texture2D<float4> gFloorNormalMap : register(t7); // 床の法線マップ（接空間）
Texture2D<float4> gFloorHeightMap : register(t8); // 床のハイトマップ（R。1=高い）
Texture2D<float4> gRiverMap : register(t10); // 川のマップ（R=水深/maxDepth、GB=流れ。大河モードのみ）
SamplerState gSampler : register(s0);
SamplerState gClampSampler : register(s3);

#include "../Object3D/Shadow.hlsli"
#include "../Object3D/Fog.hlsli"
#define CLOUD_SKY_MOKO // もこもこ（t9）を使う。PBR の映り込みは使わない
#include "../Cloud/CloudSky.hlsli"

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

static const float kPi = 3.14159265f;

float Hash21(float2 p)
{
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

// バリューノイズと、その解析的な偏微分。戻り値 = (値, d/dx, d/dy)
float3 ValueNoiseWithDerivative(float2 p)
{
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f); // quintic
    const float2 du = 30.0f * f * f * (f * (f - 2.0f) + 1.0f);

    const float a = Hash21(i);
    const float b = Hash21(i + float2(1.0f, 0.0f));
    const float c = Hash21(i + float2(0.0f, 1.0f));
    const float d = Hash21(i + float2(1.0f, 1.0f));
    const float k = a - b - c + d;

    const float value = a + (b - a) * u.x + (c - a) * u.y + k * u.x * u.y;
    const float dx = du.x * ((b - a) + k * u.y);
    const float dy = du.y * ((c - a) + k * u.x);
    return float3(value, dx, dy);
}

// 波の細かさがピクセルより細かくなったら弱める（遠くのちらつき対策）。
// 1波長が 2px 以下で 0、6px 以上で 1。
float AntiAliasFade(float featureSize, float footprint)
{
    return saturate(featureSize / max(footprint, 1e-4f) * 0.25f - 0.5f);
}

float Hash11(float x)
{
    return frac(sin(x * 127.1f) * 43758.5453f);
}

#define MAX_RING_PACKETS 16

// 波の束1つ分の、半径方向の傾き dH/dr。
// 束の中心は先端（age*speed）から半分後ろ。ガウス包絡 × sin。
float RingPacketSlope(float r, float age, float amplitude, float wavelength, float packetLength, float footprint)
{
    const float k = 2.0f * kPi / wavelength;
    const float halfLength = max(packetLength * wavelength * 0.5f, 1e-3f);
    const float d = r - (age * gRingSpeed - halfLength);
    const float envelope = exp(-(d * d) / (halfLength * halfLength));
    if (envelope < 1e-3f)
    {
        return 0.0f;
    }
    float s, c;
    sincos(k * d, s, c);
    const float envelopeSlope = -2.0f * d / (halfLength * halfLength) * envelope;
    return amplitude * (envelope * k * c + envelopeSlope * s) * AntiAliasFade(wavelength, footprint);
}

// 距離による減衰と、中心（方向が定まらない）の消去
float RingDistanceFade(float r, float maxRadius, float wavelength)
{
    return exp(-r / gRingFalloff) * saturate(1.0f - r / maxRadius) * saturate(r / (wavelength * 0.5f));
}

// CPU から EmitRing で出した波（大きな波）。中心は波ごとに持つ
float2 EmittedRingsGradient(float2 xz, float footprint)
{
    float2 gradient = float2(0.0f, 0.0f);
    [loop]
    for (int i = 0; i < MAX_EMITTED_RINGS; ++i)
    {
        const EmittedRing ring = gEmittedRings[i];
        const float age = gTime - ring.emitTime;
        if (ring.amplitude <= 0.0f || age < 0.0f)
        {
            continue;
        }
        const float2 toPoint = xz - ring.center;
        const float r = length(toPoint);
        const float slope = RingPacketSlope(r, age, ring.amplitude, ring.wavelength, ring.packetLength, footprint);
        gradient += toPoint / max(r, 1e-4f) * (slope * RingDistanceFade(r, ring.maxRadius, ring.wavelength));
    }
    return gradient;
}

// 同心円のさざ波（待機中の波）。中心から平均 gRingInterval 秒ごとに「数波長ぶんの波の束」が出て外へ広がる。
// 束ごとに出るタイミング・高さ・波長を時刻からのハッシュで決めるので、CPU 側に状態を持たない。
float2 RingPacketsGradient(float2 xz, float footprint)
{
    if (gAmbientRingScale <= 0.0f)
    {
        return float2(0.0f, 0.0f);
    }

    const float2 toPoint = xz - gRippleCenter;
    const float r = length(toPoint);
    const float2 radial = toPoint / max(r, 1e-4f);

    const float interval = max(gRingInterval, 0.05f);
    const float spacing = max(gRingSpeed * interval, 1e-3f);
    const int packetCount = min(MAX_RING_PACKETS, (int) ceil(gRingMaxRadius / spacing) + 1);
    const float newest = floor(gTime / interval);

    const float distanceFade = RingDistanceFade(r, gRingMaxRadius, gRingWavelength);

    float slope = 0.0f; // 半径方向の傾き dH/dr
    [loop]
    for (int i = 0; i < packetCount; ++i)
    {
        const float id = newest - i;
        const float hTiming = Hash11(id * 1.37f + 0.11f);
        const float hAmplitude = Hash11(id * 2.71f + 0.37f);
        const float hWavelength = Hash11(id * 3.97f + 0.73f);
        const float hSkip = Hash11(id * 5.23f + 0.91f);

        // ばらつきが大きいほど「出ない回」も増やし、間隔を空ける（最大で半分）
        if (hSkip < gRingJitter * 0.5f)
        {
            continue;
        }

        const float emitTime = (id + (hTiming - 0.5f) * gRingJitter) * interval;
        const float age = gTime - emitTime;
        if (age < 0.0f)
        {
            continue;
        }

        const float wavelength = gRingWavelength * lerp(0.5f, 1.0f, hWavelength);
        const float amplitude = gRingAmplitude * lerp(gRingMinAmplitude, 1.0f, hAmplitude);
        slope += RingPacketSlope(r, age, amplitude, wavelength, gRingPacketLength, footprint);
    }

    return radial * (slope * distanceFade * gAmbientRingScale);
}

// 水面の高さの勾配 (dH/dx, dH/dz) を返す。法線は normalize(-gx, 1, -gz)。
float2 WaveGradient(float2 xz, float footprint)
{
    float2 gradient = float2(0.0f, 0.0f);

    gradient += RingPacketsGradient(xz, footprint);
    gradient += EmittedRingsGradient(xz, footprint);

    // ----- 波のシミュレーション：高さマップの傾き（中心差分） -----
    {
        const float2 uv = (xz - gSimCenter) / gSimSize + 0.5f;
        if (all(uv > 0.0f) && all(uv < 1.0f))
        {
            const float2 du = float2(gSimTexel, 0.0f);
            const float2 dv = float2(0.0f, gSimTexel);
            const float hx = gRippleHeight.SampleLevel(gClampSampler, uv + du, 0).r - gRippleHeight.SampleLevel(gClampSampler, uv - du, 0).r;
            const float hz = gRippleHeight.SampleLevel(gClampSampler, uv + dv, 0).r - gRippleHeight.SampleLevel(gClampSampler, uv - dv, 0).r;
            const float texelWorld = gSimSize * gSimTexel;
            // 遠くで 1 ピクセルに何テクセルも入るとちらつくので弱める（数テクセル＝おおよその最短波長）
            const float fade = AntiAliasFade(texelWorld * 6.0f, footprint);
            gradient += float2(hx, hz) / (2.0f * texelWorld) * (gSimNormalScale * fade);
        }
    }

    // ----- 揺らぎ：ノイズ2オクターブを別方向に流す -----
    {
        const float s1 = gNoiseScale;
        const float s2 = gNoiseScale * 2.13f;
        const float2 flow1 = float2(0.8f, 0.6f) * gNoiseSpeed * gTime;
        const float2 flow2 = float2(-0.5f, 0.87f) * gNoiseSpeed * 1.3f * gTime;
        const float3 n1 = ValueNoiseWithDerivative((xz + flow1) * s1);
        const float3 n2 = ValueNoiseWithDerivative((xz + flow2) * s2);
        const float fade1 = AntiAliasFade(1.0f / s1, footprint);
        const float fade2 = AntiAliasFade(1.0f / s2, footprint);
        gradient += gNoiseAmplitude * (n1.yz * s1 * fade1 + 0.5f * n2.yz * s2 * fade2);
    }

    return gradient;
}

// 床の視差オクルージョン（Object3dPBR.PS の ParallaxOcclusionUV と同じ手順）。
// 床は水平なので接空間は T=+X / B=+Z / N=+Y。viewTS は床から視点へ向かう向き
float2 FloorParallaxUV(float2 uv, float3 viewTS)
{
    const float2 dx = ddx(uv);
    const float2 dy = ddy(uv);
    const float layers = lerp(gFloorParallaxMaxLayers, gFloorParallaxMinLayers, saturate(viewTS.z));
    const float layerDepth = 1.0f / layers;
    const float2 deltaUV = viewTS.xy / max(viewTS.z, 0.1f) * gFloorParallaxDepth / layers;

    float2 currentUV = uv;
    float currentDepth = 0.0f;
    float mapDepth = 1.0f - gFloorHeightMap.SampleGrad(gSampler, currentUV, dx, dy).r;
    [loop]
    for (int i = 0; i < 64 && currentDepth < mapDepth; ++i)
    {
        currentUV -= deltaUV;
        mapDepth = 1.0f - gFloorHeightMap.SampleGrad(gSampler, currentUV, dx, dy).r;
        currentDepth += layerDepth;
    }

    const float2 prevUV = currentUV + deltaUV;
    const float after = mapDepth - currentDepth;
    const float before = (1.0f - gFloorHeightMap.SampleGrad(gSampler, prevUV, dx, dy).r) - (currentDepth - layerDepth);
    const float w = after / (after - before + 1e-5f);
    return lerp(currentUV, prevUV, saturate(w));
}

PixelShaderOutput main(WaterVertexOutput input)
{
    PixelShaderOutput output;

    // 1ピクセルが覆う水面の幅 [m]
    const float2 xz = input.worldPosition.xz;
    const float footprint = max(length(ddx(xz)), length(ddy(xz)));

    // 大河モード：川のマップの水深。描かない所は波の計算より先に落とす
    float riverDepth = 0.0f;
    if (gRiverMode != 0)
    {
        const float2 riverUv = float2((xz.x - gRiverMapOrigin.x) * gRiverMapInvSize.x,
                                      (gRiverMapOrigin.y - xz.y) * gRiverMapInvSize.y);
        // 範囲の外（地形の外）には水を張らない。外周の地形の端は遠景の物で隠す
        if (any(riverUv < 0.0f) || any(riverUv > 1.0f))
        {
            discard;
        }
        riverDepth = gRiverMap.SampleLevel(gClampSampler, riverUv, 0).r * gRiverMaxDepth;
        // 岸の下と滝の縁の先（深さ 0）。縁の先の空中に鏡が浮いたり、谷から天井に見えたりしないように
        if (riverDepth < gRiverDepthThreshold)
        {
            discard;
        }
    }

    const float2 gradient = WaveGradient(xz, footprint);
    const float3 normal = normalize(float3(-gradient.x, 1.0f, -gradient.y));

    // 見え方は反射を作った視点基準（通常は描画カメラと同じ。デバッグ時はゲームカメラ基準の見え方を焼き付ける）
    const float3 viewDir = normalize(input.worldPosition - gShadingEye); // 視点→水面

    // ===== フレネル（Schlick） =====
    const float cosTheta = saturate(dot(-viewDir, normal));
    const float fresnel = gFresnelF0 + (1.0f - gFresnelF0) * pow(1.0f - cosTheta, 5.0f);

    // ===== 反射 =====
    // 空は無限遠なので反射ベクトルで cubemap を引けば正確。近くの物は反射 RT から。
    // 水面上の点は鏡像にしても動かないので、反射を作った視点の VP で投影すれば RT 上の位置になる
    float3 reflectDir = reflect(viewDir, normal);
    // 浅い角度で波に揺らされて水平線より下を向くと cubemap の地面側を拾うので、上半球に留める
    reflectDir.y = max(reflectDir.y, 0.02f);
    reflectDir = normalize(reflectDir);
    float3 sky = gSkyTexture.Sample(gSampler, reflectDir).rgb;
    // 雲は空と同じ関数で、水面の点から反射方向へ評価する（鏡像カメラから飛ばすレイと同じ直線なので空側とずれない）。
    // 反射 RT（ロゴ・扉）より先に重ねるので、雲は映す物の後ろに入る
    sky = ApplyCloudSky(sky, input.worldPosition, reflectDir);
    float3 reflection = sky * gSkyIntensity;
    if (gHasReflection != 0)
    {
        const float4 clip = mul(float4(input.worldPosition, 1.0f), gReflectionViewProj);
        float2 reflectionUv = clip.xy / clip.w * float2(0.5f, -0.5f) + 0.5f;
        // 視点の後ろ・画角の外は反射 RT に情報が無い（通常時は起きない。デバッグ視点で回り込んだ時用）
        const bool inView = clip.w > 0.0f && all(reflectionUv >= 0.0f) && all(reflectionUv <= 1.0f);
        if (inView)
        {
            if (gReflectionDistance > 0.0f)
            {
                // 空と同じく、波で傾いた反射の向きで引く（UV を少しずらすだけだと、空は大きく揺れるのに映る物はほぼ揺れない）。
                // 反射の向きの鏡像（Y を反転）へ想定距離だけ進んだ点を投影すると、反射 RT 上の位置になる。
                // 波が無ければ鏡像の向き = 視線の向きなので、水面の点と同じ位置になる
                const float3 r = reflect(viewDir, normal);
                const float4 q = mul(float4(input.worldPosition + float3(r.x, -r.y, r.z) * gReflectionDistance, 1.0f), gReflectionViewProj);
                if (q.w > 0.0f)
                {
                    reflectionUv = q.xy / q.w * float2(0.5f, -0.5f) + 0.5f;
                }
            }
            reflectionUv += normal.xz * gDistortion;
            // 反射 RT は乗算済み α（rgb は α 込み）。不透明の物は a=1、加算のエフェクトは a を増やさず rgb だけ足される
            const float4 mirrored = gReflectionTexture.Sample(gClampSampler, reflectionUv) * gReflectionRtWeight;
            reflection = reflection * (1.0f - mirrored.a) + mirrored.rgb;
        }
    }
    reflection *= gReflectionIntensity;

    // ===== 大河モード：床を描かず、水の色と反射を乗算済み α で下の地形（川底）に重ねる =====
    if (gRiverMode != 0)
    {
        const float3 lightDirection = normalize(-gDirectionalLight.direction);
        const float3 up = float3(0.0f, 1.0f, 0.0f);
        const float sunShadow = CalcShadowFactor(input.worldPosition, up, input.position.xy);
        const float3 incoming = gDirectionalLight.color.rgb * gDirectionalLight.intensity
            * saturate(lightDirection.y) * sunShadow + gAmbient;
        const float deepness = saturate(riverDepth / max(gRiverDeepDepth, 1e-3f));
        const float3 body = lerp(gRiverShallowColor.rgb, gRiverDeepColor.rgb, deepness) * incoming;

        // 視線が水の中を通る長さ（真上からなら水深、斜めほど長い）で透け具合を決める
        const float transmittance = exp(-gRiverOpacity * riverDepth / max(cosTheta, gRiverMinCos));
        // 下の地形が残る割合 = 透過 × (1 - フレネル)
        float alpha = 1.0f - transmittance * (1.0f - fresnel);
        const float3 premultiplied = body * (1.0f - transmittance) * (1.0f - fresnel) + reflection * fresnel;

        if (gDebugView >= 1 && gDebugView <= 4)
        {
            float3 debugColor = body;
            if (gDebugView == 2)
            {
                debugColor = reflection;
            }
            else if (gDebugView == 3)
            {
                debugColor = fresnel.xxx;
            }
            else if (gDebugView == 4)
            {
                debugColor = normal * 0.5f + 0.5f;
            }
            output.color = float4(debugColor, 1.0f);
            return output;
        }

        // フォグは乗算前の色に掛ける（下の地形はフォグ済みなので、そのまま重ねて釣り合う）
        float3 straight = premultiplied / max(alpha, 1e-4f);
        straight = ApplyFog(straight, input.worldPosition, gCameraPosition);
        // 水際は反射ごと薄くして、discard の境目を線にしない
        alpha *= saturate((riverDepth - gRiverDepthThreshold) / max(gRiverShoreFade, 1e-4f));
        output.color = float4(straight * alpha, alpha);
        return output;
    }

    // ===== 透過（水底の床） =====
    // 屈折したレイが水底（水面 - depth）に当たる位置の床を見る。波で屈折方向が揺れて床も揺らぐ
    const float3 refractDir = refract(viewDir, normal, 1.0f / gIor);
    const float viewTravel = gDepth / max(-refractDir.y, 1e-3f);
    const float3 floorPosition = input.worldPosition + refractDir * viewTravel;

    // 床の UV（視差で、屈折したレイが凹凸に当たる所までずらす）
    float2 floorUv = floorPosition.xz * gFloorTiling;
    if (gFloorUseParallax != 0)
    {
        const float3 toEye = -refractDir;
        floorUv = FloorParallaxUV(floorUv, normalize(float3(toEye.x, toEye.z, toEye.y)));
    }
    const float3 albedo = gFloorTexture.Sample(gSampler, floorUv).rgb;

    // 床の法線（法線マップの接空間 → ワールド。T=+X / B=+Z / N=+Y）
    float3 floorNormal = float3(0.0f, 1.0f, 0.0f);
    if (gFloorUseNormalMap != 0)
    {
        float3 n = gFloorNormalMap.Sample(gSampler, floorUv).xyz * 2.0f - 1.0f;
        n.y *= gFloorNormalFlipY;
        n.xy *= gFloorNormalStrength;
        floorNormal = normalize(float3(n.x, n.z, n.y));
    }

    const float3 lightDir = normalize(-gDirectionalLight.direction);
    const float NdotL = saturate(dot(floorNormal, lightDir));
    const float shadow = CalcShadowFactor(floorPosition, floorNormal, input.position.xy);
    const float3 lightColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
    const float3 incoming = lightColor * NdotL * shadow + gAmbient;
    const float3 floorLit = albedo * incoming;

    // 光が水中を通る長さ（光源→床＋床→水面）。長いほど水の色に寄る
    const float lightTravel = gDepth / max(NdotL, 0.1f);
    const float3 transmittance = exp(-gAbsorption * (viewTravel + lightTravel));
    const float3 refraction = floorLit * transmittance + gWaterColor.rgb * incoming * (1.0f - transmittance);

    float3 color = lerp(refraction, reflection, fresnel);

    if (gDebugView == 1)
    {
        color = refraction;
    }
    else if (gDebugView == 2)
    {
        color = reflection;
    }
    else if (gDebugView == 3)
    {
        color = fresnel.xxx;
    }
    else if (gDebugView == 4)
    {
        color = normal * 0.5f + 0.5f;
    }

    color = ApplyFog(color, input.worldPosition, gCameraPosition);
    output.color = float4(color, 1.0f);
    return output;
}
