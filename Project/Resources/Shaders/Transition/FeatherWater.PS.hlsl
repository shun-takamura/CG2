// 羽根の着水（FeatherLandTransition）の背景の描画。羽根はこの上に FeatherRenderer が描く。
//   ・取り込んだシーン（覆う側ではステージ、開く側ではゲームオーバー画面）を瞼で閉じる（Eyelid.hlsli）
//   ・暗い水面を 3D の平面として描く：仮想カメラの視線と水平な面（y = waterY）の交点を求め、
//     そこの波の高さ（RippleSimulation、水面のワールド XZ で解く）から法線を作って照り返しを付ける
//   ・浮かび上がり：交点の波の傾きで歪ませたシーンが、揺らぎの中からじわっと現れる
//     （全体がゆっくり現れ、着地点に近いほど少し早い。出始めは強く歪み、現れ切るにつれ落ち着く）

#include "../PostEffect/Common/PostProcess.hlsli"
#include "ScreenWater.hlsli"
#include "InkShape.hlsli"
#include "Eyelid.hlsli"

cbuffer FeatherWaterParams : register(b0)
{
    ScreenWaterParams gWater;   // rippleCenter / rippleSize は水面のワールド XZ（ScreenWaterSurface::SetRippleWorldCenter）
    float4x4 gInverseViewProjection; // 仮想カメラ（FeatherRenderer のカメラ）
    float3 gCameraPosition;
    float gWaterY;              // 水面の高さ
    float gEyelidClosure;       // 瞼の閉じ具合 0..1
    float gEyelidCurvature;
    float gEyelidSoftness;
    float gEyelidCloseY;
    float gWaterVisibility;     // 水面の見え方 0..1（暗闇から水面が現れる）
    float gEmerge;              // 浮かび上がりの進み 0..1
    float gTime;
    float gFogDensity;          // 遠くの水面を暗闇へ溶かす
    float3 gWaterColor;         // 水の色（暗い）
    float gSpecular;            // 照り返しの強さ
    float3 gSkyColor;           // 水面に映る空の色（斜めほど強く映る）
    float gWaveNormalScale;     // 波の高さの傾きを法線に足す倍率
    float2 gEmergeCenter;       // 浮かび上がりの中心（着地点の画面 UV）
    float gEmergeRadialBias;    // 着地点に近いほど早く現れる度合い（0 = 全体が同時）
    float gEmergeNoise;         // 現れ方のむら（ノイズ）
    float3 gLightDirection;     // 光の進む向き（羽根と同じ。正規化済み）
    float gEmergeDistortion;    // 出始めの歪みの強さ（屈折の何倍か。現れ切ると 1 倍）
};

// 画面 UV → 仮想カメラから見た視線（ワールド）
float3 ViewRay(float2 uv)
{
    const float2 ndc = float2(uv.x * 2.0f - 1.0f, 1.0f - uv.y * 2.0f);
    const float4 nearPoint = mul(float4(ndc, 0.0f, 1.0f), gInverseViewProjection);
    const float4 farPoint = mul(float4(ndc, 1.0f, 1.0f), gInverseViewProjection);
    return normalize(farPoint.xyz / farPoint.w - nearPoint.xyz / nearPoint.w);
}

// 水面のワールド XZ の波の傾き（dh/dx, dh/dz）
float2 WaterSlope(float2 xz)
{
    const float2 r = (xz - gWater.rippleCenter) / gWater.rippleSize + 0.5f;
    const float texel = kRippleTexel;
    const float hx = gRippleHeight.SampleLevel(gLinearClampSampler, r + float2(texel, 0.0f), 0).r - gRippleHeight.SampleLevel(gLinearClampSampler, r - float2(texel, 0.0f), 0).r;
    const float hz = gRippleHeight.SampleLevel(gLinearClampSampler, r + float2(0.0f, texel), 0).r - gRippleHeight.SampleLevel(gLinearClampSampler, r - float2(0.0f, texel), 0).r;
    const float worldTexel = gWater.rippleSize * texel * 2.0f;
    return float2(hx, hz) / worldTexel;
}

float4 main(VertexShaderOutput input) : SV_TARGET
{
    const float2 uv = input.texcoord;

    // 1. 瞼で閉じたシーン（開く側では閉じ切っているので黒）
    EyelidParams lid;
    lid.closure = gEyelidClosure;
    lid.curvature = gEyelidCurvature;
    lid.softness = gEyelidSoftness;
    lid.closeY = gEyelidCloseY;
    const float4 scene = gScene.SampleLevel(gLinearClampSampler, uv, 0);
    float3 color = scene.rgb * EyelidVisible(lid, uv);

    // 2. 水面（見えていなければ飛ばす）
    float2 refractedUV = uv;
    if (gWaterVisibility > 0.0f || gEmerge > 0.0f)
    {
        const float3 ray = ViewRay(uv);
        float3 water = 0.0f.xxx;
        if (ray.y < -1e-4f)
        {
            const float t = (gWaterY - gCameraPosition.y) / ray.y;
            const float3 hit = gCameraPosition + ray * t;
            const float2 slope = WaterSlope(hit.xz) * gWaveNormalScale;
            const float3 normal = normalize(float3(-slope.x, 1.0f, -slope.y));

            // 斜めに見るほど空が映る（フレネル）＋光の照り返し。遠くは暗闇へ
            const float facing = saturate(dot(normal, -ray));
            const float fresnel = pow(1.0f - facing, 5.0f);
            const float3 reflected = reflect(ray, normal);
            const float specular = pow(saturate(dot(reflected, -gLightDirection)), 48.0f) * gSpecular;
            const float fog = exp(-t * gFogDensity);
            water = (gWaterColor + gSkyColor * fresnel + specular.xxx) * fog;

            // 浮かび上がるシーンは波の傾きで歪ませる（画面の縦横は XZ の向きにおおよそ対応する）
            const float distortion = 1.0f + gEmergeDistortion * (1.0f - gEmerge);
            refractedUV = uv + float2(slope.x, -slope.y) * gWater.refraction * distortion;
        }
        color = lerp(color, water, saturate(gWaterVisibility));
    }

    // 3. 浮かび上がり：全体がじわっと現れる。着地点に近いほど少し早く、ノイズで現れ方にむらを付ける。最後は全面
    if (gEmerge > 0.0f)
    {
        const float2 fromCenter = ToWidthSpace(gWater, uv) - ToWidthSpace(gWater, gEmergeCenter);
        const float noise = InkNoise(ToWidthSpace(gWater, uv) * 3.0f + gTime * 0.05f).r;
        // 着地点から画面の端まで（画面の幅 = 1 で約 1）を 0..1 に
        const float distance01 = saturate(length(fromCenter) / 1.1f + (noise - 0.5f) * gEmergeNoise);
        const float local = saturate(gEmerge * (1.0f + gEmergeRadialBias) - gEmergeRadialBias * distance01);
        const float mask = smoothstep(0.0f, 1.0f, local);
        const float3 emerged = gScene.SampleLevel(gLinearClampSampler, refractedUV, 0).rgb;
        color = lerp(color, emerged, mask);
    }

    return float4(color, scene.a);
}
