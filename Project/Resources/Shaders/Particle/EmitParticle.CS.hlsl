#include "Random.hlsli"

// GPU Particle: EmitterSphereの設定に従ってParticleを発生させるCS（FreeList版）

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
};

struct EmitterSphere
{
    float3 translate;
    float radius;
    uint count;
    float frequency;
    float frequencyTime;
    uint emit;
    uint colorMode;       // 0=Random, 1=Fixed
    float3 baseVelocity;  // velocityMode!=0 のとき、この方向＋ジッタを初速にする
    float4 startColor;
    float4 endColor;
    float2 scaleMin;
    float2 scaleMax;
    uint   uniformScale;  // 0=per-axis ランダム, 1=幅=高さ強制
    float  particleLifeTime; // emit時に各粒子へ設定する寿命
    float  velocityMode;  // 0=ランダム(従来), 1=baseVelocity+ジッタ
    float  velocityJitter;// velocityMode!=0 のときの速度ゆらぎ量
};

struct PerFrame
{
    float time;
    float deltaTime;
};

static const uint kMaxParticles = 1024;

ConstantBuffer<EmitterSphere> gEmitter : register(b0);
ConstantBuffer<PerFrame> gPerFrame : register(b1);
RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<uint> gFreeList : register(u2);

[numthreads(1, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    RandomGenerator generator;
    generator.seed = (float3(DTid) + gPerFrame.time) * gPerFrame.time;

    if (gEmitter.emit != 0)
    {
        for (uint countIndex = 0; countIndex < gEmitter.count; ++countIndex)
        {
            int freeListIndex;
            InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);

            if (0 <= freeListIndex && freeListIndex < (int) kMaxParticles)
            {
                uint particleIndex = gFreeList[freeListIndex];

                // スケール：uniform フラグで「幅=高さ」を強制 or per-axis
                float3 randTriple = generator.Generate3d();
                float sx, sy;
                if (gEmitter.uniformScale != 0) {
                    // 単一のランダム値で W=H に統一
                    float t = randTriple.x;
                    sx = lerp(gEmitter.scaleMin.x, gEmitter.scaleMax.x, t);
                    sy = sx;
                } else {
                    sx = lerp(gEmitter.scaleMin.x, gEmitter.scaleMax.x, randTriple.x);
                    sy = lerp(gEmitter.scaleMin.y, gEmitter.scaleMax.y, randTriple.y);
                }
                // ビルボード時 z は不要だが、None モード時の見た目維持で x と同じに
                gParticles[particleIndex].scale = float3(sx, sy, sx);
                float3 spawnOffset = (generator.Generate3d() * 2.0f - 1.0f) * gEmitter.radius;
                gParticles[particleIndex].translate = gEmitter.translate + spawnOffset;
                if (gEmitter.velocityMode > 1.5f) {
                    // 放射モード：発生位置の中心からのオフセット方向へ baseVelocity.x の速さで飛ばす
                    float len = length(spawnOffset);
                    float3 d = (len > 1e-5f) ? (spawnOffset / len) : float3(0.0f, 1.0f, 0.0f);
                    gParticles[particleIndex].velocity =
                        d * gEmitter.baseVelocity.x + (generator.Generate3d() * 2.0f - 1.0f) * gEmitter.velocityJitter;
                } else if (gEmitter.velocityMode > 0.5f) {
                    // 方向固定モード：baseVelocity を初速に、velocityJitter ぶんだけランダムに散らす
                    gParticles[particleIndex].velocity =
                        gEmitter.baseVelocity + (generator.Generate3d() * 2.0f - 1.0f) * gEmitter.velocityJitter;
                } else {
                    // 従来：全方向ランダム
                    gParticles[particleIndex].velocity = (generator.Generate3d() * 2.0f - 1.0f) * 3.0f;
                }
                gParticles[particleIndex].lifeTime = gEmitter.particleLifeTime;
                gParticles[particleIndex].currentTime = 0.0f;

                // 色モード
                if (gEmitter.colorMode == 0) {
                    // Random：startは乱数RGB+α1.0、endは同色α0でフェードアウト
                    float3 rnd = generator.Generate3d();
                    gParticles[particleIndex].startColor = float4(rnd, 1.0f);
                    gParticles[particleIndex].endColor   = float4(rnd, 0.0f);
                } else {
                    // Fixed：エミッタ指定の start→end を使う
                    gParticles[particleIndex].startColor = gEmitter.startColor;
                    gParticles[particleIndex].endColor   = gEmitter.endColor;
                }
                // 初期の現在色は startColor
                gParticles[particleIndex].color = gParticles[particleIndex].startColor;
            }
            else
            {
                InterlockedAdd(gFreeListIndex[0], 1);
                break;
            }
        }
    }
}
