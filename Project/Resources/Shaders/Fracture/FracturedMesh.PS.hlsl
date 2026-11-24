#include "FracturedMesh.hlsli"
#include "../Object3D/Fog.hlsli"
#define DISSOLVE_NO_MATERIAL
#include "../Object3D/Dissolve.hlsli"

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

Texture2D<float4> gExteriorBase : register(t1);
Texture2D<float4> gExteriorNormal : register(t2);
Texture2D<float4> gInteriorBase : register(t3);
Texture2D<float4> gInteriorNormal : register(t4);
SamplerState gSampler : register(s0);

struct PixelShaderOutput
{
    float4 color : SV_TARGET0;
};

// OpenGL 規約（G = テクスチャの上）の法線マップを、UV の (u, v) 軸の接空間へ
float3 UnpackNormal(float4 texel)
{
    float3 n = texel.xyz * 2.0f - 1.0f;
    n.y = -n.y;   // D3D の UV は v が下向き
    n.xy *= gNormalStrength;
    return n;
}

// 三平面投影。岩の空間の位置と法線から色と法線（岩の空間）を求める（法線の合成は whiteout）
void SampleTriplanar(Texture2D<float4> baseTex, Texture2D<float4> normalTex, float3 p, float3 n,
    out float3 albedo, out float3 normal)
{
    float3 w = pow(abs(n), 4.0f);
    w /= max(w.x + w.y + w.z, 1e-5f);

    float2 uvX = p.zy;
    float2 uvY = p.xz;
    float2 uvZ = p.xy;

    albedo = baseTex.Sample(gSampler, uvX).rgb * w.x
           + baseTex.Sample(gSampler, uvY).rgb * w.y
           + baseTex.Sample(gSampler, uvZ).rgb * w.z;

    float3 tnX = UnpackNormal(normalTex.Sample(gSampler, uvX));
    float3 tnY = UnpackNormal(normalTex.Sample(gSampler, uvY));
    float3 tnZ = UnpackNormal(normalTex.Sample(gSampler, uvZ));
    tnX = float3(tnX.xy + n.zy, abs(tnX.z) * n.x);
    tnY = float3(tnY.xy + n.xz, abs(tnY.z) * n.y);
    tnZ = float3(tnZ.xy + n.xy, abs(tnZ.z) * n.z);
    normal = normalize(tnX.zyx * w.x + tnY.xzy * w.y + tnZ.xyz * w.z);
}

PixelShaderOutput main(VertexShaderOutput input, bool isFrontFace : SV_IsFrontFace)
{
    float edge = 0.0f;
    if (input.visibility < 0.999f)
    {
        // 模様は岩の空間に貼り付けるので、破片が飛んでも溶け方の模様が流れない
        edge = DissolveClip(input.rockPosition * input.textureScale, input.visibility, gDissolveEdgeWidth,
            0.0f, 1.0f, gDissolveNoiseScale, 1.0f);
    }

    float3 rockNormal = normalize(input.rockNormal);
    float3 p = input.rockPosition * (gTextureTiling * input.textureScale);
    bool interior = input.interior > 0.5f;
    // 裏面はディゾルブ中（カリングなしの PSO）だけ届く。穴から覗いた岩の内側なので、法線を返して断面の模様で暗く塗る
    bool backface = !isFrontFace;
    if (backface)
    {
        rockNormal = -rockNormal;
        interior = true;
    }

    float3 albedo;
    float3 bumped;
    if (interior)
    {
        SampleTriplanar(gInteriorBase, gInteriorNormal, p, rockNormal, albedo, bumped);
        albedo *= gInteriorTint.rgb * (backface ? gBackfaceShade : 1.0f);
    }
    else
    {
        SampleTriplanar(gExteriorBase, gExteriorNormal, p, rockNormal, albedo, bumped);
        albedo *= gExteriorTint.rgb;
    }

    // 岩の空間で凹凸を付けた法線をワールドへ（mul(bumped, 回転) と同じ）
    float3 N = normalize(bumped.x * input.axis0 + bumped.y * input.axis1 + bumped.z * input.axis2);

    float3 L = normalize(-gDirectionalLight.direction);
    float3 V = normalize(gCameraPosition - input.worldPosition);
    float ndl = saturate(dot(N, L));
    float3 H = normalize(L + V);
    float spec = pow(saturate(dot(N, H)), 24.0f) * 0.08f;

    float3 lightColor = gDirectionalLight.color.rgb * gDirectionalLight.intensity;
    float3 color = albedo * (gAmbient + lightColor * ndl) + lightColor * spec;
    if (interior)
    {
        color += albedo * gInteriorEmissive;
    }
    color = ApplyFog(color, input.worldPosition, gCameraPosition);
    color += gDissolveEdgeColor * edge;

    PixelShaderOutput output;
    output.color = float4(color, 1.0f);
    return output;
}
