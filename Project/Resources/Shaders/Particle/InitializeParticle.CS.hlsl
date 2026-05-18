// GPU Particle: ParticleResource / FreeList / FreeListIndexを初期化するCS

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

static const uint kMaxParticles = 1024;

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int32_t> gFreeListIndex : register(u1);
RWStructuredBuffer<uint32_t> gFreeList : register(u2);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles)
    {
        // Particleは0埋め
        gParticles[particleIndex] = (Particle) 0;
        // FreeListは連番で初期化（FreeList[i] = i）
        gFreeList[particleIndex] = particleIndex;
    }

    // FreeListIndexは末尾を指すように、kMaxParticles - 1 にしておく
    if (particleIndex == 0)
    {
        gFreeListIndex[0] = kMaxParticles - 1;
    }
}
