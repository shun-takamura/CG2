#include "Object3d.hlsli"

// 平面リフレクション用 VS。
// 映る物の CB（b0）からは World だけを読み、鏡像 ViewProj は鏡側の CB（b1）から読む。
// これで映る物ごとに CB を増やさずに、同じ物を本編と反射の2回描き分けられる。
// PS は通常描画と同じもの（ライティング／影／フォグがそのまま効く）。

struct TransformationMatrix
{
    float4x4 WVP;
    float4x4 World;
    float4x4 WorldInverseTranspose;
};

cbuffer TransformationMatrixBuffer : register(b0)
{
    TransformationMatrix transformationMatrix;
};

cbuffer ReflectionCameraBuffer : register(b1)
{
    float4x4 gReflectionViewProj; // 鏡像行列 × 本編カメラの ViewProj
    float4   gClipPlane;          // 水面（xyz=法線, w=距離）。水面より下の物は映さない
};

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float4 tangent : TANGENT0;
};

// PS の入力は VertexShaderOutput と同じ並び。クリップ距離は末尾に足す（PS は読まない）
struct ReflectionVertexOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
    float3 worldPosition : POSITION;
    float3 tangent : TANGENT0;
    float3 bitangent : BINORMAL0;
    float clipDistance : SV_ClipDistance0;
};

ReflectionVertexOutput main(VertexShaderInput input)
{
    ReflectionVertexOutput output;
    float4 worldPosition = mul(input.position, transformationMatrix.World);
    output.position = mul(worldPosition, gReflectionViewProj);
    output.texcoord = input.texcoord;
    output.normal = normalize(mul(input.normal, (float3x3) transformationMatrix.WorldInverseTranspose));
    // ライティングは実物の位置で計算する（視点側を鏡像カメラにすることで反射の見え方になる）
    output.worldPosition = worldPosition.xyz;

    float3 T = normalize(mul(input.tangent.xyz, (float3x3) transformationMatrix.World));
    T = normalize(T - output.normal * dot(output.normal, T));
    output.tangent = T;
    output.bitangent = cross(output.normal, T) * input.tangent.w;

    output.clipDistance = dot(float4(worldPosition.xyz, 1.0f), gClipPlane);
    return output;
}
