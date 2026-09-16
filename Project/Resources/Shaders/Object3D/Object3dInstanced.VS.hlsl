#include "Object3d.hlsli"

// インスタンシング描画用の頂点シェーダ。
// 通常の Object3d.VS は座標変換行列を cbuffer(b0) から受け取るが、こちらは
// 頂点バッファの slot 1（PER_INSTANCE_DATA）からインスタンスごとに受け取る。
// ルートシグネチャは Object3d と共通（b0/VERTEX は宣言しないので未バインドでも問題ない）。
// PS は Object3dNoEnv.PS をそのまま使う。

struct VertexShaderInput
{
    // slot 0: メッシュ頂点
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;

    // slot 1: インスタンスごとの変換行列（float4x4 は float4 × 4 に分解して受け取る）
    float4 iWVP0 : INSTWVP0;
    float4 iWVP1 : INSTWVP1;
    float4 iWVP2 : INSTWVP2;
    float4 iWVP3 : INSTWVP3;
    float4 iWorld0 : INSTWORLD0;
    float4 iWorld1 : INSTWORLD1;
    float4 iWorld2 : INSTWORLD2;
    float4 iWorld3 : INSTWORLD3;
    float4 iWIT0 : INSTWIT0;
    float4 iWIT1 : INSTWIT1;
    float4 iWIT2 : INSTWIT2;
    float4 iWIT3 : INSTWIT3;
};

VertexShaderOutput main(VertexShaderInput input)
{
    float4x4 WVP = float4x4(input.iWVP0, input.iWVP1, input.iWVP2, input.iWVP3);
    float4x4 World = float4x4(input.iWorld0, input.iWorld1, input.iWorld2, input.iWorld3);
    float4x4 WorldInverseTranspose = float4x4(input.iWIT0, input.iWIT1, input.iWIT2, input.iWIT3);

    VertexShaderOutput output;
    output.position = mul(input.position, WVP);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) WorldInverseTranspose));
    output.worldPosition = mul(input.position, World).xyz;

    float3 T = normalize(mul(input.tangent.xyz, (float3x3) World));
    T = normalize(T - output.normal * dot(output.normal, T));
    output.tangent = T;
    output.bitangent = cross(output.normal, T) * input.tangent.w;
    return output;
}
