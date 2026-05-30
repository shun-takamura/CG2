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
static const uint kMaxGradientKeys = 8;

// 多色グラデーション（Fixed カラーモードで keyCount>=2 のとき有効）
struct ParticleGradient
{
    uint keyCount;
    float3 pad;
    float4 keyColor[kMaxGradientKeys];
    float4 keyLoc[kMaxGradientKeys]; // .x に位置(0..1)。CPU 側で昇順ソート済み
};

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<int> gFreeList : register(u2);
ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleGradient> gGradient : register(b1);

float4 EvalGradient(float t)
{
    if (t <= gGradient.keyLoc[0].x) return gGradient.keyColor[0];
    for (uint i = 0; i < gGradient.keyCount - 1; ++i)
    {
        float l0 = gGradient.keyLoc[i].x;
        float l1 = gGradient.keyLoc[i + 1].x;
        if (t <= l1)
        {
            float u = (l1 > l0) ? (t - l0) / (l1 - l0) : 0.0f;
            return lerp(gGradient.keyColor[i], gGradient.keyColor[i + 1], u);
        }
    }
    return gGradient.keyColor[gGradient.keyCount - 1];
}

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

            // 寿命比率で色補間。グラデーションキーが2個以上なら多色補間、なければ start→end の2色。
            float t = saturate(gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
            if (gGradient.keyCount >= 2)
            {
                gParticles[particleIndex].color = EvalGradient(t);
            }
            else
            {
                gParticles[particleIndex].color = lerp(
                    gParticles[particleIndex].startColor,
                    gParticles[particleIndex].endColor,
                    t);
            }

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
