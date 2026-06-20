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
    float2 lifeScale; // 寿命に沿ったサイズ倍率（.x=開始, .y=終了）
    float4 orientation; // 3D姿勢クオータニオン（ビルボードNone時に使用）
    float3 angularVel;  // 各軸の角速度（rad/s）
};

struct PerFrame
{
    float time;
    float deltaTime;
};

static const uint kMaxParticles = 1024;
static const uint kMaxGradientKeys = 8;

// 多色グラデーション（Fixed カラーモードで keyCount>=2 のとき有効）
// 旧 pad を Hue 回転（生存中のシームレス色変化）に転用（CB サイズ不変）。
struct ParticleGradient
{
    uint keyCount;
    float hueEnable;      // 0/1
    float hueSpeed;       // 1秒あたりの回転数（1.0=毎秒1周）
    float hueRandomPhase; // 0/1（粒子ごとに位相をばらす）
    float4 keyColor[kMaxGradientKeys];
    float4 keyLoc[kMaxGradientKeys]; // .x に位置(0..1)。CPU 側で昇順ソート済み
};

// 色相回転：輝度軸(1,1,1)まわりに RGB を ang ラジアン回す（彩度/明度を保ったまま色相だけ変える）。
float3 HueRotate(float3 c, float ang)
{
    const float3 k = float3(0.57735026f, 0.57735026f, 0.57735026f); // 1/sqrt(3)
    float cosA = cos(ang);
    return c * cosA + cross(k, c) * sin(ang) + k * dot(k, c) * (1.0f - cosA);
}

// particleIndex から 0..1 の擬似乱数（位相ばらし用）
float Hash01(uint i)
{
    return frac(sin((float) i * 12.9898f) * 43758.5453f);
}

// 周回（orbit）：粒子を center まわりに2軸で回す。spin=帯上を流れる(法線軸) / tumble=帯自体の回転(別軸)。
struct ParticleOrbit
{
    float enabled;       // 0/1
    float spinSpeed;     // 帯上を流れる速度（spinAxis まわり、rad/s）
    float tumbleSpeed;   // 帯自体の回転速度（tumbleAxis まわり、rad/s）
    float pad0;
    float3 center;
    float pad1;
    float3 spinAxis;     // 帯上の流れの軸（＝現在のリング法線。CPUが毎フレ更新）
    float pad2;
    float3 tumbleAxis;   // 帯自体の回転軸
    float pad3;
    // 収束（移動をカーブで制御：spawn位置→convergeCenter）。enable で velocity/orbit より優先。
    float convergeEnable;
    float3 pad4;
    float3 convergeCenter;
    float pad7;
    float4 convergeLUT[8]; // 32サンプル（convergeCurve を焼いた 0..1。4成分=連続4サンプル）
};

// ハミルトン積（エンジンの Multiply(q1,q2) と一致）
float4 QMul(float4 a, float4 b)
{
    return float4(
        a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
        a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
        a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
        a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z);
}

// axis まわりに v を ang 回転（ロドリゲス）
float3 RotateAxis(float3 v, float3 axis, float ang)
{
    float l = length(axis);
    if (l < 1e-5f) return v;
    float3 k = axis / l;
    float c = cos(ang);
    float s = sin(ang);
    return v * c + cross(k, v) * s + k * dot(k, v) * (1.0f - c);
}

RWStructuredBuffer<Particle> gParticles : register(u0);
RWStructuredBuffer<int> gFreeListIndex : register(u1);
RWStructuredBuffer<int> gFreeList : register(u2);
ConstantBuffer<PerFrame> gPerFrame : register(b0);
ConstantBuffer<ParticleGradient> gGradient : register(b1);
ConstantBuffer<ParticleOrbit> gOrbit : register(b2);

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

// 収束カーブ LUT（32サンプル＝float4[8]）を t(0..1) で区分線形サンプル。
float SampleConvergeLUT(float t)
{
    t = saturate(t);
    float f = t * 31.0f;
    int i = (int) floor(f);
    if (i >= 31) return gOrbit.convergeLUT[7].w;
    float fr = f - (float) i;
    int j = i + 1;
    float a = gOrbit.convergeLUT[i >> 2][i & 3];
    float b = gOrbit.convergeLUT[j >> 2][j & 3];
    return lerp(a, b, fr);
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
            if (gOrbit.convergeEnable > 0.5f)
            {
                // 収束：velocity に保持した spawn 位置から convergeCenter へ、カーブ進行度で寄せる。
                float tt = saturate(gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
                float s = SampleConvergeLUT(tt);
                gParticles[particleIndex].translate =
                    lerp(gParticles[particleIndex].velocity, gOrbit.convergeCenter, s);
            }
            else if (gOrbit.enabled > 0.5f)
            {
                // 2軸の剛体回転。半径一定＝外に出ない。
                float3 rel = gParticles[particleIndex].translate - gOrbit.center;
                // tumble：帯自体を回す（別軸）
                rel = RotateAxis(rel, gOrbit.tumbleAxis, gOrbit.tumbleSpeed * gPerFrame.deltaTime);
                // spin：帯上を流れる（現在のリング法線まわり）
                rel = RotateAxis(rel, gOrbit.spinAxis, gOrbit.spinSpeed * gPerFrame.deltaTime);
                gParticles[particleIndex].translate = gOrbit.center + rel;
            }
            else
            {
                gParticles[particleIndex].translate += gParticles[particleIndex].velocity * gPerFrame.deltaTime;
            }
            gParticles[particleIndex].currentTime += gPerFrame.deltaTime;

            // 3D姿勢：角速度ベクトルをクオータニオン積分（ジンバルロックなし）
            float3 w = gParticles[particleIndex].angularVel;
            float wl = length(w);
            if (wl > 1e-6f)
            {
                float ang = wl * gPerFrame.deltaTime;
                float h = ang * 0.5f;
                float4 dq = float4((w / wl) * sin(h), cos(h));
                float4 q = QMul(dq, gParticles[particleIndex].orientation);
                gParticles[particleIndex].orientation = normalize(q);
            }

            // 寿命比率で色補間。グラデーションキーが2個以上なら多色補間、なければ start→end の2色。
            float t = saturate(gParticles[particleIndex].currentTime / gParticles[particleIndex].lifeTime);
            float4 col;
            if (gGradient.keyCount >= 2)
            {
                col = EvalGradient(t);
            }
            else
            {
                col = lerp(
                    gParticles[particleIndex].startColor,
                    gParticles[particleIndex].endColor,
                    t);
            }
            // Hue 回転（生存中のシームレス色変化）。位相を粒子ごとにばらしてパッと変わらないようにする。
            if (gGradient.hueEnable > 0.5f)
            {
                float phase = (gGradient.hueRandomPhase > 0.5f) ? (Hash01(particleIndex) * 6.2831853f) : 0.0f;
                float ang = gGradient.hueSpeed * 6.2831853f * gParticles[particleIndex].currentTime + phase;
                col.rgb = HueRotate(col.rgb, ang);
            }
            gParticles[particleIndex].color = col;

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
