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
};

struct EmitterSphere
{
    float3 translate;
    float radius;
    uint count;
    float frequency;
    float frequencyTime;
    uint emit;
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
            // FreeListIndexを1つ前に進めて、減算前のIndexを取得
            int freeListIndex;
            InterlockedAdd(gFreeListIndex[0], -1, freeListIndex);

            if (0 <= freeListIndex && freeListIndex < (int) kMaxParticles)
            {
                // 取得できた空きスロット
                uint particleIndex = gFreeList[freeListIndex];

                gParticles[particleIndex].scale = generator.Generate3d() * 0.5f;
                gParticles[particleIndex].translate =
                    gEmitter.translate + (generator.Generate3d() * 2.0f - 1.0f) * gEmitter.radius;
                gParticles[particleIndex].color.rgb = generator.Generate3d();
                gParticles[particleIndex].color.a = 1.0f;
                // velocityは「1秒あたりの移動量（速度）」。60FPS時にこれまでの 0.05/フレーム と同じ見た目になるよう 0.05*60=3.0
                gParticles[particleIndex].velocity = (generator.Generate3d() * 2.0f - 1.0f) * 3.0f;
                gParticles[particleIndex].lifeTime = 1.0f;
                gParticles[particleIndex].currentTime = 0.0f;
            }
            else
            {
                // 空きが取れなかった。減らした分を戻して、以降の射出は諦める
                InterlockedAdd(gFreeListIndex[0], 1);
                break;
            }
        }
    }
}
