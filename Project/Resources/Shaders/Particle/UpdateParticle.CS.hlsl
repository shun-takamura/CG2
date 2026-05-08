// GPU Particle: Particleの位置・寿命・アルファを毎フレーム更新するCS（FreeList版）

struct Particle
{
    float3 translate;
    float3 scale;
    float lifeTime;
    float3 velocity;
    float currentTime;
    float4 color;
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
        // alphaが0のParticleは死んでいるとみなして更新しない
        if (gParticles[particleIndex].color.a != 0)
        {
            // velocityは「1秒あたりの移動量（速度）」として運用する
            gParticles[particleIndex].translate += gParticles[particleIndex].velocity * gPerFrame.deltaTime;
            gParticles[particleIndex].currentTime += gPerFrame.deltaTime;
            float alpha = 1.0f - (gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
            gParticles[particleIndex].color.a = saturate(alpha);

            // 今フレームで寿命が尽きた場合、FreeListに戻す
            if (gParticles[particleIndex].color.a == 0)
            {
                // VertexShader出力で潰されるようにscale=0にしておく
                gParticles[particleIndex].scale = float3(0.0f, 0.0f, 0.0f);

                int freeListIndex;
                InterlockedAdd(gFreeListIndex[0], 1, freeListIndex);
                if ((freeListIndex + 1) < (int) kMaxParticles)
                {
                    // 加算後の天辺に死んだIndexを登録
                    gFreeList[freeListIndex + 1] = particleIndex;
                }
                else
                {
                    // 来るはずがないが安全策。加算をなかったことにする
                    InterlockedAdd(gFreeListIndex[0], -1);
                }
            }
        }
    }
}
