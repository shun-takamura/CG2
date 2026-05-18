// GPU Particle: Particleの位置・寿命・色を毎フレーム更新するCS（FreeList版）

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

struct PerFrame
{
    float time;
    float deltaTime;
};

static const uint kMaxParticles = 1024;

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<int> gFreeList : register(u2);
ConstantBuffer<PerFrame> gPerFrame : register(b0);

[numthreads(1024, 1, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    uint particleIndex = DTid.x;
    if (particleIndex < kMaxParticles)
    {
        // lifeTime > 0 を生存条件とする（Init CS 直後は全粒子 lifeTime=0 で死亡扱い）
        if (gParticles[particleIndex].lifeTime > 0.0f)
        {
            gParticles[particleIndex].translate += gParticles[particleIndex].velocity * gPerFrame.deltaTime;
            gParticles[particleIndex].currentTime += gPerFrame.deltaTime;

            // 寿命比率で色補間
            float t = saturate(gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
            gParticles[particleIndex].color = lerp(
                gParticles[particleIndex].startColor,
                gParticles[particleIndex].endColor,
                t);

            // 寿命を超えたら FreeList に返す
            if (gParticles[particleIndex].currentTime >= gParticles[particleIndex].lifeTime)
            {
                gParticles[particleIndex].lifeTime = 0.0f;          // 死亡フラグ
                gParticles[particleIndex].scale    = float3(0, 0, 0); // VS で潰す
                gParticles[particleIndex].color.a  = 0.0f;          // PS discard

                int freeListIndex;
                InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
                if ((freeListIndex + 1) < (int) kMaxParticles)
                {
                    gFreeList[freeListIndex + 1] = particleIndex;
                }
                else
                {
                    InterlockedAdd(gFreeListIndex[0], -1);
                }
            }
        }
    }
}
