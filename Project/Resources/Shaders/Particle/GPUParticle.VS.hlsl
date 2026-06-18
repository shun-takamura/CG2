#include "Particle.hlsli"

// GPU Particle: Particle情報をVSで読み出してWorldMatrixを構築する

struct Particle
{
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
    float4 startColor;
    float4 endColor;
    float2 lifeScale; // 寿命に沿ったサイズ倍率（.x=開始, .y=終了）
    float4 orientation; // 3D姿勢クオータニオン（ビルボードNone時に使用）
    float3 angularVel;  // 各軸の角速度（rad/s）
};

struct PerView
{
    float4x4 viewProjection;
    float4x4 billboardMatrix;   // Full 用（CPU側で構築済み）
    float3   cameraPosition;    // YAxis 用
    uint     billboardMode;     // 0=None, 1=Full, 2=YAxis
};

StructuredBuffer<Particle> gParticles : register(t0);
ConstantBuffer<PerView> gPerView : register(b0);

struct VertexShaderInput
{
    float4 position : POSITION0;
    float2 texcoord : TEXCOORD0;
    float3 normal : NORMAL0;
};

VertexShaderOutput main(VertexShaderInput input, uint instanceId : SV_InstanceID)
{
    VertexShaderOutput output;
    Particle particle = gParticles[instanceId];

    // ビルボードモードに応じて回転部分を構築
    float4x4 worldMatrix;
    if (gPerView.billboardMode == 1) {
        // Full：CPU で計算済みのスクリーン整列行列
        worldMatrix = gPerView.billboardMatrix;
    } else if (gPerView.billboardMode == 2) {
        // YAxis：ワールドY軸固定で水平にカメラを向く
        float fx = gPerView.cameraPosition.x - particle.translate.x;
        float fz = gPerView.cameraPosition.z - particle.translate.z;
        float len = sqrt(fx * fx + fz * fz);
        if (len < 1e-5f) { fx = 0.0f; fz = 1.0f; }
        else             { fx /= len; fz /= len; }
        // forward=(fx,0,fz)、up=(0,1,0)、right=(fz,0,-fx)
        worldMatrix = float4x4(
             fz,   0.0f, -fx,  0.0f,
             0.0f, 1.0f, 0.0f, 0.0f,
             fx,   0.0f, fz,   0.0f,
             0.0f, 0.0f, 0.0f, 1.0f);
    } else {
        // None：ビルボードなし。粒子の3D姿勢クオータニオンから回転行列を組む
        // （エンジンの MakeRotateMatrix(q) と同じ行ベクトル配置）。
        float qx = particle.orientation.x;
        float qy = particle.orientation.y;
        float qz = particle.orientation.z;
        float qw = particle.orientation.w;
        float xx = qx * qx, yy = qy * qy, zz = qz * qz;
        float xy = qx * qy, xz = qx * qz, yz = qy * qz;
        float wx = qw * qx, wy = qw * qy, wz = qw * qz;
        worldMatrix = float4x4(
            1.0f - 2.0f * (yy + zz), 2.0f * (xy + wz),        2.0f * (xz - wy),        0.0f,
            2.0f * (xy - wz),        1.0f - 2.0f * (xx + zz), 2.0f * (yz + wx),        0.0f,
            2.0f * (xz + wy),        2.0f * (yz - wx),        1.0f - 2.0f * (xx + yy), 0.0f,
            0.0f,                    0.0f,                    0.0f,                    1.0f);
    }

    // 寿命に沿ったサイズ倍率（Size over Lifetime）。発生時サイズ × lerp(start, end, 寿命比率)。
    float lifeT = (particle.lifeTime > 0.0f) ? saturate(particle.currentTime / particle.lifeTime) : 0.0f;
    float sizeMul = lerp(particle.lifeScale.x, particle.lifeScale.y, lifeT);

    // スケールと位置を組み込んで完成
    worldMatrix[0] *= particle.scale.x * sizeMul;
    worldMatrix[1] *= particle.scale.y * sizeMul;
    worldMatrix[2] *= particle.scale.z * sizeMul;
    worldMatrix[3].xyz = particle.translate;

    output.position = mul(input.position, mul(worldMatrix, gPerView.viewProjection));
    output.texcoord = input.texcoord;
    output.normal = input.normal;
    output.color = particle.color;
    output.dissolveLife = lifeT; // 粒子ごとの寿命比率（ディゾルブ用）
    return output;
}
